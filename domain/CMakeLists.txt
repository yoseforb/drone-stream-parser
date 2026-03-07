add_library(domain STATIC src/stub.cpp)
target_include_directories(domain PUBLIC include)
target_compile_features(domain PUBLIC cxx_std_20)
