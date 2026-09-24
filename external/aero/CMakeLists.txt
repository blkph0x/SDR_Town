# DEC-0121: pinned upstream receive core, with no P25 codec symbol interposition.
enable_language(C)
file(GLOB correct_sources CONFIGURE_DEPENDS correct/convolutional/*.c)
add_library(sdr_aero_correct STATIC ${correct_sources})
target_include_directories(sdr_aero_correct PUBLIC correct/include)
set_target_properties(sdr_aero_correct PROPERTIES AUTOMOC OFF AUTOUIC OFF)

add_library(sdr_aero_codec SHARED
    codec/ambe4800x3600.c codec/ecc.c codec/mbelib.c ../../src/AeroCodec.cpp)
target_include_directories(sdr_aero_codec PRIVATE codec ../../include)
target_compile_definitions(sdr_aero_codec PRIVATE SDR_AERO_CODEC_BUILD _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
set_target_properties(sdr_aero_codec PROPERTIES AUTOMOC OFF AUTOUIC OFF
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden WINDOWS_EXPORT_ALL_SYMBOLS OFF)
if(NOT WIN32)
    target_link_libraries(sdr_aero_codec PRIVATE m)
endif()

add_library(sdr_aero_receive STATIC
    jaero/aerol.cpp jaero/aerol.h jaero/jconvolutionalcodec.cpp jaero/jconvolutionalcodec.h
    jaero/oqpskdemodulator.cpp jaero/oqpskdemodulator.h
    jaero/mskdemodulator.cpp jaero/mskdemodulator.h
    jaero/burstmskdemodulator.cpp jaero/burstmskdemodulator.h
    jaero/burstoqpskdemodulator.cpp jaero/burstoqpskdemodulator.h jaero/fftrwrapper.cpp
    jaero/coarsefreqestimate.cpp jaero/coarsefreqestimate.h jaero/DSP.cpp jaero/DSP.h
    jaero/fftwrapper.cpp jfft/jfft.cpp)
target_include_directories(sdr_aero_receive PUBLIC jaero jfft)
target_link_libraries(sdr_aero_receive PUBLIC Qt6::Core sdr_aero_correct)
target_compile_definitions(sdr_aero_receive PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(sdr_aero_receive PRIVATE /utf-8)
endif()
install(TARGETS sdr_aero_codec RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES jaero/LICENSE DESTINATION licenses/aero/jaero)
install(FILES jfft/LICENSE DESTINATION licenses/aero/jfft)
install(FILES correct/LICENSE DESTINATION licenses/aero/correct)
install(FILES codec/COPYRIGHT codec/LICENSE-wrapper codec/README.md DESTINATION licenses/aero/codec)

add_library(sdr_inmarsat_native STATIC ../../src/InmarsatAero.cpp ../../src/InmarsatAdsc.cpp)
target_include_directories(sdr_inmarsat_native PUBLIC ../../include)
target_link_libraries(sdr_inmarsat_native PUBLIC sdr_aero_codec sdr_aero_receive nlohmann_json::nlohmann_json)
add_library(sdr_inmarsat_audio STATIC ../../src/InmarsatAudio.cpp)
target_include_directories(sdr_inmarsat_audio PUBLIC ../../include PRIVATE ../miniaudio)
target_link_libraries(sdr_inmarsat_audio PUBLIC Qt6::Core nlohmann_json::nlohmann_json)
add_executable(inmarsat_reference_probe EXCLUDE_FROM_ALL ../../tests/inmarsat_reference_probe.cpp)
target_link_libraries(inmarsat_reference_probe PRIVATE sdr_inmarsat_native)
add_library(sdr_inmarsat_map STATIC ../../src/InmarsatMapWidget.cpp ../../include/InmarsatMapWidget.h ../../resources/inmarsat_map.qrc)
target_include_directories(sdr_inmarsat_map PUBLIC ../../include)
target_link_libraries(sdr_inmarsat_map PUBLIC Qt6::Widgets nlohmann_json::nlohmann_json)
