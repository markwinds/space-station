if (NOT BUILD_CONFIG STREQUAL "Release")
    return()
endif()

if (TARGET_PLATFORM STREQUAL "Windows")
    message(STATUS "Skipping strip for ${TARGET_NAME}: MSVC Release symbols are emitted separately")
    return()
endif()

if (NOT STRIP_TOOL OR NOT EXISTS "${STRIP_TOOL}")
    message(FATAL_ERROR "Release strip tool was not found: ${STRIP_TOOL}")
endif()

if (NOT TARGET_FILE OR NOT EXISTS "${TARGET_FILE}")
    message(FATAL_ERROR "Release executable was not found: ${TARGET_FILE}")
endif()

file(SIZE "${TARGET_FILE}" size_before)

if (TARGET_PLATFORM STREQUAL "Darwin")
    execute_process(
            COMMAND "${STRIP_TOOL}" -S -x "${TARGET_FILE}"
            RESULT_VARIABLE strip_result
            ERROR_VARIABLE strip_error)
else()
    execute_process(
            COMMAND "${STRIP_TOOL}" --strip-unneeded "${TARGET_FILE}"
            RESULT_VARIABLE strip_result
            ERROR_VARIABLE strip_error)
endif()

if (NOT strip_result EQUAL 0)
    message(FATAL_ERROR "Failed to strip ${TARGET_NAME}: ${strip_error}")
endif()

file(SIZE "${TARGET_FILE}" size_after)
math(EXPR saved_bytes "${size_before} - ${size_after}")
message(STATUS "Stripped ${TARGET_NAME}: ${size_before} -> ${size_after} bytes (saved ${saved_bytes})")
