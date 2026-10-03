set(PASTES_GIT_REVISION "")

if(GIT_EXECUTABLE AND EXISTS "${PASTES_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PASTES_SOURCE_DIR}"
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(revision_result EQUAL 0 AND revision MATCHES "^[0-9a-f]+$")
        set(PASTES_GIT_REVISION "${revision}")
    endif()
endif()

# configure_file only rewrites the header when its contents change.
configure_file("${CMAKE_CURRENT_LIST_DIR}/build-version.h.in"
    "${PASTES_OUTPUT_FILE}" @ONLY)
