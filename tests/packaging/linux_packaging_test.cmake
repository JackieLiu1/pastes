cmake_minimum_required(VERSION 3.16)
include("${PASTES_SOURCE_DIR}/packaging/linux/select-generator.cmake")
set(fixtures "${PASTES_TEST_BINARY_DIR}/linux-packaging-fixtures")
file(REMOVE_RECURSE "${fixtures}")

function(check_distribution name release database expected)
    set(root "${fixtures}/${name}")
    file(MAKE_DIRECTORY "${root}/etc")
    file(WRITE "${root}/etc/os-release" "${release}")
    if(database)
        get_filename_component(directory "${root}/${database}" DIRECTORY)
        file(MAKE_DIRECTORY "${directory}")
        file(WRITE "${root}/${database}" "")
    endif()
    pastes_select_linux_generator("${root}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${name}: expected ${expected}, got ${actual}")
    endif()
endfunction()

# Distribution metadata takes precedence over a secondary package database.
check_distribution(ubuntu "ID=ubuntu\nID_LIKE=debian\n"
    "var/lib/rpm/Packages.db" DEB)
check_distribution(fedora "ID=fedora\n"
    "var/lib/dpkg/status" RPM)
check_distribution(derivative "ID=custom\nID_LIKE=\"rhel fedora\"\n" "" RPM)
check_distribution(suse "ID=opensuse-tumbleweed\n" "" RPM)
# Kylin's shared distribution ID must not determine the package format alone.
check_distribution(kylin-server "ID=kylin\n" "var/lib/rpm/Packages.db" RPM)
check_distribution(kylin-desktop "ID=kylin\n" "var/lib/dpkg/status" DEB)
check_distribution(rpm-sqlite "ID=custom\n" "usr/lib/sysimage/rpm/rpmdb.sqlite" RPM)
check_distribution(rpm-legacy "ID=custom\n" "var/lib/rpm/Packages" RPM)
check_distribution(unknown "ID=custom\n" "" TGZ)

# An absent target sysroot must not cause cross-builds to inspect the host.
pastes_select_linux_generator("" actual)
if(NOT actual STREQUAL "TGZ")
    message(FATAL_ERROR "Missing target sysroot used the host package format")
endif()
file(REMOVE_RECURSE "${fixtures}")
message(STATUS "Linux package format selection verified")
