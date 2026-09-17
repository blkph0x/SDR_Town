#pragma once
#include "RdsMpxDecoder.h"
#include "CtcssDecoder.h"
#include "DcsDecoder.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

// DEC-0085: only real supported float domains. IQ/audio contracts will be added
// with their first implemented consumer, not masqueraded as discriminator data.
enum class DecoderInputDomain { FmMultiplex, FmDiscriminator };
struct ReceiveDecoderDescriptor {
    std::string_view id, name;
    DecoderInputDomain input;
    double minimumRateHz, maximumRateHz;
    size_t maximumBlockSamples;
    bool experimental;
    std::string_view requiredModule; // Empty for native code; not an availability probe.
    uint32_t contractVersion = 1;
};

struct ReceiveDecoderBlock {
    std::span<const float> samples;
    DecoderInputDomain domain = DecoderInputDomain::FmMultiplex;
    double sampleRateHz = 0, targetHz = 0;
    uint64_t sourceId = 0, epoch = 0, firstSample = 0;
    bool discontinuity = false;
    uint32_t contractVersion = 1;
};
enum class DecoderInputError { None, ContractVersion, InputDomain, InvalidBlock, BackendRejected };
struct DecoderProcessResult {
    DecoderInputError error = DecoderInputError::None;
    std::string detail;
    explicit operator bool() const { return error == DecoderInputError::None; }
};
using ReceiveDecoderSnapshot = std::variant<RdsMpxSnapshot,CtcssSnapshot,DcsSnapshot>;

class ReceiveDecoder {
public:
    virtual ~ReceiveDecoder() = default;
    virtual const ReceiveDecoderDescriptor& descriptor() const = 0;
    // One DSP owner for process/reset; consume synchronously, never retain input.
    virtual DecoderProcessResult process(const ReceiveDecoderBlock& block) = 0;
    virtual void reset() = 0;
    // May be polled from another thread. Native counters retain backend semantics.
    virtual ReceiveDecoderSnapshot snapshot() const = 0;
};

std::span<const ReceiveDecoderDescriptor> receiveDecoderRegistry();
std::unique_ptr<ReceiveDecoder> createReceiveDecoder(std::string_view id);
std::string_view decoderInputName(DecoderInputDomain domain);
