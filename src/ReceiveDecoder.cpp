#include "ReceiveDecoder.h"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <nlohmann/json.hpp>

namespace {
// Bounds match the existing backend input contracts; registration is not RF QA.
constexpr std::array<ReceiveDecoderDescriptor,3> descriptors{{
    {"rds","Broadcast FM RDS",DecoderInputDomain::FmMultiplex,128000,384000,262144,false,"sdrtown_rds_dsp.dll"},
    {"ctcss","CTCSS identification",DecoderInputDomain::FmDiscriminator,8000,96000,262144,true,""},
    {"dcs","DCS identification",DecoderInputDomain::FmDiscriminator,8000,96000,262144,true,""}
}};

template<class Backend,size_t Index> class Adapter final : public ReceiveDecoder {
public:
    const ReceiveDecoderDescriptor& descriptor() const override { return descriptors[Index]; }
    ReceiveDecoderSnapshot snapshot() const override { return backend_.snapshot(); }
    void reset() override { backend_.reset(); sourceKnown_=false; }
    DecoderProcessResult process(const ReceiveDecoderBlock& block) override {
        auto reject=[&](DecoderInputError error,const char* message) {
            reset(); return DecoderProcessResult{error,message};
        };
        const auto& spec=descriptor();
        if (block.contractVersion!=spec.contractVersion)
            return reject(DecoderInputError::ContractVersion,"Unsupported decoder input contract version");
        if (block.domain!=spec.input)
            return reject(DecoderInputError::InputDomain,"Wrong decoder input domain");
        if (!std::isfinite(block.sampleRateHz) || !std::isfinite(block.targetHz) ||
            block.sampleRateHz<spec.minimumRateHz || block.sampleRateHz>spec.maximumRateHz ||
            block.samples.size()>spec.maximumBlockSamples ||
            block.firstSample>std::numeric_limits<uint64_t>::max()-block.samples.size())
            return reject(DecoderInputError::InvalidBlock,"Invalid decoder block metadata or size");
        // A device/source switch must not inherit another stream's native state.
        const bool gap=block.discontinuity || !sourceKnown_ || sourceId_!=block.sourceId;
        if (!backend_.process(block.samples,block.sampleRateHz,block.targetHz,
                              block.epoch,block.firstSample,gap)) {
            sourceKnown_=false;
            return {DecoderInputError::BackendRejected,backend_.snapshot().status};
        }
        sourceKnown_=true; sourceId_=block.sourceId;
        return {};
    }
private:
    Backend backend_;
    uint64_t sourceId_=0;
    bool sourceKnown_=false;
};

auto comparable(const RdsMpxSnapshot& s) {
    return std::tie(s.samples,s.bits,s.groups,s.correctedBlocks,s.rejectedGroups,
        s.resets,s.targetHz,s.sampleRate,s.status,s.lastGroupWords,
        s.station.identified,s.station.pi,s.station.pty,s.station.trafficProgramme,
        s.station.trafficAnnouncement,s.station.programmeService,s.station.radioText);
}

// DEC-0086: diagnostic only; no file IO in process(), no retained sample buffers.
class RdsParity final : public ReceiveDecoder {
public:
    explicit RdsParity(std::filesystem::path path) : path_(std::move(path)) {}
    ~RdsParity() override {
        if (!blocks_) return;
        try {
            nlohmann::json report{{"blocks",blocks_},{"samples",samples_},
                {"mismatches",mismatches_},{"resetMismatches",resetMismatches_},
                {"adapterFailures",failures_},{"sourceId",sourceId_},
                {"adapterGroups",lastA_.groups},{"nativeGroups",lastB_.groups},
                {"adapterBits",lastA_.bits},{"nativeBits",lastB_.bits},
                {"resets",lastA_.resets},{"sampleRate",lastA_.sampleRate},
                {"targetHz",lastA_.targetHz},{"pi",lastA_.station.pi},
                {"ps",lastA_.station.programmeService},{"status",lastA_.status}};
            std::ofstream file(path_,std::ios::app);
            file << report.dump() << '\n';
        } catch (...) { /* Diagnostics must not throw during receiver teardown. */ }
    }
    const ReceiveDecoderDescriptor& descriptor() const override { return adapter_.descriptor(); }
    ReceiveDecoderSnapshot snapshot() const override { return adapter_.snapshot(); }
    void reset() override {
        adapter_.reset(); native_.reset(); known_=false;
        if (!equal(std::get<RdsMpxSnapshot>(adapter_.snapshot()),native_.snapshot())) ++resetMismatches_;
    }
    DecoderProcessResult process(const ReceiveDecoderBlock& block) override {
        const auto result=adapter_.process(block);
        bool nativeOk=false;
        if (result.error==DecoderInputError::ContractVersion ||
            result.error==DecoderInputError::InputDomain || result.error==DecoderInputError::InvalidBlock) {
            native_.reset();
        } else {
            nativeOk=native_.process(block.samples,block.sampleRateHz,block.targetHz,
                block.epoch,block.firstSample,block.discontinuity || !known_ || sourceId_!=block.sourceId);
        }
        known_=bool(result); sourceId_=block.sourceId;
        lastA_=std::get<RdsMpxSnapshot>(adapter_.snapshot()); lastB_=native_.snapshot();
        ++blocks_; samples_+=block.samples.size(); failures_+=!bool(result);
        if (bool(result)!=nativeOk || !equal(lastA_,lastB_)) ++mismatches_;
        return result;
    }
private:
    static bool equal(const RdsMpxSnapshot& a,const RdsMpxSnapshot& b) {
        return comparable(a)==comparable(b) && (a.lastGroupMs!=0)==(b.lastGroupMs!=0);
    }
    Adapter<RdsMpxDecoder,0> adapter_;
    RdsMpxDecoder native_;
    std::filesystem::path path_;
    RdsMpxSnapshot lastA_,lastB_;
    uint64_t blocks_=0,samples_=0,mismatches_=0,resetMismatches_=0,failures_=0,sourceId_=0;
    bool known_=false;
};
}

std::span<const ReceiveDecoderDescriptor> receiveDecoderRegistry() { return descriptors; }
std::unique_ptr<ReceiveDecoder> createReceiveDecoder(std::string_view id) {
    if (id=="rds") {
        wchar_t* path=nullptr; size_t length=0;
        if (_wdupenv_s(&path,&length,L"SDR_TOWN_RDS_PARITY_LOG")==0 && path) {
            std::unique_ptr<wchar_t,decltype(&std::free)> owner(path,&std::free);
            if (length>1) return std::make_unique<RdsParity>(std::filesystem::path(path));
        }
        return std::make_unique<Adapter<RdsMpxDecoder,0>>();
    }
    if (id=="ctcss") return std::make_unique<Adapter<CtcssDecoder,1>>();
    if (id=="dcs") return std::make_unique<Adapter<DcsDecoder,2>>();
    throw std::invalid_argument("Unknown receive decoder ID");
}
std::string_view decoderInputName(DecoderInputDomain domain) {
    switch(domain) {
        case DecoderInputDomain::FmMultiplex: return "raw-fm-multiplex";
        case DecoderInputDomain::FmDiscriminator: return "raw-fm-discriminator";
    }
    return "unknown";
}
