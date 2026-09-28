find_package(Python3 3.9 QUIET COMPONENTS Interpreter)
set(PASTES_MACOS_INSTALLER_SIGNING_IDENTITY "" CACHE STRING
    "Optional Developer ID Installer identity for the macOS package")

if(Python3_Interpreter_FOUND AND TARGET Qt6::macdeployqt AND TARGET Qt6::qmake)
    add_custom_target(package_macos
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/package.py"
            --app $<TARGET_BUNDLE_DIR:pastes>
            --output "${CMAKE_BINARY_DIR}/dist"
            --version "${PROJECT_VERSION}"
            --macdeployqt $<TARGET_FILE:Qt6::macdeployqt>
            --qmake $<TARGET_FILE:Qt6::qmake>
            --identity "${PASTES_MACOS_SIGNING_IDENTITY}"
            --installer-identity "${PASTES_MACOS_INSTALLER_SIGNING_IDENTITY}"
        DEPENDS pastes
        USES_TERMINAL
        VERBATIM
    )
else()
    message(STATUS "package_macos requires Python 3.9+, macdeployqt and qmake")
endif()
