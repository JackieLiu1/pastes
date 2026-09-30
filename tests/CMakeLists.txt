add_executable(history_test history_test.cpp)
target_link_libraries(history_test PRIVATE pastes_application)
add_test(NAME history_contract COMMAND history_test)

add_executable(database_test database_test.cpp)
target_link_libraries(database_test PRIVATE pastes_storage)
add_test(NAME database_contract COMMAND database_test)

add_executable(clipboard_test clipboard_test.cpp)
target_link_libraries(clipboard_test PRIVATE pastes_application)
add_test(NAME clipboard_contract COMMAND clipboard_test)

add_executable(diagnosticlog_test diagnosticlog_test.cpp)
target_link_libraries(diagnosticlog_test PRIVATE pastes_platform)
add_test(NAME diagnostics_contract COMMAND diagnosticlog_test)
set_tests_properties(diagnostics_contract PROPERTIES TIMEOUT 30)

if(APPLE)
    add_executable(clipboard_macos_test clipboard_macos_test.mm)
    target_link_libraries(clipboard_macos_test PRIVATE pastes_platform)
    add_test(NAME clipboard_macos_contract COMMAND clipboard_macos_test)
    set_tests_properties(clipboard_macos_contract PROPERTIES TIMEOUT 30)
    find_package(Python3 3.9 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME macos_signing_contract
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/macos_signing_test.py)
        set_tests_properties(macos_signing_contract PROPERTIES TIMEOUT 30)
    endif()
endif()

add_executable(historyview_test historyview_test.cpp ../sources.qrc)
target_link_libraries(historyview_test PRIVATE pastes_ui)
add_test(NAME historyview_contract COMMAND historyview_test)

# These test injected adapters and widget commands, not native window focus.
set_tests_properties(clipboard_contract historyview_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(history_contract database_contract clipboard_contract
    historyview_contract PROPERTIES TIMEOUT 30)

if(WIN32)
    # CTest child processes need the Qt/compiler runtime directory even when
    # launched from an IDE or a shell without the toolchain on its PATH.
    set(test_path "$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    string(REPLACE ";" "\\;" test_path "${test_path}")
    set_property(TEST history_contract database_contract clipboard_contract diagnostics_contract
        historyview_contract APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
endif()

add_test(NAME architecture_dependencies
    COMMAND ${CMAKE_COMMAND} -DPASTES_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/check-dependencies.cmake)

add_executable(webdav_test webdav_test.cpp)
target_link_libraries(webdav_test PRIVATE pastes_sync pastes_platform Qt6::Network)
add_test(NAME webdav_contract COMMAND webdav_test)
set_tests_properties(webdav_contract PROPERTIES TIMEOUT 60)
if(WIN32)
    set_property(TEST webdav_contract APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
endif()

add_executable(settings_test settings_test.cpp ../sources.qrc)
target_link_libraries(settings_test PRIVATE pastes_ui)
add_test(NAME settings_contract COMMAND settings_test)
set_tests_properties(settings_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
if(WIN32)
    set_property(TEST settings_contract APPEND PROPERTY ENVIRONMENT "PATH=${test_path}")
endif()
