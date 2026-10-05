# CPack supplies the same package target as the native Windows/macOS packagers.
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME copyright)
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "pastes")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Jackie Liu")
set(CPACK_PACKAGE_CONTACT "Jackie Liu <liuyun01@kylinos.cn>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform clipboard history manager")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/JackieLiu1/pastes")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
# Stage absolute install destinations such as /etc/xdg/autostart as well.
set(CPACK_SET_DESTDIR ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# These plugins and the credential helper are loaded at runtime, not linked.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite, libqt6svg6, libsecret-tools")
include(CPack)
