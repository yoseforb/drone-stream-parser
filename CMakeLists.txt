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

# ---- Format patterns (used by auto-format and lint targets) ----
set(
    FORMAT_PATTERNS
    domain/src/*.cpp domain/include/*.hpp
    protocol/src/*.cpp protocol/include/*.hpp
    server/src/*.cpp server/include/*.hpp
    client/src/*.cpp client/include/*.hpp
    common/include/*.hpp
    tests/src/*.cpp tests/domain/*.cpp tests/protocol/*.cpp tests/common/*.cpp
)

# Expand glob patterns for auto-format target
file(GLOB_RECURSE SOURCE_FILES
    domain/src/*.cpp domain/include/*.hpp
    protocol/src/*.cpp protocol/include/*.hpp
    server/src/*.cpp server/include/*.hpp
    client/src/*.cpp client/include/*.hpp
    common/include/*.hpp
)

# ---- clang-tidy (prefer clang-tidy-cache, fall back to clang-tidy) ----

option(ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_CACHE_EXE NAMES clang-tidy-cache)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_CACHE_EXE)
    set(CMAKE_CXX_CLANG_TIDY
        "${CLANG_TIDY_CACHE_EXE};--header-filter=^${PROJECT_SOURCE_DIR}/")
    message(STATUS "clang-tidy: using clang-tidy-cache (${CLANG_TIDY_CACHE_EXE})")
  elseif(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY
        "${CLANG_TIDY_EXE};--header-filter=^${PROJECT_SOURCE_DIR}/")
    message(STATUS "clang-tidy: using clang-tidy (${CLANG_TIDY_EXE})")
  else()
    message(WARNING "ENABLE_CLANG_TIDY is ON but neither clang-tidy-cache nor clang-tidy found")
  endif()
endif()

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

# Auto-format all source files before compilation
add_custom_target(
    auto-format-sources ALL
    COMMAND clang-format -i ${SOURCE_FILES}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Auto-formatting source code..."
)

add_subdirectory(common)
add_subdirectory(domain)
add_subdirectory(protocol)
add_subdirectory(server)
add_subdirectory(client)

# Make all targets depend on auto-formatting
add_dependencies(common auto-format-sources)
add_dependencies(domain auto-format-sources)
add_dependencies(protocol auto-format-sources)
add_dependencies(drone_server auto-format-sources)
add_dependencies(drone_client auto-format-sources)

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
