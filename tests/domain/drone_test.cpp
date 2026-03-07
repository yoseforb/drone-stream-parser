#include <gtest/gtest.h>

#include "drone.hpp"

TEST(DroneTest, NewDroneHasEmptyAlertState) {
  Drone const Drone("D001");
  EXPECT_TRUE(Drone.getAlertState().empty());
}

TEST(DroneTest, GetIdReturnsConstructorId) {
  Drone const Drone("D001");
  EXPECT_EQ(Drone.getId(), "D001");
}
