// Loader-only fixture. Never implements radio access and is never packaged.
extern "C" __declspec(dllexport) int sdrplay_api_Open() { return 1; }
extern "C" __declspec(dllexport) int sdrplay_api_ApiVersion(float*) { return 1; }
extern "C" __declspec(dllexport) int sdrplay_api_GetDevices(void*, unsigned*, unsigned) { return 1; }
