#ifndef ALERT_POLICY_HPP
#define ALERT_POLICY_HPP

inline constexpr double DefaultAltitudeLimit = 120.0;
inline constexpr double DefaultSpeedLimit = 50.0;

struct AlertPolicy {
  double altitude_limit = DefaultAltitudeLimit;
  double speed_limit = DefaultSpeedLimit;
};

#endif // ALERT_POLICY_HPP
