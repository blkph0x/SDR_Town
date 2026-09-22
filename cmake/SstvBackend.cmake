# Rust SSTV/HamDRM image helper used by file decoding and live receiver sessions.
option(SDR_TOWN_ENABLE_SSTV_IMAGES "Build pinned SSTV/HamDRM image backend" OFF)
option(SDR_TOWN_REQUIRE_SSTV_IMAGES "Fail configuration unless the SSTV image backend is enabled" OFF)

if(SDR_TOWN_REQUIRE_SSTV_IMAGES AND NOT SDR_TOWN_ENABLE_SSTV_IMAGES)
    message(FATAL_ERROR "SDR_TOWN_REQUIRE_SSTV_IMAGES=ON requires SDR_TOWN_ENABLE_SSTV_IMAGES=ON")
endif()
if(NOT SDR_TOWN_ENABLE_SSTV_IMAGES)
    return()
endif()

find_program(SDR_TOWN_CARGO cargo HINTS "${CMAKE_BINARY_DIR}/toolchains/cargo/bin" REQUIRED)
set(SSTV_ENV "RUSTFLAGS=-C target-feature=+crt-static")
if(EXISTS "${CMAKE_BINARY_DIR}/toolchains/rustup")
    list(APPEND SSTV_ENV "CARGO_HOME=${CMAKE_BINARY_DIR}/toolchains/cargo"
                         "RUSTUP_HOME=${CMAKE_BINARY_DIR}/toolchains/rustup")
endif()

set(SSTV_EXE "${CMAKE_BINARY_DIR}/sstv-backend/release/sdrtown_sstv${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(OUTPUT "${SSTV_EXE}"
    COMMAND ${CMAKE_COMMAND} -E env ${SSTV_ENV} "${SDR_TOWN_CARGO}" build --release --locked
        --manifest-path "${CMAKE_SOURCE_DIR}/src/sstv_backend/Cargo.toml"
        --target-dir "${CMAKE_BINARY_DIR}/sstv-backend"
    DEPENDS src/sstv_backend/Cargo.toml src/sstv_backend/Cargo.lock src/sstv_backend/src/main.rs src/sstv_backend/src/pcm.rs
        src/sstv_backend/src/hamdrm.rs
        src/sstv_backend/vendor/sstv/src/modes/mod.rs src/sstv_backend/vendor/sstv/src/modes/extra.rs
        src/sstv_backend/vendor/sstv/src/modes/martin.rs src/sstv_backend/vendor/sstv/src/modes/scottie.rs
        src/sstv_backend/vendor/sstv/src/decoder/acquire.rs src/sstv_backend/vendor/sstv/tests/all_modes.rs
    VERBATIM)
add_custom_target(sstv_backend DEPENDS "${SSTV_EXE}")

if(BUILD_TESTS)
    target_compile_definitions(sdr_town_workspace_tests PRIVATE SDR_TOWN_TEST_SSTV_BACKEND=1)
    add_dependencies(sdr_town_workspace_tests sstv_backend)
    add_custom_command(TARGET sdr_town_workspace_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SSTV_EXE}" "$<TARGET_FILE_DIR:sdr_town_workspace_tests>"
        VERBATIM)
    add_test(NAME SstvTransportRust
        COMMAND ${CMAKE_COMMAND} -E env ${SSTV_ENV} "${SDR_TOWN_CARGO}" test --offline --locked
            --manifest-path "${CMAKE_SOURCE_DIR}/src/sstv_backend/Cargo.toml"
            --target-dir "${CMAKE_BINARY_DIR}/sstv-backend")
    set_tests_properties(SstvTransportRust PROPERTIES TIMEOUT 900)
    add_test(NAME SstvBackendSelftest COMMAND "${SSTV_EXE}" --selftest)
    set_tests_properties(SstvBackendSelftest PROPERTIES TIMEOUT 180 DEPENDS SstvTransportRust)
endif()

add_dependencies(SDR_Town sstv_backend)
add_custom_command(TARGET SDR_Town POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SSTV_EXE}" "$<TARGET_FILE_DIR:SDR_Town>"
    VERBATIM)
add_custom_command(TARGET deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SSTV_EXE}" "${DEPLOY_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/external/sstv-licenses"
        "${DEPLOY_STAGING}/licenses/sstv"
    VERBATIM)

# Keep cmake --install / CPack behaviour aligned with the custom portable
# deploy target.  The generated helper exists before SDR_Town is complete.
install(PROGRAMS "${SSTV_EXE}" DESTINATION bin)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/external/sstv-licenses/" DESTINATION bin/licenses/sstv)
