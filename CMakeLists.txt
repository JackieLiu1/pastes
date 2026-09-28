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
if(APPLE AND NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

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
    cardswipe.cpp cardswipe.h
    cardreflow.cpp cardreflow.h
    elasticscroll.cpp elasticscroll.h
    mainwindow.cpp mainwindow.h
    historypolicy.h
    database.cpp database.h
    clipboarddata.cpp clipboarddata.h
    pasteitem.cpp pasteitem.h
    pasteitembarnner.cpp pasteitembarnner.h
    pasteitemcontext.cpp pasteitemcontext.h
    filepreview.cpp filepreview.h
    searchbar.cpp searchbar.h
    sources.qrc
)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(PASTES_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/pastes.icns")
    set_source_files_properties(${PASTES_MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()

qt_add_executable(pastes ${PASTES_SOURCES} ${PASTES_MACOS_ICON})
target_include_directories(pastes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(pastes PRIVATE PASTES_VERSION="${PROJECT_VERSION}")

target_link_libraries(pastes PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    SingleApplication::SingleApplication
)

add_subdirectory(platform)

# translations: Pastes_zh_CN.ts -> Pastes_zh_CN.qm
qt_add_translation(QM_FILES Pastes_zh_CN.ts)
add_custom_target(translations ALL DEPENDS ${QM_FILES})
if(APPLE)
    add_dependencies(pastes translations)
    set_source_files_properties(${QM_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(pastes PRIVATE ${QM_FILES})
    set_property(TARGET pastes APPEND PROPERTY LINK_DEPENDS ${QM_FILES})
    add_custom_command(TARGET pastes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            $<TARGET_FILE_DIR:pastes>/Pastes_zh_CN.qm
        VERBATIM
    )
    # ServiceManagement requires a signed bundle, including its final resources.
    set(PASTES_MACOS_SIGNING_IDENTITY "-" CACHE STRING
        "macOS signing identity (- for local development)")
    find_program(PASTES_CODESIGN_EXECUTABLE codesign)
    if(NOT PASTES_CODESIGN_EXECUTABLE)
        message(FATAL_ERROR "codesign is required for macOS login items")
    endif()
    add_custom_command(TARGET pastes POST_BUILD
        COMMAND ${PASTES_CODESIGN_EXECUTABLE} --force --sign
            "${PASTES_MACOS_SIGNING_IDENTITY}" $<TARGET_BUNDLE_DIR:pastes>
        VERBATIM
    )
endif()


install(TARGETS pastes DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/pastes/translations)
