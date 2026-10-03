cmake_minimum_required(VERSION 3.16)

project(pastes VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

# Network is used privately by the bundled SingleApplication; declaring it
# here satisfies Qt6's dependency walk at target finalization
find_package(Qt6 6.3 REQUIRED COMPONENTS Core Gui Widgets Sql Network LinguistTools)

# used by the bundled SingleApplication build
set(QAPPLICATION_CLASS QApplication)
add_subdirectory(3rd/SingleApplication)

# Dependencies point inward: core <- application <- adapters / UI.
add_library(pastes_core STATIC
    core/itemdata.cpp core/itemdata.h
    core/clipboarddata.cpp core/clipboarddata.h
    core/clipboardcontent.cpp core/clipboardcontent.h
    core/historypolicy.h
    core/sourceicon.h
)
# Core has no Qt meta-object declarations; avoid an empty moc archive member.
set_target_properties(pastes_core PROPERTIES AUTOMOC OFF)
target_include_directories(pastes_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pastes_core PUBLIC Qt6::Core Qt6::Gui)

add_library(pastes_application STATIC
    application/historyrepository.h application/clipboardfeed.h application/syncservice.h
    application/historyservice.cpp application/historyservice.h
    application/clipboardcontroller.cpp application/clipboardcontroller.h
)
target_link_libraries(pastes_application PUBLIC pastes_core)

add_library(pastes_storage STATIC
    storage/database.cpp storage/database.h
    storage/storedentry.cpp storage/storedentry.h
)
target_link_libraries(pastes_storage PUBLIC pastes_application PRIVATE Qt6::Sql)

add_library(pastes_sync STATIC
    sync/synccontent.cpp sync/synccontent.h
    sync/webdavworker.cpp sync/webdavworker.h
    sync/webdavsync.cpp sync/webdavsync.h
)
target_link_libraries(pastes_sync PUBLIC pastes_application PRIVATE Qt6::Network)

set(PASTES_UI_SOURCES
    ui/appdialog.cpp
    ui/appdialog.h
    ui/cardinteraction.cpp
    ui/cardinteraction.h
    ui/cardreflow.cpp
    ui/cardreflow.h
    ui/cardswipe.cpp
    ui/cardswipe.h
    ui/elasticscroll.cpp
    ui/elasticscroll.h
    ui/filepreview.cpp
    ui/filepreview.h
    ui/historyview.cpp
    ui/historyview.h
    ui/mainframe.cpp
    ui/mainframe.h
    ui/mainwindow.cpp
    ui/mainwindow.h
    ui/pasteitem.cpp
    ui/pasteitem.h
    ui/pasteitembarnner.cpp
    ui/pasteitembarnner.h
    ui/pasteitemcontext.cpp
    ui/pasteitemcontext.h
    ui/previewdialog.cpp
    ui/previewdialog.h
    ui/roundedwidgets.cpp
    ui/roundedwidgets.h
    ui/searchbar.cpp
    ui/searchbar.h
    ui/sourceiconview.cpp ui/sourceiconview.h
    ui/syncsettingspage.cpp ui/syncsettingspage.h
    ui/settingsdialog.cpp
    ui/settingsdialog.h
)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(PASTES_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/pastes.icns")
    set_source_files_properties(${PASTES_MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()

qt_add_executable(pastes app/main.cpp sources.qrc ${PASTES_MACOS_ICON})
target_include_directories(pastes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(platform)

add_library(pastes_ui STATIC ${PASTES_UI_SOURCES})
target_link_libraries(pastes_ui PUBLIC pastes_application pastes_platform Qt6::Widgets)
target_compile_definitions(pastes_ui PRIVATE PASTES_VERSION="${PROJECT_VERSION}")

# Refresh the revision at build time so commits do not require reconfiguration.
find_package(Git QUIET)
set(PASTES_BUILD_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/pastes_build_version.h")
add_custom_target(pastes_build_version
    COMMAND ${CMAKE_COMMAND}
        "-DPASTES_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DPASTES_OUTPUT_FILE=${PASTES_BUILD_VERSION_HEADER}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate-build-version.cmake"
    BYPRODUCTS "${PASTES_BUILD_VERSION_HEADER}"
    VERBATIM
)
add_dependencies(pastes_ui pastes_build_version)
target_include_directories(pastes_ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pastes PRIVATE pastes_ui pastes_storage pastes_sync SingleApplication::SingleApplication)


# translations: Pastes_zh_CN.ts -> Pastes_zh_CN.qm
add_custom_target(update_translations
    COMMAND Qt6::lupdate -I "${CMAKE_CURRENT_SOURCE_DIR}" -extensions cpp,h,mm
        app core application storage sync ui platform -ts Pastes_zh_CN.ts
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)
qt_add_translation(QM_FILES Pastes_zh_CN.ts)
add_custom_target(translations ALL DEPENDS ${QM_FILES})
if(WIN32)
    add_subdirectory(packaging/windows)
endif()
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
        "macOS certificate identity (- for ad-hoc signing)")
    find_program(PASTES_CODESIGN_EXECUTABLE codesign)
    if(NOT PASTES_CODESIGN_EXECUTABLE)
        message(FATAL_ERROR "codesign is required for macOS login items")
    endif()
    add_custom_command(TARGET pastes POST_BUILD
        COMMAND ${PASTES_CODESIGN_EXECUTABLE} --force --sign
            "${PASTES_MACOS_SIGNING_IDENTITY}" $<TARGET_BUNDLE_DIR:pastes>
        VERBATIM
    )
    add_subdirectory(packaging/macos)
endif()


install(TARGETS pastes DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/pastes/translations)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(PASTES_BUILD_BENCHMARKS "Build isolated history performance benchmarks" OFF)
if(PASTES_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
