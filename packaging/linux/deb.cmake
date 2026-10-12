set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
# Use the target architecture even when dpkg is unavailable on the build host.
if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" pastes_target_processor)
    if(pastes_target_processor MATCHES "^(aarch64|arm64)$")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
        endif()
    elseif(pastes_target_processor MATCHES "^(x86_64|amd64)$")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
        endif()
    elseif(pastes_target_processor MATCHES "^(i[3-6]86|x86)$")
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
        endif()
    elseif(NOT CMAKE_CROSSCOMPILING)
        find_program(PASTES_DPKG_EXECUTABLE NAMES dpkg)
        if(PASTES_DPKG_EXECUTABLE)
            execute_process(COMMAND "${PASTES_DPKG_EXECUTABLE}" --print-architecture
                OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE pastes_dpkg_result
                ERROR_QUIET)
            if(NOT pastes_dpkg_result EQUAL 0)
                unset(CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
            endif()
        endif()
    endif()
    if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
        message(FATAL_ERROR
            "Cannot determine the Debian target architecture for ${CMAKE_SYSTEM_PROCESSOR}. "
            "Set -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=<architecture> explicitly.")
    endif()
endif()
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
if(PASTES_LINUX_BUNDLE_QT)
    # Match OrcaTerminal: custom Qt has no dpkg shlibs metadata, and old CPack
    # cannot pass private library directories to dpkg-shlibdeps.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    string(JOIN ", " CPACK_DEBIAN_PACKAGE_DEPENDS
        libsecret-tools libc6 libgcc-s1 libstdc++6 zlib1g libglib2.0-0
        libx11-6 libx11-xcb1 libxi6 libxtst6 libice6 libsm6 libxcb1
        "libxcb-cursor0 | libxcb-cursor-dev" libxcb-glx0 libxcb-icccm4
        libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render0
        libxcb-render-util0 libxcb-shape0 libxcb-shm0 libxcb-sync1
        libxcb-util1 libxcb-xfixes0 libxcb-xkb1 libxkbcommon0
        libxkbcommon-x11-0 libfontconfig1 libfreetype6 libharfbuzz0b
        libegl1 libglx0 libopengl0 libdbus-1-3 "libssl3 | libssl1.1"
        libdouble-conversion3 libpcre2-16-0 libb2-1 libmd4c0 libzstd1
        libpng16-16 "libjpeg-turbo8 | libjpeg62-turbo"
        "libwebp7 | libwebp6" libwebpdemux2 libwebpmux3 libgtk-3-0
        libsqlite3-0 libproxy1v5 libgssapi-krb5-2 libbrotli1)
else()
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # These plugins and the credential helper are loaded at runtime, not linked.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite, libqt6svg6, libsecret-tools")
endif()
