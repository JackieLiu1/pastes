add_executable(clipboard_macos_test clipboard_macos_test.mm)
target_link_libraries(clipboard_macos_test PRIVATE pastes_platform pastes_storage)
pastes_add_contract(clipboard_macos_contract headless clipboard_macos_test)

add_executable(trayicon_macos_test MACOSX_BUNDLE
    trayicon_macos_test.mm ${pastes_test_resources})
set_target_properties(trayicon_macos_test PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/platform/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME PastesTrayTests
    MACOSX_BUNDLE_GUI_IDENTIFIER io.pastes.validation.tray
)
target_link_libraries(trayicon_macos_test PRIVATE pastes_ui)
add_dependencies(trayicon_macos_test pastes)
pastes_add_contract(trayicon_macos_contract desktop trayicon_macos_test
    $<TARGET_BUNDLE_DIR:pastes>)
