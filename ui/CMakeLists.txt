add_library(pastes_ui STATIC
    mainwindow.cpp mainwindow.h

    historyview.cpp historyview.h
    mainframe.cpp mainframe.h
    searchbar.cpp searchbar.h
    cardinteraction.cpp cardinteraction.h
    cardreflow.cpp cardreflow.h
    cardswipe.cpp cardswipe.h
    elasticscroll.cpp elasticscroll.h

    pasteitem.cpp pasteitem.h
    pasteitembarnner.cpp pasteitembarnner.h
    pasteitemcontext.cpp pasteitemcontext.h

    previewdialog.cpp previewdialog.h

    appdialog.cpp appdialog.h
    settingsdialog.cpp settingsdialog.h
    syncsettingspage.cpp syncsettingspage.h

    roundedwidgets.cpp roundedwidgets.h
    sourceiconview.cpp sourceiconview.h
    filepreview.cpp filepreview.h
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
