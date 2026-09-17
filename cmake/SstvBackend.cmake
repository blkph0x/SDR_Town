# Offline-only helper; core/native-only builds can omit the Rust toolchain.
option(SDR_TOWN_ENABLE_SSTV_IMAGES "Build pinned offline Robot36/Martin1 image backend" OFF)
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
    DEPENDS src/sstv_backend/Cargo.toml src/sstv_backend/Cargo.lock src/sstv_backend/src/main.rs
    VERBATIM)
add_custom_target(sstv_backend DEPENDS "${SSTV_EXE}")
add_dependencies(SDR_Town sstv_backend)
add_custom_command(TARGET SDR_Town POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SSTV_EXE}" "$<TARGET_FILE_DIR:SDR_Town>"
    VERBATIM)
add_custom_command(TARGET deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SSTV_EXE}" "${DEPLOY_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/external/sstv-licenses"
        "${DEPLOY_STAGING}/licenses/sstv"
    VERBATIM)
