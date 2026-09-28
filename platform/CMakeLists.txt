# Public contracts and Qt-only facades belong to the application target.
target_sources(pastes PRIVATE
    clipboardsource.h pastetarget.h windowintegration.h windowintegration.cpp
    applicationintegration.h menuintegration.h menuintegration.cpp
    fileicon.h paths.h startupintegration.h startupintegration.cpp
    globalshortcut.h globalshortcut.cpp shortcut_p.h
)

# Exactly one native backend is compiled; shared desktop behavior has no SDK
# dependency and can be reused by Windows and Linux.
if(WIN32)
    target_sources(pastes PRIVATE
        windows/clipboardsource.cpp windows/pastetarget.cpp
        windows/shortcut.cpp windows/shortcutstate.h windows/windowblur.cpp
        windows/fileicon.cpp windows/paths.cpp windows/startupintegration.cpp
        desktop/windowintegration.cpp desktop/menuintegration.cpp
        desktop/applicationintegration.cpp
    )
elseif(APPLE)
    target_sources(pastes PRIVATE
        macos/clipboardsource.mm macos/pastetarget.mm macos/shortcut.cpp
        macos/windowintegration.mm macos/menuintegration.mm
        macos/applicationintegration.mm macos/fileicon.cpp macos/paths.cpp
        macos/startupintegration.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pastes PRIVATE
        linux/clipboardsource.cpp linux/pastetarget.cpp linux/shortcut.cpp
        linux/windowblur.cpp linux/fileicon.cpp linux/paths.cpp
        linux/startupintegration.cpp desktop/windowintegration.cpp
        desktop/menuintegration.cpp desktop/applicationintegration.cpp
    )
else()
    message(FATAL_ERROR "Pastes requires Windows, macOS or Linux")
endif()

if(WIN32)
    set_target_properties(pastes PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(pastes PRIVATE psapi gdi32 shell32)
elseif(APPLE)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(APPLICATIONSERVICES_FRAMEWORK ApplicationServices REQUIRED)
    target_link_libraries(pastes PRIVATE ${CARBON_FRAMEWORK}
        ${APPKIT_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK}
        ${APPLICATIONSERVICES_FRAMEWORK})
    set_target_properties(pastes PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME Pastes
        MACOSX_BUNDLE_ICON_FILE pastes.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.JackieLiu1.pastes
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 glib-2.0 gio-unix-2.0)
    target_link_libraries(pastes PRIVATE
        X11::X11
        ${X11_Xtst_LIB}
        PkgConfig::GIO
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux resolves installed translations through the paths service
    target_compile_definitions(pastes PRIVATE
        QM_FILES_INSTALL_PATH="${CMAKE_INSTALL_FULL_DATADIR}/pastes/translations/"
    )
    install(FILES "${PROJECT_SOURCE_DIR}/pastes.desktop" DESTINATION /etc/xdg/autostart)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/pastes.svg"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
