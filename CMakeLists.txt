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

# Sources (will grow)
set(MAULAUDIO_SOURCES
    src/main.cpp
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

message(STATUS "MaulAudioPro configured. Qt6 + spdlog + nlohmann-json expected.")