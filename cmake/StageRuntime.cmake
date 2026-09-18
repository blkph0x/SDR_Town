if(NOT IS_DIRECTORY "${SOURCE}" OR NOT IS_DIRECTORY "${DESTINATION}")
    message(FATAL_ERROR "StageRuntime requires existing SOURCE and DESTINATION directories")
endif()

# Never publish local replay audio, logs, captures, or old executables.
file(GLOB RUNTIME_DLLS "${SOURCE}/*.dll")
if(RUNTIME_DLLS)
    file(COPY ${RUNTIME_DLLS} DESTINATION "${DESTINATION}")
endif()
if(EXISTS "${SOURCE}/qt.conf")
    file(COPY "${SOURCE}/qt.conf" DESTINATION "${DESTINATION}")
endif()
foreach(PLUGIN_DIR generic iconengines imageformats networkinformation platforms styles tls translations)
    if(IS_DIRECTORY "${SOURCE}/${PLUGIN_DIR}")
        file(COPY "${SOURCE}/${PLUGIN_DIR}" DESTINATION "${DESTINATION}"
             FILES_MATCHING PATTERN "*.dll" PATTERN "*.qm")
    endif()
endforeach()
