# CPack supplies the same package target as the native Windows/macOS packagers.
# Public application directories use 755 for every package generator.
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
set(PASTES_LINUX_INSTALL_DIR "opt/pastes")
include("${CMAKE_CURRENT_LIST_DIR}/bundle-qt.cmake")
install(TARGETS pastes DESTINATION "${PASTES_LINUX_INSTALL_DIR}")
install(FILES ${QM_FILES} DESTINATION "${PASTES_LINUX_INSTALL_DIR}/translations")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${PASTES_LINUX_INSTALL_DIR}" RENAME LICENSE.txt)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION usr/share/doc/pastes
    RENAME copyright)
target_compile_definitions(pastes_platform PRIVATE
    QM_FILES_INSTALL_PATH="/${PASTES_LINUX_INSTALL_DIR}/translations/")
configure_file("${CMAKE_CURRENT_LIST_DIR}/pastes-launcher.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pastes-launcher" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/pastes-launcher"
    DESTINATION usr/bin RENAME pastes)
# Desktop integration stays in system paths, as it does for OrcaTerminal.
install(FILES "${PROJECT_SOURCE_DIR}/pastes.desktop" DESTINATION etc/xdg/autostart)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/pastes.desktop"
    DESTINATION usr/share/applications)
install(FILES "${PROJECT_SOURCE_DIR}/resources/pastes.svg"
    DESTINATION usr/share/icons/hicolor/scalable/apps)

# Implicit mkdir permissions are still filtered by umask. Explicit directory
# installs apply chmod, including to parents shared with other packages.
set(pastes_install_directories
    "${PASTES_LINUX_INSTALL_DIR}/translations"
    etc/xdg/autostart usr/bin usr/share/doc/pastes usr/share/applications
    usr/share/icons/hicolor/scalable/apps)
if(PASTES_LINUX_BUNDLE_QT)
    list(APPEND pastes_install_directories "${PASTES_LINUX_INSTALL_DIR}/lib")
    foreach(plugin IN LISTS PASTES_QT_PLUGINS)
        get_filename_component(directory "${plugin}" DIRECTORY)
        list(APPEND pastes_install_directories
            "${PASTES_LINUX_INSTALL_DIR}/plugins/${directory}")
    endforeach()
endif()
set(pastes_parent_directories)
foreach(directory IN LISTS pastes_install_directories)
    while(directory)
        list(APPEND pastes_parent_directories "${directory}")
        get_filename_component(directory "${directory}" DIRECTORY)
    endwhile()
endforeach()
list(REMOVE_DUPLICATES pastes_parent_directories)
foreach(directory IN LISTS pastes_parent_directories)
    install(DIRECTORY DESTINATION "${directory}"
        DIRECTORY_PERMISSIONS ${CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS})
endforeach()

# An explicit generator also supports packaging for another target system.
if(NOT CPACK_GENERATOR)
    include("${CMAKE_CURRENT_LIST_DIR}/select-generator.cmake")
    set(pastes_package_root "/")
    if(CMAKE_CROSSCOMPILING)
        set(pastes_package_root "${CMAKE_SYSROOT}")
    endif()
    pastes_select_linux_generator("${pastes_package_root}" CPACK_GENERATOR)
endif()
message(STATUS "Linux package generators: ${CPACK_GENERATOR}")
set(CPACK_PACKAGE_NAME "pastes")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Jackie Liu")
set(CPACK_PACKAGE_CONTACT "Jackie Liu <liuyun01@kylinos.cn>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform clipboard history manager")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/JackieLiu1/pastes")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_PACKAGE_RELOCATABLE OFF)
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_SET_DESTDIR ON)

if("DEB" IN_LIST CPACK_GENERATOR)
    include("${CMAKE_CURRENT_LIST_DIR}/deb.cmake")
endif()
if("RPM" IN_LIST CPACK_GENERATOR)
    include("${CMAKE_CURRENT_LIST_DIR}/rpm.cmake")
endif()
include(CPack)
