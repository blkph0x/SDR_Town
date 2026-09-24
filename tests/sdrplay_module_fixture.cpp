#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Modules.hpp>

#ifndef SDRPLAY_EMPTY_FIXTURE
static SoapySDR::KwargsList find(const SoapySDR::Kwargs&) { return {}; }
static SoapySDR::Device* make(const SoapySDR::Kwargs&) { return nullptr; }
#ifdef SDRPLAY_BAD_ABI_FIXTURE
static SoapySDR::Registry registry("sdrplay", &find, &make, "deliberately-invalid-test-abi");
#else
static SoapySDR::Registry registry("sdrplay", &find, &make, SOAPY_SDR_ABI_VERSION);
#endif
#endif
static SoapySDR::ModuleVersion version("loader-test-only");
