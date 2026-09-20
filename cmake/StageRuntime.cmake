if(NOT IS_DIRECTORY "${SOURCE}" OR NOT IS_DIRECTORY "${DESTINATION}")
    message(FATAL_ERROR "StageRuntime requires existing SOURCE and DESTINATION directories")
endif()

# Never publish local replay audio, logs, captures, or old executables.
# Skip leftover versioned control DLLs (SdrTownControl-0.2.N-win64.dll) from prior
# local builds; testers need SdrTownControl.dll only.
file(GLOB RUNTIME_DLLS "${SOURCE}/*.dll")
foreach(DLL ${RUNTIME_DLLS})
    get_filename_component(DLL_NAME "${DLL}" NAME)
    if(DLL_NAME MATCHES "^SdrTownControl-.+-win64\\.dll$")
        continue()
    endif()
    file(COPY "${DLL}" DESTINATION "${DESTINATION}")
endforeach()
if(EXISTS "${SOURCE}/qt.conf")
    file(COPY "${SOURCE}/qt.conf" DESTINATION "${DESTINATION}")
endif()
foreach(PLUGIN_DIR generic iconengines imageformats networkinformation platforms styles tls translations)
    if(IS_DIRECTORY "${SOURCE}/${PLUGIN_DIR}")
        file(COPY "${SOURCE}/${PLUGIN_DIR}" DESTINATION "${DESTINATION}"
             FILES_MATCHING PATTERN "*.dll" PATTERN "*.qm")
    endif()
endforeach()
