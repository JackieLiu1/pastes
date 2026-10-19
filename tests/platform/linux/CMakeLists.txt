add_executable(clipboard_linux_test clipboard_linux_test.cpp)
target_link_libraries(clipboard_linux_test PRIVATE pastes_platform pastes_storage PkgConfig::XCB)
pastes_add_contract(clipboard_linux_contract offscreen clipboard_linux_test)

find_program(PASTES_XVFB_RUN_EXECUTABLE xvfb-run)
if(PASTES_XVFB_RUN_EXECUTABLE)
    pastes_add_contract(clipboard_linux_x11_contract headless
        ${PASTES_XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:clipboard_linux_test>
        -platform xcb --isolated-x11)
endif()

find_package(X11 REQUIRED COMPONENTS Xtst)
add_executable(shortcut_linux_test shortcut_linux_test.cpp)
target_link_libraries(shortcut_linux_test PRIVATE pastes_platform X11::X11 X11::Xtst)
pastes_add_contract(shortcut_linux_contract desktop shortcut_linux_test)

add_executable(panel_linux_test panel_linux_test.cpp ${pastes_test_resources})
target_link_libraries(panel_linux_test PRIVATE pastes_ui X11::X11 X11::Xtst)
pastes_add_contract(panel_linux_contract offscreen panel_linux_test)
