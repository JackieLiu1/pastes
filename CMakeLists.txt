cmake_minimum_required(VERSION 3.16)

project(pastes VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

# Network is used privately by the bundled SingleApplication; declaring it
# here satisfies Qt6's dependency walk at target finalization
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network LinguistTools)

# used by the bundled SingleApplication build
set(QAPPLICATION_CLASS QApplication)
add_subdirectory(3rd/SingleApplication)

set(PASTES_SOURCES
    main.cpp
    mainframe.cpp mainframe.h
    roundedwidgets.cpp roundedwidgets.h
    previewdialog.cpp previewdialog.h
    appdialog.cpp appdialog.h
    settingsdialog.cpp settingsdialog.h
    startupintegration.cpp startupintegration.h
    cardswipe.cpp cardswipe.h
    cardreflow.cpp cardreflow.h
    elasticscroll.cpp elasticscroll.h
    mainwindow.cpp mainwindow.h
    database.cpp database.h
    pasteitem.cpp pasteitem.h
    pasteitembarnner.cpp pasteitembarnner.h
    pasteitemcontext.cpp pasteitemcontext.h
    searchbar.cpp searchbar.h
    shortcut.cpp shortcut.h
    sources.qrc
)

if(WIN32)
    list(APPEND PASTES_SOURCES shortcut_win.cpp shortcut_win_state.h
        clipboardsource_win.cpp clipboardsource_win.h)
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    list(APPEND PASTES_SOURCES shortcut_mac.cpp window_mac.mm window_mac.h
        clipboardsource_mac.mm clipboardsource_mac.h)
elseif(UNIX AND NOT APPLE)
    list(APPEND PASTES_SOURCES shortcut_x11.cpp)
endif()

qt_add_executable(pastes ${PASTES_SOURCES})
target_compile_definitions(pastes PRIVATE PASTES_VERSION="${PROJECT_VERSION}")

target_link_libraries(pastes PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    SingleApplication::SingleApplication
)

if(WIN32)
    set_target_properties(pastes PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(pastes PRIVATE psapi gdi32 shell32)
elseif(APPLE)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(pastes PRIVATE ${CARBON_FRAMEWORK}
        ${APPKIT_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK})
    set(PASTES_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/pastes.icns")
    target_sources(pastes PRIVATE ${PASTES_MACOS_ICON})
    set_source_files_properties(${PASTES_MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(pastes PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME Pastes
        MACOSX_BUNDLE_ICON_FILE pastes.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.JackieLiu1.pastes
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
elseif(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 glib-2.0 gio-unix-2.0)
    target_link_libraries(pastes PRIVATE
        X11::X11
        ${X11_Xtst_LIB}
        PkgConfig::GIO
    )
endif()

# translations: Pastes_zh_CN.ts -> Pastes_zh_CN.qm
qt_add_translation(QM_FILES Pastes_zh_CN.ts)
add_custom_target(translations ALL DEPENDS ${QM_FILES})
if(APPLE)
    add_dependencies(pastes translations)
    add_custom_command(TARGET pastes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QM_FILES} $<TARGET_FILE_DIR:pastes>
    )
endif()

if(UNIX AND NOT APPLE)
    # main.cpp loads the .qm from this path at runtime
    target_compile_definitions(pastes PRIVATE
        QM_FILES_INSTALL_PATH="${CMAKE_INSTALL_FULL_DATADIR}/pastes/translations/"
    )
    install(FILES pastes.desktop DESTINATION /etc/xdg/autostart)
    install(FILES resources/pastes.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

install(TARGETS pastes DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/pastes/translations)
