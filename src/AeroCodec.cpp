#include "AeroCodec.h"
extern "C" {
#include "mbelib.h"
}
#include <algorithm>
#include <cstring>
#include <new>
#include <random>

struct SdrAeroCodec {
    mbe_parms current{}, previous{}, enhanced{};
    std::mt19937 noise{5489u};
};
// Preserve uniform synthesis noise, but keep replay/stream state independent of
// other decoders and libc rand(). Only this isolated codec library calls it.
static thread_local SdrAeroCodec* activeCodec=nullptr;
extern "C" float sdr_aero_random(void) {
    return float((*activeCodec).noise() >> 8)*(1.0f/16777216.0f);
}
SdrAeroCodec* sdr_aero_create() {
    auto* codec = new (std::nothrow) SdrAeroCodec;
    sdr_aero_reset(codec);
    return codec;
}
void sdr_aero_destroy(SdrAeroCodec* codec) { delete codec; }
void sdr_aero_reset(SdrAeroCodec* codec) {
    if (codec) {
        mbe_initMbeParms(&codec->current, &codec->previous, &codec->enhanced);
        codec->noise.seed(5489u);
    }
}
int sdr_aero_decode(SdrAeroCodec* codec, const uint8_t frame[12], int16_t pcm[160], char flags[64]) {
    if (!codec || !frame || !pcm || !flags) return -1;
    // MIT libaeroambe df7eebf: AeroAMBE::to_decode_slot, NOT the P25 interleaver.
    static constexpr unsigned char column[96] = {
        23,11,14,2,5,8,9,11,22,10,13,1,4,7,8,10,21,9,12,0,3,6,7,9,20,8,11,14,2,5,6,
        8,19,7,10,13,1,4,5,7,18,6,9,12,0,3,4,6,17,5,8,11,14,2,3,5,16,4,7,10,13,1,2,
        4,15,3,6,9,12,0,1,3,14,2,5,8,11,12,0,2,13,1,4,7,10,11,13,1,12,0,3,6,9,10,12,0};
    static constexpr unsigned char row[96] = {
        0,0,1,1,2,3,4,5,0,0,1,1,2,3,4,5,0,0,1,1,2,3,4,5,0,0,1,2,2,3,4,5,0,0,1,2,
        2,3,4,5,0,0,1,2,2,3,4,5,0,0,1,2,3,3,4,5,0,0,1,2,3,3,4,5,0,0,1,2,3,3,4,5,
        0,0,1,2,3,4,4,5,0,0,1,2,3,4,5,5,0,0,1,2,3,4,5,5};
    char matrix[6][24]{}, parameters[72]{}, errors[1024]{};
    for (int i=0; i<96; ++i) matrix[row[i]][column[i]] = (frame[i/8] >> (i%8)) & 1;
    int golay=0, total=0;
    activeCodec=codec;
    mbe_processAmbe4800x3600Frame(pcm, &golay, &total, errors, matrix, parameters,
        &codec->current, &codec->previous, &codec->enhanced, 1);
    activeCodec=nullptr;
    std::strncpy(flags, errors, 63); flags[63]=0;
    return total;
}
