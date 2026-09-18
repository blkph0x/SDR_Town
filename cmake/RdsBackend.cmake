option(SDR_TOWN_BUILD_RDS_DSP "Build isolated Redsea/liquid-dsp runtime using MinGW" ON)
if(SDR_TOWN_BUILD_RDS_DSP)
    find_program(SDR_TOWN_RDS_CC NAMES x86_64-w64-mingw32-gcc gcc REQUIRED)
    find_program(SDR_TOWN_RDS_CXX NAMES x86_64-w64-mingw32-g++ g++ REQUIRED)
    find_program(SDR_TOWN_RDS_MAKE NAMES mingw32-make make REQUIRED)
    execute_process(COMMAND "${SDR_TOWN_RDS_CC}" -dumpmachine
        OUTPUT_VARIABLE RDS_MACHINE OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT RDS_MACHINE STREQUAL "x86_64-w64-mingw32")
        message(FATAL_ERROR "RDS DSP needs x86_64-w64-mingw32 GCC or SDR_TOWN_BUILD_RDS_DSP=OFF")
    endif()
    include(ExternalProject)
    ExternalProject_Add(rds_backend
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/rds_backend"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/rds-dsp-runtime"
        CMAKE_GENERATOR "MinGW Makefiles"
        CMAKE_ARGS "-DCMAKE_MAKE_PROGRAM=${SDR_TOWN_RDS_MAKE}"
            "-DCMAKE_C_COMPILER=${SDR_TOWN_RDS_CC}" "-DCMAKE_CXX_COMPILER=${SDR_TOWN_RDS_CXX}"
            "-DCMAKE_BUILD_TYPE=Release"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target sdrtown_rds_dsp --parallel 4
        BUILD_ALWAYS TRUE INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/rds-dsp-runtime/sdrtown_rds_dsp.dll")
    add_custom_target(rds_runtime
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/rds-dsp-runtime/sdrtown_rds_dsp.dll"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/sdrtown_rds_dsp.dll"
        DEPENDS rds_backend)
endif()
