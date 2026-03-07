#ifndef I_ALERT_NOTIFIER_HPP
#define I_ALERT_NOTIFIER_HPP

#include <string>
#include <vector>

#include "alert_types.hpp"

class IAlertNotifier {
public:
  virtual ~IAlertNotifier() = default;

  virtual void notify(const std::string& drone_id,
                      const std::vector<AlertTransition>& transitions) = 0;
};

#endif // I_ALERT_NOTIFIER_HPP
