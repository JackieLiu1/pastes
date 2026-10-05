find_package(Python3 3.9 QUIET COMPONENTS Interpreter)
set(PASTES_MACOS_INSTALLER_SIGNING_IDENTITY "" CACHE STRING
    "Optional Developer ID Installer identity for the macOS package")
option(PASTES_MACOS_LOCAL_SIGNING
    "Use a persistent local certificate without distribution signing options" OFF)
set(local_signing_args)
if(PASTES_MACOS_LOCAL_SIGNING)
    if(NOT PASTES_MACOS_SIGNING_IDENTITY OR PASTES_MACOS_SIGNING_IDENTITY STREQUAL "-")
        message(FATAL_ERROR "Local signing requires a certificate identity, not ad-hoc signing")
    endif()
    list(APPEND local_signing_args --local-signing)
endif()

if(Python3_Interpreter_FOUND AND TARGET Qt6::macdeployqt AND TARGET Qt6::qmake)
    add_custom_target(package
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/package.py"
            --app $<TARGET_BUNDLE_DIR:pastes>
            --output "${CMAKE_BINARY_DIR}/dist"
            --version "${PROJECT_VERSION}"
            --macdeployqt $<TARGET_FILE:Qt6::macdeployqt>
            --qmake $<TARGET_FILE:Qt6::qmake>
            --identity "${PASTES_MACOS_SIGNING_IDENTITY}"
            --installer-identity "${PASTES_MACOS_INSTALLER_SIGNING_IDENTITY}"
            ${local_signing_args}
        DEPENDS pastes
        USES_TERMINAL
        VERBATIM
    )
else()
    add_custom_target(package
        COMMAND ${CMAKE_COMMAND} -E echo
            "macOS packaging requires Python 3.9+, macdeployqt and qmake"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
    message(STATUS "package requires Python 3.9+, macdeployqt and qmake")
endif()
