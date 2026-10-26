add_executable(historyview_test historyview_test.cpp ${pastes_test_resources})
target_link_libraries(historyview_test PRIVATE pastes_ui)
pastes_add_contract(historyview_contract offscreen historyview_test)

add_executable(mainwindow_test mainwindow_test.cpp ${pastes_test_resources})
target_link_libraries(mainwindow_test PRIVATE pastes_ui)
pastes_add_contract(mainwindow_contract offscreen mainwindow_test)

if(WIN32)
    # Actual keyboard focus needs a desktop; keep daily component runs offscreen.
    pastes_add_contract(historyselection_windows_contract desktop
        mainwindow_test --selection-only)
endif()

add_executable(settings_test settings_test.cpp ${pastes_test_resources})
target_link_libraries(settings_test PRIVATE pastes_ui)
pastes_add_contract(settings_contract offscreen settings_test)
