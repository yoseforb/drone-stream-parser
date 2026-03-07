#include <cstdint>
#include <gtest/gtest.h>
#include <string>

#include "alert_policy.hpp"
#include "alert_types.hpp"
#include "domain/fakes/fake_alert_notifier.hpp"
#include "domain/fakes/fake_drone_repository.hpp"
#include "drone.hpp"
#include "process_telemetry.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {

constexpr double AboveAltitudeLimit = 125.0;
constexpr double BelowAltitudeLimit = 50.0;
constexpr double AboveSpeedLimit = 60.0;
constexpr double SafeSpeed = 10.0;
constexpr uint64_t DefaultTimestamp = 1000;

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
Telemetry makeTelemetry(const std::string& id, double altitude, double speed) {
  return {.drone_id = id,
          .latitude = 0.0,
          .longitude = 0.0,
          .altitude = altitude,
          .speed = speed,
          .timestamp = DefaultTimestamp};
}

} // namespace

// NOLINTEND(readability-magic-numbers)

TEST(ProcessTelemetryTest, NewDroneCreatedAndSavedOnFirstTelemetry) {
  FakeDroneRepository repo;
  FakeAlertNotifier notifier;
  ProcessTelemetry sut(repo, notifier, AlertPolicy{});

  sut.execute(makeTelemetry("D001", BelowAltitudeLimit, SafeSpeed));

  EXPECT_TRUE(repo.contains("D001"));
}

TEST(ProcessTelemetryTest, ExistingDroneRetrievedUpdatedAndSaved) {
  FakeDroneRepository repo;
  FakeAlertNotifier notifier;
  repo.save(Drone("D001"));
  ProcessTelemetry sut(repo, notifier, AlertPolicy{});

  sut.execute(makeTelemetry("D001", AboveAltitudeLimit, SafeSpeed));

  ASSERT_TRUE(repo.contains("D001"));
  auto drone = repo.findById("D001");
  ASSERT_TRUE(drone.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_TRUE(drone->getAlertState().contains(AlertType::ALTITUDE));
}

TEST(ProcessTelemetryTest, NotifierCalledWhenAlertEntered) {
  FakeDroneRepository repo;
  FakeAlertNotifier notifier;
  ProcessTelemetry sut(repo, notifier, AlertPolicy{});

  sut.execute(makeTelemetry("D001", AboveAltitudeLimit, SafeSpeed));

  ASSERT_EQ(notifier.calls().size(), 1U);
  EXPECT_EQ(notifier.calls()[0].drone_id, "D001");
  ASSERT_FALSE(notifier.calls()[0].transitions.empty());

  bool found_altitude = false;
  for (const auto& transition : notifier.calls()[0].transitions) {
    if (transition.type == AlertType::ALTITUDE && transition.entered) {
      found_altitude = true;
    }
  }
  EXPECT_TRUE(found_altitude);
}

TEST(ProcessTelemetryTest, NotifierNotCalledWhenNoTransition) {
  FakeDroneRepository repo;
  FakeAlertNotifier notifier;
  ProcessTelemetry sut(repo, notifier, AlertPolicy{});

  sut.execute(makeTelemetry("D001", BelowAltitudeLimit, SafeSpeed));

  EXPECT_FALSE(notifier.wasNotified());
}

TEST(ProcessTelemetryTest, NotifierNotCalledOnRepeatedAlertAboveThreshold) {
  FakeDroneRepository repo;
  FakeAlertNotifier notifier;
  ProcessTelemetry sut(repo, notifier, AlertPolicy{});

  sut.execute(makeTelemetry("D001", AboveAltitudeLimit, SafeSpeed));
  sut.execute(makeTelemetry("D001", AboveAltitudeLimit, SafeSpeed));

  EXPECT_EQ(notifier.calls().size(), 1U);
}

TEST(ProcessTelemetryTest, NotifierCalledWhenAlertCleared) {
  FakeDroneRepository repo;
  FakeAlertNotifier notifier;
  ProcessTelemetry sut(repo, notifier, AlertPolicy{});

  sut.execute(makeTelemetry("D001", AboveAltitudeLimit, SafeSpeed));
  sut.execute(makeTelemetry("D001", BelowAltitudeLimit, SafeSpeed));

  ASSERT_EQ(notifier.calls().size(), 2U);

  bool found_cleared = false;
  for (const auto& transition : notifier.calls()[1].transitions) {
    if (transition.type == AlertType::ALTITUDE && !transition.entered) {
      found_cleared = true;
    }
  }
  EXPECT_TRUE(found_cleared);
}

TEST(ProcessTelemetryTest, MultipleTransitionsForwardedInOneCall) {
  FakeDroneRepository repo;
  FakeAlertNotifier notifier;
  ProcessTelemetry sut(repo, notifier, AlertPolicy{});

  sut.execute(makeTelemetry("D001", AboveAltitudeLimit, AboveSpeedLimit));

  ASSERT_EQ(notifier.calls().size(), 1U);
  EXPECT_EQ(notifier.calls()[0].transitions.size(), 2U);
}
