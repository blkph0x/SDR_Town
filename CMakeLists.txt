cmake_minimum_required(VERSION 3.25)

project(SDR_Town VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Nice output dirs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# vcpkg integration (if toolchain provided)
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Qt6 (user must have it or set CMAKE_PREFIX_PATH / Qt6_DIR)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Optional: spdlog & nlohmann via vcpkg / find
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# SoapySDR for device support (PR 2+). vcpkg "soapy" port or system install.
# After vcpkg install, it should be discoverable when using the vcpkg toolchain.
find_package(SoapySDR CONFIG QUIET)
if (NOT SoapySDR_FOUND)
  # Fallback: try pkg-config or manual
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(SOAPY SoapySDR)
  endif()
endif()
if (SoapySDR_FOUND OR SOAPY_FOUND)
  message(STATUS "SoapySDR found - device support enabled")
else()
  message(WARNING "SoapySDR not found. Device features (PR2+) will be limited until installed via vcpkg or https://github.com/pothosware/SoapySDR/wiki")
endif()

# Sources (will grow)
set(SDR_TOWN_SOURCES
    src/main.cpp
    src/DeviceManager.cpp
    src/SpectrumWidget.cpp
    src/AudioEngine.cpp
    src/Demod.cpp
)

# Resources (icons, qss, etc. later)
# qt_add_resources(SDR_TOWN_RESOURCES resources/resources.qrc)

add_executable(${PROJECT_NAME} WIN32
    ${SDR_TOWN_SOURCES}
    # ${SDR_TOWN_RESOURCES}
)

# Install rules (so cpack / make install knows what to package)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
# Deployed Qt DLLs, plugins, and other runtime files from the build dir will be picked up if you run windeployqt before packaging, or manually copy them into the install prefix.

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Link SoapySDR if available (for PR2 device layer)
# Note: vcpkg soapysdr port provides target named "SoapySDR" (not namespaced :: )
if (SoapySDR_FOUND)
  target_link_libraries(${PROJECT_NAME} PRIVATE SoapySDR)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SOAPYSDR)
elseif (SOAPY_FOUND)
  target_include_directories(${PROJECT_NAME} PRIVATE ${SOAPY_INCLUDE_DIRS})
  target_link_libraries(${PROJECT_NAME} PRIVATE ${SOAPY_LIBRARIES})
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SOAPYSDR)
endif()

# Include project headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio
)

# liquid-dsp is present as submodule (per original DESIGN) but currently disabled for MSVC
# header macro compatibility in this environment. The DSP below implements state-of-the-art
# quality using hand-crafted but high-grade algorithms (windowed-sinc FIR, proper rate math,
# AGC, high-quality resample, automatic bitrate calculations for SDR / internal / output).
# When liquid builds cleanly it can be swapped in for the absolute reference implementations.

# Windows specific
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        # /EHa allows catch(...) to catch SEH (access violations etc from native drivers like rtlsdr/libusb)
        # This helps turn hard crashes during RTL open into caught errors + stub fallback.
        # /utf-8 for Unicode string literals in UI (About box etc.)
        target_compile_options(${PROJECT_NAME} PRIVATE /EHa /utf-8)
    endif()
endif()

# Install rules (basic)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# For convenience in IDEs
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

message(STATUS "SDR_Town configured. Qt6 + spdlog + nlohmann-json + (optional SoapySDR for devices) expected.")

# --- Testing support (Catch2 for unit tests + CLI harness) ---
option(BUILD_TESTS "Build unit tests with Catch2" ON)
if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
  enable_testing()

  # Simple unit tests for core classes (no full GUI)
  # Link against the implementation sources for the tested classes
  add_executable(sdr_town_tests
    tests/test_devicemanager.cpp
    tests/test_audioengine.cpp
    tests/test_demod.cpp
    src/DeviceManager.cpp
    src/AudioEngine.cpp
    src/Demod.cpp
  )
  # Real Demodulator (full body, member state) is now always linked.
  target_link_libraries(sdr_town_tests PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    spdlog::spdlog
    nlohmann_json::nlohmann_json
  )
  if(SoapySDR_FOUND)
    target_link_libraries(sdr_town_tests PRIVATE SoapySDR)
    target_compile_definitions(sdr_town_tests PRIVATE HAVE_SOAPYSDR)
  endif()
  target_include_directories(sdr_town_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio
  )
  if(MSVC)
    target_compile_options(sdr_town_tests PRIVATE /EHa /utf-8)
  endif()

  add_test(NAME UnitTests COMMAND sdr_town_tests)

  # CLI black-box tests via PowerShell harness (run manually or in CI)
  message(STATUS "Tests enabled. Run 'ctest' for C++ units. Use tests/run_cli_tests.ps1 for end-to-end CLI tests.")
endif()

# --- Proper deployment + self-contained installer (NSIS) ---
# Exact sequence for a complete installer that actually contains all the DLLs and plugins:
#   1. cmake --build build --config Release
#   2. cd build/bin/Release
#   3. C:/Qt/6.11.1/msvc2022_64/bin/windeployqt.exe SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler
#   4. cd ../../..   (back to project root)
#   5. cpack -G NSIS -C Release
#
# This produces a real ~30-60 MB installer with everything (Qt plugins, Soapy, rtlsdr, pthreads, etc.).

# The key line that makes the NSIS package actually contain the full deployed tree:
# CPack will take the entire contents of the windeployqt'ed Release folder and put it at the root of the installer.
set(CPACK_INSTALLED_DIRECTORIES "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release;.")

# Fallback / extra safety: also install the directory the normal way (helps with some generators)
if(WIN32)
  install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/
          DESTINATION .
          FILES_MATCHING
            PATTERN "*.exe" PATTERN "*.dll" PATTERN "*.qm"
            PATTERN "platforms/*" PATTERN "imageformats/*" PATTERN "tls/*"
            PATTERN "styles/*" PATTERN "iconengines/*" PATTERN "generic/*"
            PATTERN "networkinformation/*" PATTERN "translations/*"
  )
endif()

# --- Installer packaging with CPack (NSIS for Windows) ---
include(CPack)
set(CPACK_PACKAGE_NAME "SDR_Town")
set(CPACK_PACKAGE_VENDOR "SDR Town Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SDR Town - Professional multi-SDR monitoring, scanning, and analysis tool")
set(CPACK_PACKAGE_VERSION_MAJOR 0)
set(CPACK_PACKAGE_VERSION_MINOR 1)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SDR Town")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt") # create a simple LICENSE if missing

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_NSIS_DISPLAY_NAME "SDR Town")
  set(CPACK_NSIS_PACKAGE_NAME "SDR Town")
  set(CPACK_NSIS_CONTACT "https://github.com/Blkph0x/SDR_Town")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico") # optional, harmless if missing
  set(CPACK_NSIS_MODIFY_PATH ON)
endif()