cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

project(
    drone-stream-parser
    VERSION 0.1.0
    DESCRIPTION "Multi-threaded C++20 streaming parser for drone telemetry over TCP"
    LANGUAGES CXX
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

# ---- Dependencies ----

find_package(Threads REQUIRED)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    SYSTEM
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    SYSTEM
)

FetchContent_MakeAvailable(googletest spdlog)

# Suppress warnings for third-party targets — our strict flags must not
# apply to code we do not own.
foreach(_target gtest gtest_main gmock gmock_main spdlog)
  if(TARGET ${_target})
    target_compile_options(${_target} PRIVATE -w)
    set_target_properties(${_target} PROPERTIES CXX_CLANG_TIDY "")
  endif()
endforeach()

# ---- Targets ----

add_subdirectory(common)
add_subdirectory(domain)
add_subdirectory(protocol)
add_subdirectory(server)
add_subdirectory(client)

# ---- Developer mode ----

if(NOT drone-stream-parser_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of drone-stream-parser"
  )
endif()

include(cmake/dev-mode.cmake)
