set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_LICENSE "LGPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_RPM_PACKAGE_AUTOREQ "yes")
set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)
# Keep temporary build scripts within the build tree on restricted hosts.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/rpm-tmp")
string(APPEND CPACK_RPM_SPEC_MORE_DEFINE
    "\n%define _tmppath ${CMAKE_BINARY_DIR}/rpm-tmp\n")
if(NOT CPACK_RPM_PACKAGE_ARCHITECTURE)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" pastes_rpm_architecture)
    if(pastes_rpm_architecture MATCHES "^(aarch64|arm64)$")
        set(pastes_rpm_architecture "aarch64")
    elseif(pastes_rpm_architecture MATCHES "^(x86_64|amd64)$")
        set(pastes_rpm_architecture "x86_64")
    endif()
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "${pastes_rpm_architecture}")
endif()
# File requirements work across distributions with different Qt package names.
# RPM's automatic ELF dependency scan does not see these runtime-loaded plugins.
if(PASTES_LINUX_BUNDLE_QT)
    if(NOT DEFINED CPACK_RPM_PACKAGE_REQUIRES)
        set(CPACK_RPM_PACKAGE_REQUIRES "/usr/bin/secret-tool")
    endif()
    # Private Qt/ICU files must not satisfy other applications' runtime needs.
    string(APPEND CPACK_RPM_SPEC_MORE_DEFINE [=[
%global _build_id_links none
%global __requires_exclude ^(libQt6|libicu).*
%global __provides_exclude ^(libQt6|libicu|libq|lib.*platforminputcontextplugin).*
]=])
elseif(NOT DEFINED CPACK_RPM_PACKAGE_REQUIRES)
    get_target_property(pastes_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    if(NOT pastes_qmake_executable)
        get_target_property(pastes_qmake_executable Qt6::qmake LOCATION)
    endif()
    execute_process(COMMAND "${pastes_qmake_executable}" -query QT_INSTALL_PLUGINS
        OUTPUT_VARIABLE pastes_qt_plugins
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE pastes_qmake_result
        ERROR_QUIET)
    if(NOT pastes_qmake_result EQUAL 0 OR NOT IS_ABSOLUTE "${pastes_qt_plugins}")
        message(FATAL_ERROR
            "Cannot determine the Qt runtime plugin directory for RPM dependencies. "
            "Set -DCPACK_RPM_PACKAGE_REQUIRES=<requirements> explicitly.")
    endif()
    set(CPACK_RPM_PACKAGE_REQUIRES
        "${pastes_qt_plugins}/sqldrivers/libqsqlite.so, ${pastes_qt_plugins}/imageformats/libqsvg.so, ${pastes_qt_plugins}/iconengines/libqsvgicon.so, /usr/bin/secret-tool")
endif()
# These directories belong to the desktop's filesystem and icon packages.
list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /opt /etc/xdg /etc/xdg/autostart
    /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
    /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps)
