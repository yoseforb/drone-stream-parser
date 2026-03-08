#ifndef IN_MEMORY_DRONE_REPO_HPP
#define IN_MEMORY_DRONE_REPO_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "drone.hpp"
#include "i_drone_repository.hpp"

// Thread safety: no mutex. This repository is accessed exclusively from the
// process thread (Stage 3 of the pipeline). The single-consumer guarantee is
// structural — enforced by the data flow design (ADR-006), not a runtime
// invariant that requires locking.
class InMemoryDroneRepository : public IDroneRepository {
public:
  std::optional<Drone> findById(const std::string& drone_id) override;
  void save(Drone drone) override;

  [[nodiscard]] std::size_t size() const;

private:
  std::unordered_map<std::string, Drone> drones_;
};

#endif // IN_MEMORY_DRONE_REPO_HPP
