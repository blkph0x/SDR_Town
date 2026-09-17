#pragma once
#include <stdint.h>
#if defined(_WIN32) && defined(SDRTOWN_RDS_DSP_BUILD)
#define RDS_EXPORT __declspec(dllexport)
#else
#define RDS_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif
// ABI v1. No allocations or C++ types cross the DLL boundary. A handle has a
// single owner/thread. Input <=8192 finite float samples, rate 128..384 kHz.
// Caller provides >=1024 bytes of output capacity; invalid calls consume nothing.
RDS_EXPORT uint32_t rds_dsp_version(void);
RDS_EXPORT void* rds_dsp_create(float rate);
RDS_EXPORT void rds_dsp_destroy(void* handle);
RDS_EXPORT int rds_dsp_process(void* handle, const float* samples, uint32_t count,
                              uint8_t* bits, uint32_t capacity, uint32_t* written);
#ifdef __cplusplus
}
#endif
