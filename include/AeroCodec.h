#pragma once
#include <stdint.h>
#if defined(_WIN32)
# if defined(SDR_AERO_CODEC_BUILD)
#  define SDR_AERO_API __declspec(dllexport)
# else
#  define SDR_AERO_API __declspec(dllimport)
# endif
#else
# define SDR_AERO_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SdrAeroCodec SdrAeroCodec;
SDR_AERO_API SdrAeroCodec* sdr_aero_create(void);
SDR_AERO_API void sdr_aero_destroy(SdrAeroCodec* codec);
SDR_AERO_API void sdr_aero_reset(SdrAeroCodec* codec);
// One mini-m codeword, LSB first within each byte. Exactly 160 samples at 8 kHz.
// Return corrected-bit count, or -1 on invalid arguments; flags E/T/R/M are codec status.
SDR_AERO_API int sdr_aero_decode(SdrAeroCodec* codec, const uint8_t frame[12],
                               int16_t pcm[160], char flags[64]);
#ifdef __cplusplus
}
#endif
