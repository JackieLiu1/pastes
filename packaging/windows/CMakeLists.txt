find_program(PASTES_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
find_program(PASTES_ISCC_EXECUTABLE NAMES ISCC
    HINTS "${CMAKE_BINARY_DIR}/windows-tools/inno"
        "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
        "$ENV{ProgramFiles}/Inno Setup 6")

if(MINGW AND PASTES_POWERSHELL_EXECUTABLE AND PASTES_ISCC_EXECUTABLE)
    add_custom_target(package_windows
        COMMAND ${CMAKE_COMMAND} -E env --unset=PSModulePath
            ${PASTES_POWERSHELL_EXECUTABLE} -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/package.ps1"
            -Executable $<TARGET_FILE:pastes>
            -Translation "${CMAKE_BINARY_DIR}/Pastes_zh_CN.qm"
            -Version "${PROJECT_VERSION}"
            -QtBin $<TARGET_FILE_DIR:Qt6::Core>
            -Iscc "${PASTES_ISCC_EXECUTABLE}"
            -Output "${CMAKE_BINARY_DIR}/dist"
        DEPENDS pastes translations
        USES_TERMINAL
        VERBATIM
    )
else()
    message(STATUS "package_windows requires MinGW, PowerShell and Inno Setup 6.5+")
endif()
