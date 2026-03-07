include(cmake/folders.cmake)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

add_custom_target(
    run-server
    COMMAND drone_server
    VERBATIM
)
add_dependencies(run-server drone_server)

add_custom_target(
    run-client
    COMMAND drone_client
    VERBATIM
)
add_dependencies(run-client drone_client)

option(ENABLE_COVERAGE "Enable coverage support separate from CTest's" OFF)
if(ENABLE_COVERAGE)
  include(cmake/coverage.cmake)
endif()

include(cmake/lint-targets.cmake)

add_folders(Project)
