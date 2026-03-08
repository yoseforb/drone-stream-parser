add_executable(drone_client src/main.cpp src/packet_builder.cpp)
target_include_directories(drone_client PRIVATE include)
target_compile_features(drone_client PRIVATE cxx_std_20)
target_link_libraries(drone_client PRIVATE protocol domain common spdlog::spdlog)
