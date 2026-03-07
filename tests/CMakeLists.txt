project(drone-stream-parserTests LANGUAGES CXX)

add_executable(tests src/stub_test.cpp)
target_link_libraries(tests PRIVATE protocol domain GTest::gtest_main GTest::gmock)
target_compile_features(tests PRIVATE cxx_std_20)

enable_testing()
include(GoogleTest)
gtest_discover_tests(tests)

add_folders(Test)
