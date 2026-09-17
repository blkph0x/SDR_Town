#include "RdsMpxDecoder.h"
#include "RdsDspAbi.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

struct RdsMpxDecoder::Impl {
    HMODULE module = nullptr;
    decltype(&rds_dsp_create) create = nullptr;
    decltype(&rds_dsp_destroy) destroy = nullptr;
    decltype(&rds_dsp_process) process = nullptr;
    void* handle = nullptr;
    RdsDecoder bits;
    RdsMpxSnapshot stats;
    uint64_t epoch = 0, nextSample = 0;
    bool attemptedLoad = false;
    ~Impl() { discard(); if(module) FreeLibrary(module); }
    void discard() { if(handle) { destroy(handle); handle=nullptr; } bits.reset(); }
    bool load() {
        if (attemptedLoad) return module && create && destroy && process;
        attemptedLoad = true;
        std::array<wchar_t,32768> path{};
        const DWORD length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if (!length || length>=path.size()) return false;
        const auto dll=std::filesystem::path(path.data()).parent_path()/L"sdrtown_rds_dsp.dll";
        module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module) return false;
        const auto version=reinterpret_cast<decltype(&rds_dsp_version)>(GetProcAddress(module,"rds_dsp_version"));
        if(!version || version()!=1) return false;
        create=reinterpret_cast<decltype(create)>(GetProcAddress(module,"rds_dsp_create"));
        destroy=reinterpret_cast<decltype(destroy)>(GetProcAddress(module,"rds_dsp_destroy"));
        process=reinterpret_cast<decltype(process)>(GetProcAddress(module,"rds_dsp_process"));
        return create && destroy && process;
    }
};

RdsMpxDecoder::RdsMpxDecoder() : impl_(std::make_unique<Impl>()) {}
RdsMpxDecoder::~RdsMpxDecoder() = default;
int64_t RdsMpxDecoder::monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void RdsMpxDecoder::publish() { std::lock_guard lock(mutex_); published_=impl_->stats; }
RdsMpxSnapshot RdsMpxDecoder::snapshot() const { std::lock_guard lock(mutex_); return published_; }
void RdsMpxDecoder::reset() {
    if (!impl_->handle && impl_->stats.status=="Disabled") return;
    const auto resets=impl_->stats.resets;
    impl_->discard(); impl_->stats={}; impl_->stats.resets=resets;
    impl_->nextSample=0; publish();
}

bool RdsMpxDecoder::process(std::span<const float> samples,double rate,double targetHz,
    uint64_t epoch,uint64_t firstSample,bool discontinuity) {
    auto& s=*impl_;
    auto fail=[&](const char* status) {
        s.discard(); s.stats.station={}; s.stats.lastGroupMs=0; s.stats.lastGroupWords={};
        s.stats.status=status; publish(); return false;
    };
    if (!std::isfinite(rate) || rate<128000 || rate>384000 || !std::isfinite(targetHz))
        return fail("Unsupported MPX rate (128-384 kHz required)");
    if (samples.size()>262144 || firstSample>std::numeric_limits<uint64_t>::max()-samples.size() ||
        !std::all_of(samples.begin(),samples.end(),[](float x){return std::isfinite(x);}))
        return fail("Invalid MPX block");
    if (!s.load()) return fail("RDS DSP module unavailable or incompatible");
    if (!s.handle || discontinuity || epoch!=s.epoch || firstSample!=s.nextSample ||
        rate!=s.stats.sampleRate || targetHz!=s.stats.targetHz) {
        s.discard(); const auto resets=s.stats.resets+1; s.stats={}; s.stats.resets=resets;
        s.stats.sampleRate=rate; s.stats.targetHz=targetHz; s.epoch=epoch;
        s.handle=s.create(static_cast<float>(rate));
        if(!s.handle) return fail("RDS DSP initialisation failed");
    }
    std::array<uint8_t,1024> output{};
    for(size_t offset=0;offset<samples.size();offset+=8192) {
        const auto count=static_cast<uint32_t>(std::min<size_t>(8192,samples.size()-offset));
        uint32_t written=0;
        if(s.process(s.handle,samples.data()+offset,count,output.data(),static_cast<uint32_t>(output.size()),&written)!=0 || written>output.size())
            return fail("RDS DSP processing failed");
        s.stats.bits+=written;
        for(uint32_t i=0;i<written;++i) if(auto group=s.bits.pushBit(output[i]!=0)) {
            ++s.stats.groups; s.stats.correctedBlocks+=group->correctedBlocks;
            s.stats.lastGroupWords=group->words;
            s.stats.lastGroupMs=monotonicMs();
        }
    }
    s.nextSample=firstSample+samples.size(); s.stats.samples+=samples.size();
    s.stats.station=s.bits.station(); s.stats.rejectedGroups=s.bits.rejectedGroups();
    s.stats.status=s.stats.station.identified?"Receiving RDS":"Acquiring RDS";
    publish(); return true;
}
