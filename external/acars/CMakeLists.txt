# DEC-0162: pinned application decoders; no RF/DSP or codec changes.
find_package(ZLIB REQUIRED)
find_package(jansson CONFIG REQUIRED)
include(TestBigEndian)
test_big_endian(IS_BIG_ENDIAN)
configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
add_library(sdr_miam STATIC
    decoder_status.c
    libacars/miam-core.c libacars/libacars.c libacars/util.c
    libacars/vstring.c libacars/json.c libacars/dict.c libacars/crc.c
    libacars/configuration.c libacars/hash.c libacars/list.c
    libacars/acars.c libacars/adsc.c libacars/arinc.c libacars/cpdlc.c
    libacars/asn1-format-common.c libacars/asn1-format-cpdlc-text.c
    libacars/asn1-format-cpdlc-json.c libacars/asn1-util.c libacars/bitstream.c
    libacars/media-adv.c libacars/miam.c libacars/ohma.c libacars/reassembly.c)
# ASN.1 files are the unmodified generated upstream CPDLC schema/runtime.
file(GLOB ASN_SOURCES CONFIGURE_DEPENDS libacars/asn1/*.c)
target_sources(sdr_miam PRIVATE ${ASN_SOURCES})
set_target_properties(sdr_miam PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
    AUTOMOC OFF AUTOUIC OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(sdr_miam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libacars/asn1)
target_link_libraries(sdr_miam PUBLIC ZLIB::ZLIB jansson::jansson)
if(MSVC)
    target_compile_definitions(sdr_miam PRIVATE _CRT_SECURE_NO_WARNINGS strdup=_strdup LFIND_NMEMB_SIZE_UINT)
else()
    target_compile_definitions(sdr_miam PRIVATE _POSIX_C_SOURCE=200809L LFIND_NMEMB_SIZE_SIZE_T)
endif()
install(FILES LICENSE.md ZLIB-LICENSE.txt JANSSON-LICENSE.txt UPSTREAM-README.md README.sdr-town.md DESTINATION licenses/acars)
