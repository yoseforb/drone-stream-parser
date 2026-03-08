#ifndef FAKE_DRONE_REPOSITORY_HPP
#define FAKE_DRONE_REPOSITORY_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "drone.hpp"
#include "i_drone_repository.hpp"

class FakeDroneRepository : public IDroneRepository {
public:
  std::optional<Drone> findById(const std::string& drone_id) override {
    auto iter = drones_.find(drone_id);
    if (iter == drones_.end()) {
      return std::nullopt;
    }
    return iter->second;
  }

  void save(Drone drone) override {
    auto id = drone.getId();
    drones_.insert_or_assign(std::move(id), std::move(drone));
  }

  [[nodiscard]] bool contains(const std::string& drone_id) const {
    return drones_.contains(drone_id);
  }

  [[nodiscard]] std::size_t size() const { return drones_.size(); }

private:
  std::unordered_map<std::string, Drone> drones_;
};

#endif // FAKE_DRONE_REPOSITORY_HPP
