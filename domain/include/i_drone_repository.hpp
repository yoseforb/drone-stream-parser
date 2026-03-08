#ifndef I_DRONE_REPOSITORY_HPP
#define I_DRONE_REPOSITORY_HPP

#include <optional>
#include <string>

#include "drone.hpp"

class IDroneRepository {
public:
  virtual ~IDroneRepository() = default;

  virtual std::optional<Drone> findById(const std::string& drone_id) = 0;
  virtual void save(Drone drone) = 0;
};

#endif // I_DRONE_REPOSITORY_HPP
