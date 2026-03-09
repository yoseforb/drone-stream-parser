project(drone-stream-parserTests LANGUAGES CXX)

add_executable(tests
  domain/drone_test.cpp
  domain/process_telemetry_test.cpp
  protocol/crc16_test.cpp
  protocol/serializer_test.cpp
  protocol/deserializer_test.cpp
  protocol/parser_test.cpp
  common/blocking_queue_test.cpp
  client/packet_builder_test.cpp
  ${CMAKE_SOURCE_DIR}/client/src/packet_builder.cpp
)
target_link_libraries(tests PRIVATE protocol domain common GTest::gtest_main GTest::gmock)
target_compile_features(tests PRIVATE cxx_std_20)
target_include_directories(tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/client/include
)

enable_testing()
include(GoogleTest)
gtest_discover_tests(tests)

add_folders(Test)
