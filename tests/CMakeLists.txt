project(drone-stream-parserTests LANGUAGES CXX)

add_executable(tests
  domain/drone_test.cpp
  domain/process_telemetry_test.cpp
  protocol/crc16_test.cpp
  protocol/serializer_test.cpp
  protocol/parser_test.cpp
)
target_link_libraries(tests PRIVATE protocol domain GTest::gtest_main GTest::gmock)
target_compile_features(tests PRIVATE cxx_std_20)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
include(GoogleTest)
gtest_discover_tests(tests)

add_folders(Test)
