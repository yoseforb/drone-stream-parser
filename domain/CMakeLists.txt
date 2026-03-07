add_library(domain STATIC
  src/drone.cpp
  src/process_telemetry.cpp
)
target_include_directories(domain PUBLIC include)
target_compile_features(domain PUBLIC cxx_std_20)
