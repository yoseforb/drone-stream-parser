#include "console_alert_notifier.hpp"

#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "alert_types.hpp"

void ConsoleAlertNotifier::notify(
    const std::string& drone_id,
    const std::vector<AlertTransition>& transitions) {
  for (const auto& transition : transitions) {
    const char* type_str =
        (transition.type == AlertType::ALTITUDE) ? "ALTITUDE" : "SPEED";
    const char* state_str = transition.entered ? "ENTERED" : "CLEARED";
    spdlog::warn("[ALERT] drone={} type={} state={}", drone_id, type_str,
                 state_str);
  }
}
