if(APPLE)
    set(PASTES_MACOS_ICON "${PROJECT_SOURCE_DIR}/resources/pastes.icns")
    set_source_files_properties(${PASTES_MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()

qt_add_executable(pastes main.cpp "${PROJECT_SOURCE_DIR}/sources.qrc" ${PASTES_MACOS_ICON})
# Keep the executable, translations and Windows database beside the build root.
set_target_properties(pastes PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(pastes PRIVATE pastes_ui pastes_storage pastes_sync SingleApplication::SingleApplication)
add_dependencies(pastes translations)

if(WIN32)
    set_target_properties(pastes PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(pastes PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/platform/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME Pastes
        MACOSX_BUNDLE_ICON_FILE pastes.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.JackieLiu1.pastes
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
    # Info.plist changes invalidate the bundle signature even without C++ edits.
    set_property(TARGET pastes APPEND PROPERTY LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/platform/macos/Info.plist.in"
        "$<TARGET_BUNDLE_DIR:pastes>/Contents/Info.plist")
    set_source_files_properties(${QM_FILES} PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(pastes PRIVATE ${QM_FILES})
    set_property(TARGET pastes APPEND PROPERTY LINK_DEPENDS ${QM_FILES})
    add_custom_command(TARGET pastes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            $<TARGET_FILE_DIR:pastes>/Pastes_zh_CN.qm
        VERBATIM
    )
    # ServiceManagement requires a signed bundle, including its final resources.
    set(PASTES_MACOS_SIGNING_IDENTITY "-" CACHE STRING
        "macOS certificate identity (- for ad-hoc signing)")
    find_program(PASTES_CODESIGN_EXECUTABLE codesign)
    if(NOT PASTES_CODESIGN_EXECUTABLE)
        message(FATAL_ERROR "codesign is required for macOS login items")
    endif()
    add_custom_command(TARGET pastes POST_BUILD
        COMMAND ${PASTES_CODESIGN_EXECUTABLE} --force --sign
            "${PASTES_MACOS_SIGNING_IDENTITY}" $<TARGET_BUNDLE_DIR:pastes>
        VERBATIM
    )
endif()
