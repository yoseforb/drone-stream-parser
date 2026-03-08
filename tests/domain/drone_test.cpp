#include <cstdint>
#include <gtest/gtest.h>

#include "alert_policy.hpp"
#include "alert_types.hpp"
#include "drone.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {

constexpr double AboveAltitudeLimit = 125.0;
constexpr double BelowAltitudeLimit = 100.0;
constexpr double WellBelowAltitudeLimit = 50.0;
constexpr double FurtherAboveAltitudeLimit = 130.0;
constexpr double AboveSpeedLimit = 60.0;
constexpr double BelowSpeedLimit = 30.0;
constexpr double WellBelowSpeedLimit = 20.0;
constexpr uint64_t DefaultTimestamp = 1000;

Telemetry makeTelemetry(double altitude, double speed) {
  return {.drone_id = "D001",
          .latitude = 0.0,
          .longitude = 0.0,
          .altitude = altitude,
          .speed = speed,
          .timestamp = DefaultTimestamp};
}

} // namespace

// NOLINTEND(readability-magic-numbers)

TEST(DroneTest, NewDroneHasEmptyAlertState) {
  Drone const Drone("D001");
  EXPECT_TRUE(Drone.getAlertState().empty());
}

TEST(DroneTest, GetIdReturnsConstructorId) {
  Drone const Drone("D001");
  EXPECT_EQ(Drone.getId(), "D001");
}

TEST(DroneTest, AltitudeAboveLimitEntersAlert) {
  Drone drone("D001");
  AlertPolicy const Policy;

  auto transitions =
      drone.updateFrom(makeTelemetry(AboveAltitudeLimit, 0.0), Policy);

  ASSERT_EQ(transitions.size(), 1U);
  EXPECT_EQ(transitions[0].type, AlertType::ALTITUDE);
  EXPECT_TRUE(transitions[0].entered);
  EXPECT_TRUE(drone.getAlertState().contains(AlertType::ALTITUDE));
}

TEST(DroneTest, AltitudeBelowLimitAfterAlertClearsAlert) {
  Drone drone("D001");
  AlertPolicy const Policy;

  drone.updateFrom(makeTelemetry(AboveAltitudeLimit, 0.0), Policy);
  auto transitions =
      drone.updateFrom(makeTelemetry(BelowAltitudeLimit, 0.0), Policy);

  ASSERT_EQ(transitions.size(), 1U);
  EXPECT_EQ(transitions[0].type, AlertType::ALTITUDE);
  EXPECT_FALSE(transitions[0].entered);
  EXPECT_TRUE(drone.getAlertState().empty());
}

TEST(DroneTest, NoTransitionWhenAlreadyInAlert) {
  Drone drone("D001");
  AlertPolicy const Policy;

  drone.updateFrom(makeTelemetry(AboveAltitudeLimit, 0.0), Policy);
  auto transitions =
      drone.updateFrom(makeTelemetry(FurtherAboveAltitudeLimit, 0.0), Policy);

  EXPECT_TRUE(transitions.empty());
}

TEST(DroneTest, SpeedAboveLimitEntersAlert) {
  Drone drone("D001");
  AlertPolicy const Policy;

  auto transitions =
      drone.updateFrom(makeTelemetry(0.0, AboveSpeedLimit), Policy);

  ASSERT_EQ(transitions.size(), 1U);
  EXPECT_EQ(transitions[0].type, AlertType::SPEED);
  EXPECT_TRUE(transitions[0].entered);
}

TEST(DroneTest, SpeedBelowLimitClearsAlert) {
  Drone drone("D001");
  AlertPolicy const Policy;

  drone.updateFrom(makeTelemetry(0.0, AboveSpeedLimit), Policy);
  auto transitions =
      drone.updateFrom(makeTelemetry(0.0, BelowSpeedLimit), Policy);

  ASSERT_EQ(transitions.size(), 1U);
  EXPECT_EQ(transitions[0].type, AlertType::SPEED);
  EXPECT_FALSE(transitions[0].entered);
}

TEST(DroneTest, BothAlertsActiveSimultaneously) {
  Drone drone("D001");
  AlertPolicy const Policy;

  auto transitions = drone.updateFrom(
      makeTelemetry(AboveAltitudeLimit, AboveSpeedLimit), Policy);

  ASSERT_EQ(transitions.size(), 2U);
  EXPECT_TRUE(drone.getAlertState().contains(AlertType::ALTITUDE));
  EXPECT_TRUE(drone.getAlertState().contains(AlertType::SPEED));
}

TEST(DroneTest, NoTransitionWhenBelowBothLimits) {
  Drone drone("D001");
  AlertPolicy const Policy;

  auto transitions = drone.updateFrom(
      makeTelemetry(WellBelowAltitudeLimit, WellBelowSpeedLimit), Policy);

  EXPECT_TRUE(transitions.empty());
}
