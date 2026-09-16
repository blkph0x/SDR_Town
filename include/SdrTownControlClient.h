#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
  #ifdef SDRTOWN_CONTROL_BUILD
    #define SDRTOWN_CONTROL_API extern "C" __declspec(dllexport)
  #else
    #define SDRTOWN_CONTROL_API extern "C" __declspec(dllimport)
  #endif
#else
  #define SDRTOWN_CONTROL_API extern "C"
#endif

enum SdrTownControlResult {
    SDRTOWN_CONTROL_OK = 0,
    SDRTOWN_CONTROL_BAD_ARGUMENT = -1,
    SDRTOWN_CONTROL_SOCKET_ERROR = -2,
    SDRTOWN_CONTROL_HTTP_ERROR = -3,
    SDRTOWN_CONTROL_PROTOCOL_ERROR = -4
};

struct SdrTownControlConfig {
    const char* host;      // null/empty = 127.0.0.1
    uint16_t port;         // 0 = 8765
    const char* token;     // optional bearer token
    uint32_t timeoutMs;    // 0 = 2500 ms
};

struct SdrTownTuneRequest {
    double frequencyHz;       // required
    const char* mode;         // AUTO, NFM, WFM, AM, USB, LSB, CW, or P25
    double bandwidthHz;       // 0 = leave/default
    double lpfHz;             // 0 = leave/default
    int audioLpfEnabled;      // -1 = leave, 0 = off, 1 = on
    double rfGainDb;          // NaN or negative = leave
    double squelchDb;         // NaN = leave
    int startDevice;          // 0/1
    int p25AutoFollow;        // 0/1, only meaningful for P25
};

SDRTOWN_CONTROL_API const char* SdrTownControl_Version(void);

SDRTOWN_CONTROL_API int SdrTownControl_Health(const SdrTownControlConfig* config,
                                              char* responseJson,
                                              size_t responseJsonBytes);

SDRTOWN_CONTROL_API int SdrTownControl_Status(const SdrTownControlConfig* config,
                                              char* responseJson,
                                              size_t responseJsonBytes);

SDRTOWN_CONTROL_API int SdrTownControl_Tune(const SdrTownControlConfig* config,
                                            const SdrTownTuneRequest* request,
                                            char* responseJson,
                                            size_t responseJsonBytes);

SDRTOWN_CONTROL_API int SdrTownControl_SetMode(const SdrTownControlConfig* config,
                                               const char* mode,
                                               char* responseJson,
                                               size_t responseJsonBytes);

SDRTOWN_CONTROL_API int SdrTownControl_SetRfGain(const SdrTownControlConfig* config,
                                                 double rfGainDb,
                                                 char* responseJson,
                                                 size_t responseJsonBytes);

SDRTOWN_CONTROL_API int SdrTownControl_SetVolume(const SdrTownControlConfig* config,
                                                 double volume,
                                                 char* responseJson,
                                                 size_t responseJsonBytes);

SDRTOWN_CONTROL_API int SdrTownControl_StartP25Control(const SdrTownControlConfig* config,
                                                       double controlFrequencyHz,
                                                       int autoFollow,
                                                       char* responseJson,
                                                       size_t responseJsonBytes);
