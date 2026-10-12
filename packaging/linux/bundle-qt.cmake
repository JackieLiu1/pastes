# Follow OrcaTerminal's Linux deployment: private Qt/ICU libraries and plugins,
# with system libraries supplied by the distribution.
option(PASTES_LINUX_BUNDLE_QT
    "Bundle the Qt runtime resolved by CMake into Linux packages" ON)
if(NOT PASTES_LINUX_BUNDLE_QT)
    return()
endif()

function(pastes_imported_location target output)
    foreach(property IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_RELWITHDEBINFO
            IMPORTED_LOCATION_MINSIZEREL IMPORTED_LOCATION_DEBUG IMPORTED_LOCATION)
        get_target_property(location ${target} ${property})
        if(location)
            set(${output} "${location}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Cannot resolve the runtime location of ${target}")
endfunction()

# Seeds must include every directly linked module, especially Sql: no image or
# platform plugin necessarily depends on it.
set(PASTES_QT_RUNTIME_SEEDS)
foreach(module Core Gui Widgets Sql Network)
    pastes_imported_location(Qt6::${module} location)
    list(APPEND PASTES_QT_RUNTIME_SEEDS "${location}")
endforeach()
pastes_imported_location(Qt6::Core pastes_qt_core)
get_filename_component(PASTES_QT_LIBRARY_DIR "${pastes_qt_core}" DIRECTORY)
pastes_imported_location(Qt6::qmake pastes_qmake)
execute_process(COMMAND "${pastes_qmake}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE PASTES_QT_PLUGIN_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE pastes_qmake_result ERROR_QUIET)
if(NOT pastes_qmake_result EQUAL 0 OR NOT IS_ABSOLUTE "${PASTES_QT_PLUGIN_DIR}")
    message(FATAL_ERROR "Cannot determine the Qt runtime plugin directory")
endif()

find_program(PASTES_PATCHELF_EXECUTABLE NAMES patchelf)
if(NOT PASTES_PATCHELF_EXECUTABLE)
    message(FATAL_ERROR
        "Bundled Qt packaging requires patchelf. Install patchelf or set "
        "-DPASTES_LINUX_BUNDLE_QT=OFF to use distribution Qt packages.")
endif()

set(PASTES_QT_PLUGINS)
foreach(plugin platforms/libqxcb.so sqldrivers/libqsqlite.so
        imageformats/libqsvg.so iconengines/libqsvgicon.so)
    if(NOT EXISTS "${PASTES_QT_PLUGIN_DIR}/${plugin}")
        message(FATAL_ERROR
            "Bundled Qt packaging requires ${PASTES_QT_PLUGIN_DIR}/${plugin}. "
            "Build and install Qt's X11, SQLite and SVG runtime plugins.")
    endif()
    list(APPEND PASTES_QT_PLUGINS "${plugin}")
endforeach()
foreach(directory platformthemes xcbglintegrations tls)
    file(GLOB plugins LIST_DIRECTORIES false RELATIVE "${PASTES_QT_PLUGIN_DIR}"
        "${PASTES_QT_PLUGIN_DIR}/${directory}/*.so")
    list(APPEND PASTES_QT_PLUGINS ${plugins})
endforeach()
foreach(plugin platforms/libqoffscreen.so
        platforminputcontexts/libcomposeplatforminputcontextplugin.so
        platforminputcontexts/libibusplatforminputcontextplugin.so
        imageformats/libqgif.so imageformats/libqjpeg.so
        imageformats/libqwebp.so imageformats/libqico.so)
    if(EXISTS "${PASTES_QT_PLUGIN_DIR}/${plugin}")
        list(APPEND PASTES_QT_PLUGINS "${plugin}")
    endif()
endforeach()
list(REMOVE_DUPLICATES PASTES_QT_PLUGINS)

set(PASTES_QT_RUNTIME_DIR "${PASTES_LINUX_INSTALL_DIR}")
set_target_properties(pastes PROPERTIES
    INSTALL_RPATH "$ORIGIN/lib"
    INSTALL_RPATH_USE_LINK_PATH FALSE)

# Keep qt.conf beside the private executable, away from other /usr/bin apps.
configure_file("${CMAKE_CURRENT_LIST_DIR}/qt.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/qt.conf" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qt.conf"
    DESTINATION "${PASTES_QT_RUNTIME_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/install-qt-runtime.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/install-qt-runtime.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-qt-runtime.cmake")
message(STATUS "Linux packaging bundles Qt from ${PASTES_QT_LIBRARY_DIR}")
