add_library(pastes_ui STATIC
    window/mainwindow.cpp window/mainwindow.h

    history/historyview.cpp history/historyview.h
    history/mainframe.cpp history/mainframe.h
    history/searchbar.cpp history/searchbar.h
    history/cardinteraction.cpp history/cardinteraction.h
    history/cardreflow.cpp history/cardreflow.h
    history/cardswipe.cpp history/cardswipe.h
    history/elasticscroll.cpp history/elasticscroll.h

    cards/pasteitem.cpp cards/pasteitem.h
    cards/pasteitembarnner.cpp cards/pasteitembarnner.h
    cards/pasteitemcontext.cpp cards/pasteitemcontext.h

    preview/previewdialog.cpp preview/previewdialog.h

    dialogs/appdialog.cpp dialogs/appdialog.h
    dialogs/settingsdialog.cpp dialogs/settingsdialog.h
    dialogs/syncsettingspage.cpp dialogs/syncsettingspage.h

    common/roundedwidgets.cpp common/roundedwidgets.h
    common/sourceiconview.cpp common/sourceiconview.h
    common/filepreview.cpp common/filepreview.h
)
target_link_libraries(pastes_ui PUBLIC pastes_application pastes_platform Qt6::Widgets)
target_compile_definitions(pastes_ui PRIVATE PASTES_VERSION="${PROJECT_VERSION}")

# Refresh the revision at build time so commits do not require reconfiguration.
find_package(Git QUIET)
set(PASTES_BUILD_VERSION_HEADER "${PROJECT_BINARY_DIR}/generated/pastes_build_version.h")
add_custom_target(pastes_build_version
    COMMAND ${CMAKE_COMMAND}
        "-DPASTES_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPASTES_OUTPUT_FILE=${PASTES_BUILD_VERSION_HEADER}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        -P "${PROJECT_SOURCE_DIR}/cmake/generate-build-version.cmake"
    BYPRODUCTS "${PASTES_BUILD_VERSION_HEADER}"
    VERBATIM
)
add_dependencies(pastes_ui pastes_build_version)
target_include_directories(pastes_ui PRIVATE "${PROJECT_BINARY_DIR}/generated")
