add_library(protocol STATIC src/stub.cpp)
target_include_directories(protocol PUBLIC include)
target_compile_features(protocol PUBLIC cxx_std_20)
target_link_libraries(protocol PUBLIC domain)
