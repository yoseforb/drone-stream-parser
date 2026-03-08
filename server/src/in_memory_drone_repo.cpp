#include "in_memory_drone_repo.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "drone.hpp"

std::optional<Drone>
InMemoryDroneRepository::findById(const std::string& drone_id) {
  auto iter = drones_.find(drone_id);
  if (iter == drones_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

void InMemoryDroneRepository::save(Drone drone) {
  auto id = drone.getId();
  drones_.insert_or_assign(std::move(id), std::move(drone));
}

std::size_t InMemoryDroneRepository::size() const { return drones_.size(); }
