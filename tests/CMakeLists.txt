# Keep test executable locations stable while sources follow module ownership.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(pastes_test_working_directory "${CMAKE_CURRENT_BINARY_DIR}")
set(pastes_test_resources "${PROJECT_SOURCE_DIR}/sources.qrc")
if(WIN32)
    set(pastes_test_path "$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    string(REPLACE ";" "\\;" pastes_test_path "${pastes_test_path}")
endif()

# Every contract declares whether it needs a real desktop. Offscreen widgets
# and isolated Xvfb servers belong to the headless group.
function(pastes_add_contract name mode)
    if(NOT mode MATCHES "^(headless|offscreen|desktop)$")
        message(FATAL_ERROR "Unknown execution environment for ${name}: ${mode}")
    endif()
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 30 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
        WORKING_DIRECTORY "${pastes_test_working_directory}")
    if(mode STREQUAL "offscreen")
        set_tests_properties(${name} PROPERTIES LABELS headless)
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        set_tests_properties(${name} PROPERTIES LABELS ${mode})
    endif()
    if(WIN32)
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "PATH=${pastes_test_path}")
    endif()
endfunction()

add_subdirectory(core)
add_subdirectory(application)
add_subdirectory(storage)
add_subdirectory(sync)
add_subdirectory(ui)
add_subdirectory(platform)
add_subdirectory(packaging)

pastes_add_contract(architecture_dependencies headless
    ${CMAKE_COMMAND} -DPASTES_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/cmake/check-dependencies.cmake)
