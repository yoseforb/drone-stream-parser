#ifndef FAKE_ALERT_NOTIFIER_HPP
#define FAKE_ALERT_NOTIFIER_HPP

#include <string>
#include <vector>

#include "alert_types.hpp"
#include "i_alert_notifier.hpp"

class FakeAlertNotifier : public IAlertNotifier {
public:
  struct Call {
    std::string drone_id;
    std::vector<AlertTransition> transitions;
  };

  void notify(const std::string& drone_id,
              const std::vector<AlertTransition>& transitions) override {
    calls_.push_back({drone_id, transitions});
  }

  [[nodiscard]] const std::vector<Call>& calls() const { return calls_; }

  [[nodiscard]] bool wasNotified() const { return !calls_.empty(); }

private:
  std::vector<Call> calls_;
};

#endif // FAKE_ALERT_NOTIFIER_HPP
