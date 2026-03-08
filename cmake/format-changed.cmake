# Format only git-changed source files matching our patterns
execute_process(
    COMMAND git diff --name-only --diff-filter=ACMR HEAD
    WORKING_DIRECTORY "${PROJECT_DIR}"
    OUTPUT_VARIABLE CHANGED_FILES
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
)

# Also include untracked files
execute_process(
    COMMAND git ls-files --others --exclude-standard
    WORKING_DIRECTORY "${PROJECT_DIR}"
    OUTPUT_VARIABLE UNTRACKED_FILES
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Combine
if(UNTRACKED_FILES)
    if(CHANGED_FILES)
        set(CHANGED_FILES "${CHANGED_FILES}\n${UNTRACKED_FILES}")
    else()
        set(CHANGED_FILES "${UNTRACKED_FILES}")
    endif()
endif()

if(NOT CHANGED_FILES OR CHANGED_FILES STREQUAL "")
    message(STATUS "format-changed: no changed files")
    return()
endif()

# Convert newline-separated string to list
string(REPLACE "\n" ";" FILE_LIST "${CHANGED_FILES}")

# Filter to only .cpp and .hpp files
set(FORMAT_FILES "")
foreach(f IN LISTS FILE_LIST)
    if(f MATCHES "\\.(cpp|hpp)$")
        list(APPEND FORMAT_FILES "${PROJECT_DIR}/${f}")
    endif()
endforeach()

if(NOT FORMAT_FILES)
    message(STATUS "format-changed: no .cpp/.hpp files changed")
    return()
endif()

# Run clang-format on changed files only
list(LENGTH FORMAT_FILES FILE_COUNT)
message(STATUS "format-changed: formatting ${FILE_COUNT} file(s)")
execute_process(
    COMMAND "${CLANG_FORMAT_EXE}" -i ${FORMAT_FILES}
    WORKING_DIRECTORY "${PROJECT_DIR}"
    RESULT_VARIABLE FORMAT_RESULT
)
if(NOT FORMAT_RESULT EQUAL 0)
    message(WARNING "clang-format failed with exit code ${FORMAT_RESULT}")
endif()
