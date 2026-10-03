# DEC-0174: generated paths bind packaging to this configuration's build inputs.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(RUNTIME_CONFIG "${CMAKE_BINARY_DIR}/runtime-inputs-$<CONFIG>.json")
get_filename_component(_runtime_vcpkg_root "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
get_filename_component(_runtime_vcpkg_root "${_runtime_vcpkg_root}/../.." ABSOLUTE)
set(_runtime_files "{}")
foreach(_pair
    "SDR_Town.exe|$<TARGET_FILE:SDR_Town>"
    "SdrTownControl.dll|$<TARGET_FILE:sdrtown_control_client>"
    "sdr_aero_codec.dll|$<TARGET_FILE:sdr_aero_codec>"
    "SoapyRTLSDR.dll|${CMAKE_BINARY_DIR}/rtl-driver-build/Release/rtlsdrSupport.dll"
    "sdrPlaySupport.dll|${CMAKE_BINARY_DIR}/sdrplay-driver-build/Release/sdrPlaySupport.dll")
    string(REPLACE "|" ";" _parts "${_pair}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _path)
    string(JSON _runtime_files SET "${_runtime_files}" "${_name}" "\"${_path}\"")
endforeach()
if(SDR_TOWN_BUILD_RDS_DSP)
    string(JSON _runtime_files SET "${_runtime_files}" sdrtown_rds_dsp.dll
        "\"${CMAKE_BINARY_DIR}/rds-dsp-runtime/sdrtown_rds_dsp.dll\"")
endif()
file(GENERATE OUTPUT "${RUNTIME_CONFIG}" CONTENT "{
  \"schema\": 1,
  \"configuration\": \"$<CONFIG>\",
  \"buildRoot\": \"${CMAKE_BINARY_DIR}\",
  \"vcpkgBin\": \"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin\",
  \"vcpkgRoot\": \"${_runtime_vcpkg_root}\",
  \"qtBin\": \"$<TARGET_FILE_DIR:Qt6::Core>\",
  \"qtVersion\": \"${Qt6_VERSION}\",
  \"compiler\": \"${CMAKE_CXX_COMPILER}\",
  \"builtFiles\": ${_runtime_files},
  \"moduleNotices\": {
    \"licenses/SoapyRTLSDR-LICENSE.txt\": \"${CMAKE_BINARY_DIR}/rtl-driver-source/LICENSE.txt\",
    \"licenses/SoapySDRPlay3-LICENSE.txt\": \"${CMAKE_BINARY_DIR}/sdrplay-driver-source/LICENSE.txt\"
  }
}
")
