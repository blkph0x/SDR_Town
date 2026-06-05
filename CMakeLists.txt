cmake_minimum_required(VERSION 3.25)

project(MaulAudioPro VERSION 0.0.1 LANGUAGES CXX)

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
set(MAULAUDIO_SOURCES
    src/main.cpp
    src/DeviceManager.cpp
    src/SpectrumWidget.cpp
)

# Resources (icons, qss, etc. later)
# qt_add_resources(MAULAUDIO_RESOURCES resources/resources.qrc)

add_executable(${PROJECT_NAME} WIN32
    ${MAULAUDIO_SOURCES}
    # ${MAULAUDIO_RESOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Link SoapySDR if available (for PR2 device layer)
if (SoapySDR_FOUND)
  target_link_libraries(${PROJECT_NAME} PRIVATE SoapySDR::SoapySDR)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SOAPYSDR)
elseif (SOAPY_FOUND)
  target_include_directories(${PROJECT_NAME} PRIVATE ${SOAPY_INCLUDE_DIRS})
  target_link_libraries(${PROJECT_NAME} PRIVATE ${SOAPY_LIBRARIES})
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SOAPYSDR)
endif()

# Include project headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Windows specific
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Install rules (basic)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# For convenience in IDEs
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

message(STATUS "MaulAudioPro configured. Qt6 + spdlog + nlohmann-json + (optional SoapySDR for devices) expected.")