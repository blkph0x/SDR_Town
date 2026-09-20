cmake_minimum_required(VERSION 3.25)

project(SDR_Town VERSION 0.2.76 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Optional: spdlog & nlohmann via vcpkg / find
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)

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
set(SDR_TOWN_DSP_SOURCES
    src/dsp/P25FilterCache.cpp
    src/dsp/DspKernels.cpp
    src/dsp/P25StreamingChannelDdc.cpp
    src/dsp/P25CqpskStagedScorer.cpp
    src/dsp/P25Phase2Framer.cpp
    src/dsp/P25DemodStateMachine.cpp
)

add_library(sdr_town_dsp STATIC ${SDR_TOWN_DSP_SOURCES})
target_include_directories(sdr_town_dsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MSVC)
    target_compile_options(sdr_town_dsp PRIVATE /O2 /EHsc /utf-8)
endif()
include(CheckIPOSupported)
check_ipo_supported(RESULT SDR_TOWN_IPO_SUPPORTED)
if(SDR_TOWN_IPO_SUPPORTED)
    set_property(TARGET sdr_town_dsp PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

add_library(sdr_town_rds STATIC
    src/RdsDecoder.cpp
    src/RdsMpxDecoder.cpp
    external/redsea-block/src/block_sync.cc
    external/redsea-block/src/group.cc
)
target_include_directories(sdr_town_rds PUBLIC include PRIVATE external/redsea-block external/miniaudio)
add_library(sdr_town_tones STATIC src/CtcssDecoder.cpp src/DcsDecoder.cpp src/DtmfDecoder.cpp src/ControlEventLog.cpp)
target_include_directories(sdr_town_tones PUBLIC include PRIVATE external/miniaudio)
add_library(sdr_town_decoders STATIC src/ReceiveDecoder.cpp src/RdsMpxFile.cpp src/SstvVis.cpp src/SstvAudioFile.cpp src/SstvLiveInput.cpp src/SstvRateConverter.cpp src/SstvReceiverFeed.cpp)
target_include_directories(sdr_town_decoders PUBLIC include PRIVATE external/miniaudio)
target_link_libraries(sdr_town_decoders PUBLIC sdr_town_rds sdr_town_tones)
target_link_libraries(sdr_town_decoders PRIVATE nlohmann_json::nlohmann_json)
include(cmake/RdsBackend.cmake)

set(SDR_TOWN_SOURCES
    src/P25Aliases.cpp
    src/P25AliasCsv.cpp
    src/P25AliasDialog.cpp
    src/SstvImageFile.cpp
    src/SstvStreamWorker.cpp
    src/SstvLiveSession.cpp
    src/SstvProgress.cpp
    src/SstvWindow.cpp
    include/SstvWindow.h
    src/main.cpp
    src/P25VoiceTiming.cpp
    include/P25VoiceTiming.h
    src/P25TalkgroupRegistry.cpp
    include/P25TalkgroupRegistry.h
    src/P25AppGlobals.cpp
    include/P25AppGlobals.h
    src/P25RollingIq.cpp
    include/P25RollingIq.h
    src/P25VoiceDecode.cpp
    src/P25AudioResampler.cpp
    include/P25VoiceDecode.h
    src/P25VoiceTest.cpp
    include/P25VoiceTest.h
    src/P25VoiceSession.cpp
    include/P25VoiceSession.h
    src/P25DecodeConfig.cpp
    include/P25DecodeConfig.h
    src/DemodModeUtils.cpp
    src/BandPlan.cpp
    src/BandPlanDialog.cpp
    include/DemodModeUtils.h
    src/SavedFrequencies.cpp
    include/SavedFrequencies.h
    src/CliApp.cpp
    include/CliApp.h
    src/AppBootstrap.cpp
    include/AppBootstrap.h
    src/MainWindow.cpp
    src/WorkspaceLayout.cpp
    src/MainWindowP25Voice.cpp
    src/MainWindowP25Orchestration.cpp
    include/MainWindow.h
    src/DeviceManager.cpp
    src/SdrplayProfile.cpp
    include/SdrplayProfile.h
    src/SdrplayDiversity.cpp
    include/SdrplayDiversity.h
    src/SatcomAsyncLog.cpp
    include/SatcomAsyncLog.h
    src/SatcomScannerEngine.cpp
    include/SatcomScannerEngine.h
    src/Ax25AprsDecoder.cpp
    include/Ax25AprsDecoder.h
    src/AptImageDecoder.cpp
    include/AptImageDecoder.h
    src/SatcomScannerWidget.cpp
    include/SatcomScannerWidget.h
    src/SatObserverConfig.cpp
    include/SatObserverConfig.h
    src/SatCatalogue.cpp
    include/SatCatalogue.h
    src/TleStore.cpp
    include/TleStore.h
    src/Sgp4.cpp
    include/Sgp4.h
    src/SatPassPlanner.cpp
    include/SatPassPlanner.h
    src/SatCatalogueDialog.cpp
    include/SatCatalogueDialog.h
    src/ModeS.cpp
    include/ModeS.h
    src/AdsBTrackStore.cpp
    include/AdsBTrackStore.h
    src/AircraftMapWidget.cpp
    include/AircraftMapWidget.h
    src/InmarsatBandPlan.cpp
    include/InmarsatBandPlan.h
    src/InmarsatMessageStore.cpp
    include/InmarsatMessageStore.h
    src/InmarsatDemod.cpp
    include/InmarsatDemod.h
    src/InmarsatAcars.cpp
    include/InmarsatAcars.h
    src/InmarsatVoice.cpp
    include/InmarsatVoice.h
    src/InmarsatEngine.cpp
    include/InmarsatEngine.h
    src/InmarsatWidget.cpp
    include/InmarsatWidget.h
    src/SatcomHubWidget.cpp
    include/SatcomHubWidget.h
    src/ObserverMapWidget.cpp
    include/ObserverMapWidget.h
    src/HttpGet.cpp
    include/HttpGet.h
    src/SpectrumWidget.cpp
    src/RdsStatusWidget.cpp
    include/SpectrumWidget.h
    src/AudioEngine.cpp
    src/Demod.cpp
    src/P25Control.cpp
    src/P25LiveDecoder.cpp
    src/P25FollowStateMachine.cpp
    src/P25AudioDropClass.cpp
    src/P25TxSession.cpp
    include/P25TxSession.h
    include/P25TxConfig.h
    src/SignalClassifier.cpp
    src/ClassifierModelBackend.cpp
    src/P25TrafficChannelProcessor.cpp
    include/P25TrafficChannelProcessor.h
    src/Receiver.cpp
    src/SdrTownControlServer.cpp
    include/SdrTownControlServer.h
    src/RemoteDiagnostics.cpp
    include/RemoteDiagnostics.h
    src/UpdateManager.cpp   # professional GitHub release + safe in-app updater
    src/UpdateManifestSignature.cpp
    include/UpdateManager.h
    include/UpdateManifestSignature.h
    # Decode Log / voice-to-text (modular transcript feeds)
    src/TranscriptHub.cpp
    include/TranscriptHub.h
    src/SttEngine.cpp
    include/SttEngine.h
    src/P25TranscriptSource.cpp
    include/P25TranscriptSource.h
    src/TranscriptWindow.cpp
    include/TranscriptWindow.h
    include/TranscriptTypes.h
    include/ITranscriptSource.h
    src/AudioCapture.cpp
    include/AudioCapture.h
    src/P25AmbeEncoder.cpp
    include/IP25AmbeEncoder.h
    src/P25Phase2TxFramer.cpp
    include/P25Phase2TxFramer.h
)

# Resources (icons, qss, etc. later)
# qt_add_resources(SDR_TOWN_RESOURCES resources/resources.qrc)

add_executable(${PROJECT_NAME} WIN32
    ${SDR_TOWN_SOURCES}
    # ${SDR_TOWN_RESOURCES}
)

if(SDR_TOWN_BUILD_RDS_DSP)
    add_dependencies(${PROJECT_NAME} rds_runtime)
endif()

add_library(sdrtown_control_client SHARED
    src/SdrTownControlClient.cpp
    include/SdrTownControlClient.h
)
target_include_directories(sdrtown_control_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(sdrtown_control_client PRIVATE SDRTOWN_CONTROL_BUILD)
set_target_properties(sdrtown_control_client PROPERTIES OUTPUT_NAME "SdrTownControl")
if(WIN32)
    target_compile_definitions(sdrtown_control_client PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(sdrtown_control_client PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(sdrtown_control_client PRIVATE /EHsc /utf-8)
endif()

# Install rules (so cpack / make install knows what to package)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(TARGETS sdrtown_control_client
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)
# Deployed Qt DLLs, plugins, and other runtime files from the build dir will be picked up if you run windeployqt before packaging, or manually copy them into the install prefix.

target_link_libraries(${PROJECT_NAME} PRIVATE
    sdr_town_decoders
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    winhttp
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    unofficial-sodium::sodium
    sdr_town_dsp
)
target_compile_definitions(${PROJECT_NAME} PRIVATE SDR_TOWN_VERSION=\"${PROJECT_VERSION}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE SDR_TOWN_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/inmarsat DESTINATION bin/data)

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

# liquid-dsp is isolated in the MinGW RDS runtime; legacy MSVC DSP is unchanged.
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
    target_link_libraries(${PROJECT_NAME} PRIVATE Psapi)
    if(MSVC)
        # /EHa allows catch(...) to catch SEH (access violations etc from native drivers like rtlsdr/libusb)
        # This helps turn hard crashes during RTL open into caught errors + stub fallback.
        # /utf-8 for Unicode string literals in UI (About box etc.)
        target_compile_options(${PROJECT_NAME} PRIVATE /EHa /utf-8 /bigobj)
    endif()
endif()

# Install rules (basic)
if(WIN32 AND (SoapySDR_FOUND OR SOAPY_FOUND))
    # DEC-0089: do not silently reuse an arbitrary old RTL DLL in the output tree.
    find_package(rtlsdr CONFIG REQUIRED)
    get_target_property(SDR_TOWN_RTL_TARGET_TYPE rtlsdr::rtlsdr TYPE)
    if(NOT SDR_TOWN_RTL_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Soapy RTL runtime deployment requires a shared rtlsdr target")
    endif()
    add_custom_target(stage_rtl_runtime
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rtlsdr::rtlsdr>" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/rtlsdr.dll"
        COMMENT "Staging configured RTL-SDR runtime"
        VERBATIM)
    add_dependencies(SDR_Town stage_rtl_runtime)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# For convenience in IDEs
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

message(STATUS "SDR_Town configured. Qt6 + spdlog + nlohmann-json + (optional SoapySDR for devices) expected.")

# --- Testing support (Catch2 for unit tests + CLI harness) ---
option(BUILD_TESTS "Build unit tests with Catch2" ON)
option(SDR_TOWN_ENABLE_ONNX "Enable optional ONNX Runtime classifier backend hook" OFF)
option(SDR_TOWN_ENABLE_MBELIB "Enable optional mbelib backend for clear P25 Phase 1 IMBE and Phase 2 AMBE voice frames" ON)

if(SDR_TOWN_ENABLE_ONNX)
  find_package(onnxruntime CONFIG QUIET)
  if(onnxruntime_FOUND)
    message(STATUS "ONNX Runtime found - classifier backend hook enabled")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_ONNXRUNTIME)
  else()
    message(WARNING "SDR_TOWN_ENABLE_ONNX is ON, but onnxruntime was not found. Deterministic classifier remains active.")
  endif()
endif()

if(SDR_TOWN_ENABLE_MBELIB)
  set(SDR_TOWN_BUNDLED_MBELIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/mbelib")
  if(EXISTS "${SDR_TOWN_BUNDLED_MBELIB_DIR}/mbelib.h" AND EXISTS "${SDR_TOWN_BUNDLED_MBELIB_DIR}/CMakeLists.txt")
    set(DISABLE_TEST ON CACHE BOOL "Disable bundled mbelib test framework" FORCE)
    add_subdirectory("${SDR_TOWN_BUNDLED_MBELIB_DIR}" "${CMAKE_BINARY_DIR}/external/mbelib" EXCLUDE_FROM_ALL)
    message(STATUS "Bundled mbelib found - P25 IMBE/AMBE voice backend enabled")
    target_include_directories(${PROJECT_NAME} PRIVATE "${SDR_TOWN_BUNDLED_MBELIB_DIR}")
    target_link_libraries(${PROJECT_NAME} PRIVATE mbe-static)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_MBELIB)
  else()
    find_path(MBELIB_INCLUDE_DIR NAMES mbelib.h)
    find_library(MBELIB_LIBRARY NAMES mbe mbelib libmbe)
    if(MBELIB_INCLUDE_DIR AND MBELIB_LIBRARY)
      message(STATUS "System mbelib found - P25 IMBE/AMBE voice backend enabled")
      target_include_directories(${PROJECT_NAME} PRIVATE ${MBELIB_INCLUDE_DIR})
      target_link_libraries(${PROJECT_NAME} PRIVATE ${MBELIB_LIBRARY})
      target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_MBELIB)
    else()
      message(WARNING "SDR_TOWN_ENABLE_MBELIB is ON, but mbelib was not found. P25 voice backend remains unavailable.")
    endif()
  endif()
endif()

if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
  enable_testing()

  # Simple unit tests for core classes (no full GUI)
  # Link against the implementation sources for the tested classes
  add_executable(sdr_town_tests
    tests/test_rds.cpp
    tests/test_ctcss.cpp
    tests/test_dtmf.cpp
    tests/test_dcs.cpp
    tests/test_receive_decoder.cpp
    tests/test_sstv_vis.cpp
    tests/test_sstv_live_input.cpp
    tests/test_sstv_rate_converter.cpp
    tests/test_devicemanager.cpp
    tests/test_sdrplay_profile.cpp
    tests/test_sdrplay_diversity.cpp
    tests/test_audioengine.cpp
    tests/test_p25_audio_resampler.cpp
    src/P25AudioResampler.cpp
    tests/test_bandplan.cpp
    src/BandPlan.cpp
    tests/test_audiocapture.cpp
    tests/test_demod.cpp
    tests/test_p25control.cpp
    tests/test_p25live.cpp
    tests/test_p25follow.cpp
    tests/test_p25_audio_drop_class.cpp
    tests/test_p25_sdrtrunk_tune.cpp
    tests/test_p25tx.cpp
    tests/test_p25tx_encode.cpp
    tests/test_p25traffic_processor.cpp
    tests/test_p25dsp.cpp
    tests/test_satcom.cpp
    tests/test_satcom_passes.cpp
    tests/test_adsb.cpp
    tests/test_inmarsat.cpp
    src/Ax25AprsDecoder.cpp
    src/AptImageDecoder.cpp
    src/SatcomAsyncLog.cpp
    src/SatObserverConfig.cpp
    src/SatCatalogue.cpp
    src/TleStore.cpp
    src/HttpGet.cpp
    src/Sgp4.cpp
    src/SatPassPlanner.cpp
    src/ModeS.cpp
    src/AdsBTrackStore.cpp
    src/InmarsatBandPlan.cpp
    src/InmarsatMessageStore.cpp
    src/InmarsatDemod.cpp
    src/InmarsatAcars.cpp
    src/InmarsatVoice.cpp
    src/InmarsatEngine.cpp
    src/DeviceManager.cpp
    src/SdrplayProfile.cpp
    src/SdrplayDiversity.cpp
    src/AudioEngine.cpp
    src/AudioCapture.cpp
    src/Demod.cpp
    src/P25Control.cpp
    src/P25LiveDecoder.cpp
    src/P25FollowStateMachine.cpp
    src/P25AudioDropClass.cpp
    src/P25TxSession.cpp
    src/P25AmbeEncoder.cpp
    src/P25Phase2TxFramer.cpp
    src/P25TrafficChannelProcessor.cpp
    src/SignalClassifier.cpp
    src/ClassifierModelBackend.cpp
    src/Receiver.cpp
  )
  # Real Demodulator (full body, member state) is now always linked.
  target_link_libraries(sdr_town_tests PRIVATE
    sdr_town_decoders
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Network
    winhttp
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    sdr_town_dsp
  )
  target_compile_definitions(sdr_town_tests PRIVATE SDR_TOWN_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")
  if(SDR_TOWN_BUILD_RDS_DSP)
    add_dependencies(sdr_town_tests rds_runtime)
    target_compile_definitions(sdr_town_tests PRIVATE SDR_TOWN_TEST_RDS_DSP=1
        SDR_TOWN_RDS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rds-mpx-yksi.flac")
  endif()
  if(SoapySDR_FOUND)
    target_link_libraries(sdr_town_tests PRIVATE SoapySDR)
    target_compile_definitions(sdr_town_tests PRIVATE HAVE_SOAPYSDR)
  endif()
  if(SDR_TOWN_ENABLE_MBELIB AND TARGET mbe-static)
    target_include_directories(sdr_town_tests PRIVATE "${SDR_TOWN_BUNDLED_MBELIB_DIR}")
    target_link_libraries(sdr_town_tests PRIVATE mbe-static)
    target_compile_definitions(sdr_town_tests PRIVATE HAVE_MBELIB)
  elseif(SDR_TOWN_ENABLE_MBELIB AND MBELIB_INCLUDE_DIR AND MBELIB_LIBRARY)
    target_include_directories(sdr_town_tests PRIVATE ${MBELIB_INCLUDE_DIR})
    target_link_libraries(sdr_town_tests PRIVATE ${MBELIB_LIBRARY})
    target_compile_definitions(sdr_town_tests PRIVATE HAVE_MBELIB)
  endif()
  target_include_directories(sdr_town_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio
  )
  if(MSVC)
    target_compile_options(sdr_town_tests PRIVATE /EHa /utf-8)
  endif()

  add_test(NAME UnitTests COMMAND sdr_town_tests)

  add_executable(sdr_town_workspace_tests tests/test_workspace.cpp src/WorkspaceLayout.cpp
    tests/test_p25_aliases.cpp src/P25Aliases.cpp src/P25AliasCsv.cpp src/P25AliasDialog.cpp
    tests/test_sstv_gui.cpp tests/sstv_miniaudio.cpp src/SstvWindow.cpp include/SstvWindow.h src/SstvImageFile.cpp
    tests/test_sstv_progress.cpp src/SstvProgress.cpp
    tests/test_sstv_stream_worker.cpp src/SstvStreamWorker.cpp src/SstvRateConverter.cpp src/SstvLiveInput.cpp
    src/SstvReceiverFeed.cpp src/SstvLiveSession.cpp
    tests/test_sstv_live_gui.cpp
    src/BandPlan.cpp src/BandPlanDialog.cpp src/SpectrumWidget.cpp include/SpectrumWidget.h src/RdsStatusWidget.cpp)
  target_include_directories(sdr_town_workspace_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_include_directories(sdr_town_workspace_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio)
  target_link_libraries(sdr_town_workspace_tests PRIVATE Catch2::Catch2 Qt6::Widgets nlohmann_json::nlohmann_json sdr_town_tones)
  add_test(NAME WorkspaceTests COMMAND sdr_town_workspace_tests)

  # CLI black-box tests via PowerShell harness (run manually or in CI)
  message(STATUS "Tests enabled. Run 'ctest' for C++ units. Use tests/run_cli_tests.ps1 for end-to-end CLI tests.")
endif()

# --- Proper deployment + self-contained installer (NSIS) + GitHub Release pipeline (state-of-the-art, no compromise) ---
# MUST use clean deploy_staging (Big Caveat from audit). Never package the raw build Release tree for public releases.
# 1. cmake --build build --config Release --target deploy
# 2. cd build/bin/Release ; windeployqt SDR_Town.exe --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw --no-system-d3d-compiler
# 3. cd ../../.. ; cmake --build build --config Release --target deploy   # re-sync after windeployqt (purges stales again)
# 4. cpack -G NSIS -C Release
#    (optionally) cpack -G ZIP -C Release   # for the portable zip
#
# Produces (in build/):
#   SDR_Town-${PROJECT_VERSION}-win64-setup.exe
#   SDR_Town-${PROJECT_VERSION}-win64.zip (portable)
#
# GitHub Release v${PROJECT_VERSION} assets (exact names for update.json + downloader):
#   SDR_Town-${PROJECT_VERSION}-win64-setup.exe
#   SDR_Town-${PROJECT_VERSION}-win64-setup.exe.sha256
#   update.json   (see recommended structure in user query + DESIGN.md)
#   SHA256SUMS.txt
#
# The deploy target + GLOB purge before windeployqt staging + before CPack + conditional real icon guarantees clean product.
# NSIS supports silent: SDR_Town-...-setup.exe /S
# After install/upgrade, %APPDATA%\SDR_Town (settings) is preserved by NSIS defaults + our AppData code.

# === Clean Release Packaging (state-of-the-art, audit-driven) ===
# We never package the raw Release folder blindly. We produce a clean staging tree
# that contains *only* the current SDR_Town bits + the windeployqt artifacts.
# This eliminates all stale MaulAudioPro.exe, maulaudio_tests.exe, sdr_town_tests.exe etc.

set(DEPLOY_STAGING "${CMAKE_BINARY_DIR}/deploy_staging")
set(DEPLOY_CLEAN_SCRIPT "${CMAKE_BINARY_DIR}/CleanDeployStaging.cmake")
file(WRITE "${DEPLOY_CLEAN_SCRIPT}" "
file(GLOB_RECURSE STALE_DEPLOY_BINARIES
     \"${DEPLOY_STAGING}/Maul*.exe\"
     \"${DEPLOY_STAGING}/*Maul*.exe\"
     \"${DEPLOY_STAGING}/*test*.exe\"
     \"${DEPLOY_STAGING}/sdr_town_tests.exe\"
     \"${DEPLOY_STAGING}/maulaudio_tests.exe\")
if(STALE_DEPLOY_BINARIES)
  file(REMOVE \${STALE_DEPLOY_BINARIES})
endif()
")

# Helper target: "deploy" — creates a pristine staging folder the user (and CI) can package from.
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${DEPLOY_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_STAGING}/tools/diagnostics"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_STAGING}/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/external/redsea-block"
            "${DEPLOY_STAGING}/licenses/redsea-block"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/external/liquid-dsp/LICENSE"
            "${DEPLOY_STAGING}/licenses/liquid-dsp-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/SDR_Town.exe" "${DEPLOY_STAGING}/"
    COMMAND ${CMAKE_COMMAND}
            "-DSOURCE=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release"
            "-DDESTINATION=${DEPLOY_STAGING}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageRuntime.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/remote_diag_server.py"
            "${DEPLOY_STAGING}/tools/diagnostics/remote_diag_server.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/start_remote_diag_server.ps1"
            "${DEPLOY_STAGING}/scripts/start_remote_diag_server.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install_remote_diag_task.ps1"
            "${DEPLOY_STAGING}/scripts/install_remote_diag_task.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data/inmarsat"
            "${DEPLOY_STAGING}/data/inmarsat"
    COMMAND ${CMAKE_COMMAND} -E remove -f
            "${DEPLOY_STAGING}/MaulAudioPro.exe"
            "${DEPLOY_STAGING}/maulaudio_tests.exe"
            "${DEPLOY_STAGING}/sdr_town_tests.exe"
            "${DEPLOY_STAGING}/Maul*.exe"
            "${DEPLOY_STAGING}/*test*.exe"
    COMMAND ${CMAKE_COMMAND} -P "${DEPLOY_CLEAN_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E echo "Clean deploy staging ready at: ${DEPLOY_STAGING}"
    DEPENDS SDR_Town sdrtown_control_client
    COMMENT "Creating clean, release-ready deploy staging (no stale binaries)"
)

# For CPack we prefer the clean staging when it exists (Big Caveat / audit prerequisite).
if(TARGET stage_rtl_runtime AND EXISTS "${rtlsdr_DIR}/copyright")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_STAGING}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${rtlsdr_DIR}/copyright"
            "${DEPLOY_STAGING}/licenses/rtlsdr-COPYRIGHT.txt"
        VERBATIM)
    install(FILES "${rtlsdr_DIR}/copyright" DESTINATION licenses RENAME rtlsdr-COPYRIGHT.txt)
endif()

# MUST run the deploy target first for a pristine tree (no Maul*, no *test* exes, only SDR_Town + windeployqt).
include(cmake/SstvBackend.cmake)
#   cmake --build build --config Release --target deploy
#   then (in build/bin/Release) windeployqt ... then re-run deploy target, then cpack.
if(EXISTS "${DEPLOY_STAGING}/SDR_Town.exe")
    set(CPACK_INSTALLED_DIRECTORIES "${DEPLOY_STAGING};.")
    # Do not also run the fallback Release-tree install and reintroduce stale
    # executables after constructing an allowlisted staging directory.
    set(CPACK_INSTALL_CMAKE_PROJECTS "")
    message(STATUS "Using clean deploy staging for packaging: ${DEPLOY_STAGING}")
else()
    # Fallback with aggressive stale purge (never ship old names or test binaries)
    file(GLOB_RECURSE STALE_BINARIES
         "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/Maul*.exe"
         "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/*Maul*.exe"
         "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/*test*.exe"
         "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/sdr_town_tests.exe"
         "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/maulaudio_tests.exe")
    if(STALE_BINARIES)
        file(REMOVE ${STALE_BINARIES})
    endif()
    set(CPACK_INSTALLED_DIRECTORIES "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release;.")
    message(STATUS "No clean staging — using Release after stale purge (run deploy target for production releases)")
endif()

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
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/redsea-block/"
          DESTINATION licenses/redsea-block)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/external/liquid-dsp/LICENSE"
          DESTINATION licenses RENAME liquid-dsp-LICENSE.txt)
  install(FILES
          "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/remote_diag_server.py"
          DESTINATION tools/diagnostics)
  install(FILES
          "${CMAKE_CURRENT_SOURCE_DIR}/scripts/start_remote_diag_server.ps1"
          "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install_remote_diag_task.ps1"
          DESTINATION scripts)
endif()

# --- Installer packaging with CPack (NSIS for Windows) ---
# S0-9: all CPACK_* must be set *before* include(CPack) so generators see the metadata
# (license, name, NSIS branding, etc.). Previously include was first → generic license in installer.
set(CPACK_PACKAGE_NAME "SDR_Town")
set(CPACK_PACKAGE_VENDOR "SDR Town Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SDR Town - Professional multi-SDR monitoring, scanning, and analysis tool")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SDR Town")
set(CPACK_PACKAGE_FILE_NAME "SDR_Town-${PROJECT_VERSION}-win64-setup")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_NSIS_DISPLAY_NAME "SDR Town")
  set(CPACK_NSIS_PACKAGE_NAME "SDR Town")
  set(CPACK_NSIS_CONTACT "https://github.com/Blkph0x/SDR_Town")
  set(CPACK_NSIS_MODIFY_PATH ON)

  # Real valid .ico (audit Big Caveat prerequisite). NSIS requires a proper ICO; we size-check and only set if real.
  # Recommended: drop a multi-resolution 256/128/64/32/16 ICO here (at least 32x32 32bpp works).
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico")
    file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico" _icon_size)
    if(_icon_size GREATER 100)
      set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico")
      message(STATUS "Using real branded icon for NSIS (${_icon_size} bytes)")
    endif()
  endif()

  # Release metadata is published beside the installer on GitHub. It is generated
  # after packaging so it can hash the final installer bytes, and must not be
  # embedded into the installer itself.
endif()

include(CPack)
