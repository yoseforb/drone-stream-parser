find_package(Threads REQUIRED)
add_executable(drone_server
  src/signal_handler.cpp
  src/in_memory_drone_repo.cpp
  src/console_alert_notifier.cpp
  src/main.cpp
)
target_include_directories(drone_server PRIVATE include)
target_compile_features(drone_server PRIVATE cxx_std_20)
target_link_libraries(drone_server PRIVATE protocol domain common spdlog::spdlog Threads::Threads)
