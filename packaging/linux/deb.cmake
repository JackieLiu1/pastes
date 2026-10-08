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
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# These plugins and the credential helper are loaded at runtime, not linked.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite, libqt6svg6, libsecret-tools")
