#include "RdsDspAbi.h"
#include "src/dsp/subcarrier.hh"
#include <algorithm>
#include <cmath>

uint32_t rds_dsp_version() { return 1; }
void* rds_dsp_create(float rate) {
    if (!std::isfinite(rate) || rate < 128000 || rate > 384000) return nullptr;
    try { return new redsea::SubcarrierSet(rate); }
    catch (...) { return nullptr; }
}
void rds_dsp_destroy(void* handle) {
    delete static_cast<redsea::SubcarrierSet*>(handle);
}
int rds_dsp_process(void* handle, const float* samples, uint32_t count,
                    uint8_t* bits, uint32_t capacity, uint32_t* written) {
    if (written) *written = 0;
    if (!handle || !written || !bits || capacity < 1024 || count > 8192 || (!samples && count)) return 1;
    for (uint32_t i=0;i<count;++i) if (!std::isfinite(samples[i])) return 2;
    if (!count) return 0;
    try {
        redsea::MPXBuffer input;
        std::copy_n(samples,count,input.data.begin());
        input.used_size=count;
        const auto output=static_cast<redsea::SubcarrierSet*>(handle)->chunkToBits(input,1);
        if (output.bits[0].size()>capacity) return 3; // Host must reset on internal failure.
        for (const auto& bit:output.bits[0]) bits[(*written)++]=bit.value?1:0;
        return 0;
    } catch (...) { return 3; }
}
