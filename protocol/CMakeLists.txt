add_library(protocol STATIC src/crc16.cpp src/packet_deserializer.cpp src/packet_serializer.cpp src/stream_parser.cpp src/telemetry_parser.cpp)
target_include_directories(protocol PUBLIC include)
target_compile_features(protocol PUBLIC cxx_std_20)
target_link_libraries(protocol PUBLIC domain)
