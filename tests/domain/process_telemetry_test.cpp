#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "alert_policy.hpp"
#include "alert_types.hpp"
#include "drone.hpp"
#include "i_alert_notifier.hpp"
#include "i_drone_repository.hpp"
#include "process_telemetry.hpp"

namespace {

class MockDroneRepository : public IDroneRepository {
public:
  MOCK_METHOD(std::optional<Drone>, findById, (const std::string&), (override));
  MOCK_METHOD(void, save, (const Drone&), (override));
};

class MockAlertNotifier : public IAlertNotifier {
public:
  MOCK_METHOD(void, notify,
              (const std::string&, const std::vector<AlertTransition>&),
              (override));
};

} // namespace

TEST(ProcessTelemetryTest, CanConstruct) {
  MockDroneRepository repository;
  MockAlertNotifier notifier;
  ProcessTelemetry const Sut(repository, notifier, AlertPolicy{});
  static_cast<void>(Sut);
  EXPECT_TRUE(true);
}
