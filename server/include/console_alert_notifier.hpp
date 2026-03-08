#ifndef CONSOLE_ALERT_NOTIFIER_HPP
#define CONSOLE_ALERT_NOTIFIER_HPP

#include <string>
#include <vector>

#include "alert_types.hpp"
#include "i_alert_notifier.hpp"

class ConsoleAlertNotifier : public IAlertNotifier {
public:
  void notify(const std::string& drone_id,
              const std::vector<AlertTransition>& transitions) override;
};

#endif // CONSOLE_ALERT_NOTIFIER_HPP
