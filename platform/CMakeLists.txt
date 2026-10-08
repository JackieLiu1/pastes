# Native adapters depend on application ports, never on application widgets.
add_library(pastes_platform STATIC)
target_link_libraries(pastes_platform PUBLIC pastes_application Qt6::Widgets)
target_sources(pastes_platform PRIVATE
    clipboardsource.h pastetarget.h windowintegration.h windowintegration.cpp
    applicationintegration.h menuintegration.h menuintegration.cpp
    secretstore.h localcredentialstore.cpp
    diagnosticlog.h diagnosticlog.cpp
    fileicon.h paths.h startupintegration.h startupintegration.cpp
    globalshortcut.h globalshortcut.cpp shortcut_p.h trayicon.h
)

# Exactly one native backend is compiled; shared desktop behavior has no SDK
# dependency and can be reused by Windows and Linux.
if(WIN32)
    target_sources(pastes_platform PRIVATE
        windows/secretstore.cpp
        windows/clipboardsource.cpp windows/pastetarget.cpp
        windows/shortcut.cpp windows/shortcutstate.h windows/windowblur.cpp
        windows/compositionabi.h
        windows/fileicon.cpp windows/paths.cpp windows/startupintegration.cpp
        desktop/windowintegration.cpp desktop/menuintegration.cpp
        desktop/applicationintegration.cpp desktop/trayicon.cpp
    )
elseif(APPLE)
    target_sources(pastes_platform PRIVATE
        macos/secretstore.cpp
        macos/clipboardpolicy.mm macos/clipboardpolicy.h
        macos/clipboardmetadata.mm macos/clipboardmetadata.h
        macos/clipboardsource.mm macos/pastetarget.mm macos/shortcut.cpp
        macos/windowintegration.mm macos/menuintegration.mm
        macos/applicationintegration.mm macos/fileicon.cpp macos/paths.cpp
        macos/startupintegration.mm macos/trayicon.mm
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pastes_platform PRIVATE
        linux/secretstore.cpp
        linux/clipboardsource.cpp linux/pastetarget.cpp linux/shortcut.cpp
        linux/windowblur.cpp linux/fileicon.cpp linux/paths.cpp
        linux/startupintegration.cpp desktop/windowintegration.cpp
        desktop/menuintegration.cpp desktop/applicationintegration.cpp
        desktop/trayicon.cpp
    )
else()
    message(FATAL_ERROR "Pastes requires Windows, macOS or Linux")
endif()

if(WIN32)
    set_target_properties(pastes PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(pastes_platform PRIVATE psapi gdi32 shell32 advapi32
        dwmapi d2d1 runtimeobject)
elseif(APPLE)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(APPLICATIONSERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(SERVICEMANAGEMENT_FRAMEWORK ServiceManagement REQUIRED)
    target_link_libraries(pastes_platform PRIVATE ${CARBON_FRAMEWORK}
        ${APPKIT_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK}
        ${APPLICATIONSERVICES_FRAMEWORK} ${SERVICEMANAGEMENT_FRAMEWORK})
    set_target_properties(pastes PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME Pastes
        MACOSX_BUNDLE_ICON_FILE pastes.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.JackieLiu1.pastes
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
    # Info.plist changes invalidate the bundle signature even without C++ edits.
    set_property(TARGET pastes APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
        "$<TARGET_BUNDLE_DIR:pastes>/Contents/Info.plist")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 glib-2.0 gio-unix-2.0)
    target_link_libraries(pastes_platform PRIVATE
        X11::X11
        ${X11_Xtst_LIB}
        PkgConfig::GIO
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux resolves installed translations through the paths service
    target_compile_definitions(pastes_platform PRIVATE
        QM_FILES_INSTALL_PATH="${CMAKE_INSTALL_FULL_DATADIR}/pastes/translations/"
    )
    install(FILES "${PROJECT_SOURCE_DIR}/pastes.desktop" DESTINATION /etc/xdg/autostart)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/pastes.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/pastes.svg"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
