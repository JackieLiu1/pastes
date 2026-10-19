if(APPLE)
    find_package(Python3 3.9 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        pastes_add_contract(macos_signing_contract headless ${Python3_EXECUTABLE}
            -B ${CMAKE_CURRENT_SOURCE_DIR}/macos_signing_test.py)
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pastes_add_contract(linux_packaging_contract headless
        ${CMAKE_COMMAND} -DPASTES_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DPASTES_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/linux_packaging_test.cmake)
endif()
