# Phase 2: Domain Boundary — Implementation Plan

**Date:** 2026-03-07
**Status:** Active
**Depends on:** Phase 1 (project scaffolding complete)
**Blocks:** Phase 3 (Protocol uses `Telemetry` from domain)

---

## Overview

Phase 2 establishes the domain boundary: all business logic for drone telemetry processing,
alert detection, and state management. This boundary has zero external dependencies — pure
C++20 standard library only. Every component is fully TDD'd before the next component is
started.

**Completion criterion:** `cmake --build --preset=dev && ctest --preset=dev` passes with all
domain tests GREEN and zero warnings.

---

## Dependency Order Within Phase 2

The items below MUST be implemented in this exact order. Each depends on what is above it.

```
2a: Value Objects       (Telemetry, AlertType/Transition, AlertPolicy)
       |
2b: Port Interfaces     (IDroneRepository, IAlertNotifier — depend on Telemetry + Drone + AlertTransition)
       |
2c: Test Fakes          (FakeDroneRepository, FakeAlertNotifier — implement the interfaces)
       |
2d: Drone Entity        (TDD: header -> stub -> RED tests -> GREEN impl)
       |
2e: ProcessTelemetry    (TDD: header -> stub -> RED tests -> GREEN impl)
```

**Note on stub removal:** `domain/src/stub.cpp` and `tests/src/stub_test.cpp` are placeholders
from Phase 1. They are removed as the first act of adding real sources. The CMakeLists files are
updated at the same time to avoid an intermediate broken state.

---

## Step 1 — Remove Phase 1 Placeholders and Update CMakeLists

**Prerequisite:** Phase 1 complete. Build and tests pass.

**Files modified:**
- `domain/CMakeLists.txt` — remove `src/stub.cpp`, add `src/drone.cpp` and `src/process_telemetry.cpp`
- `tests/CMakeLists.txt` — remove `src/stub_test.cpp`, add domain and future test files
- `domain/src/stub.cpp` — delete this file
- `tests/src/stub_test.cpp` — delete this file

**Updated `domain/CMakeLists.txt`:**
```cmake
add_library(domain STATIC
  src/drone.cpp
  src/process_telemetry.cpp
)
target_include_directories(domain PUBLIC include)
target_compile_features(domain PUBLIC cxx_std_20)
```

**Updated `tests/CMakeLists.txt`:**
```cmake
project(drone-stream-parserTests LANGUAGES CXX)

add_executable(tests
  domain/drone_test.cpp
  domain/process_telemetry_test.cpp
)
target_link_libraries(tests PRIVATE protocol domain GTest::gtest_main GTest::gmock)
target_compile_features(tests PRIVATE cxx_std_20)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
include(GoogleTest)
gtest_discover_tests(tests)

add_folders(Test)
```

**Important:** After this step the build will FAIL until `drone.cpp`, `process_telemetry.cpp`,
`domain/drone_test.cpp`, and `domain/process_telemetry_test.cpp` exist. Do NOT build until the
stub source files for those are created (Steps 5 and 7). The next steps fill in those files in
rapid succession before any build attempt is made.

---

## Step 2 — Value Object: Telemetry

**File to create:** `domain/include/telemetry.hpp`

**Description:** Immutable snapshot of drone state at a point in time. Header-only struct with
no behavior. Public fields without trailing underscore (public member convention). Fields are
not `const` — const would break move semantics needed for pipeline efficiency (BlockingQueue
passes by move). Immutability is by convention: construct once, never mutate after creation.

**Exact content:**
```cpp
#pragma once

#include <cstdint>
#include <string>

struct Telemetry {
  std::string drone_id;
  double latitude;
  double longitude;
  double altitude;
  double speed;
  uint64_t timestamp;
};
```

**No namespace.** The architecture specifies no namespaces for domain types.

**No .cpp file.** Header-only — nothing to compile.

**Dependencies:** None.

---

## Step 3 — Value Objects: AlertType and AlertTransition

**File to create:** `domain/include/alert_types.hpp`

**Description:** Contains two types in a single file:
1. `AlertType` — enum class for extensible alert classification
2. `AlertTransition` — struct signaling a state change (entered or cleared)

The master plan and architecture.md combine these into `alert_types.hpp`. Do NOT create
separate `alert_type.hpp` and `alert_transition.hpp` (the C4 code doc lists them separately
but the master plan takes precedence).

**Exact content:**
```cpp
#pragma once

enum class AlertType {
  ALTITUDE,
  SPEED,
};

struct AlertTransition {
  AlertType type;
  bool entered;
};
```

**No namespace.** Header-only. No .cpp file.

**Dependencies:** None.

---

## Step 4 — Value Object: AlertPolicy

**File to create:** `domain/include/alert_policy.hpp`

**Description:** Threshold configuration for alert evaluation. External to the `Drone` entity —
injected per-call so that the drone does not own policy storage (keeping repository clean).
Fields have constexpr defaults. The struct itself is not constexpr — it is a regular aggregate
with default member initializers. Fields are public without trailing underscore (public member
convention).

**Exact content:**
```cpp
#pragma once

struct AlertPolicy {
  double altitude_limit = 120.0;
  double speed_limit = 50.0;
};
```

**No namespace.** Header-only. No .cpp file.

**Dependencies:** None.

---

## Step 5 — Port Interface: IDroneRepository

**File to create:** `domain/include/i_drone_repository.hpp`

**Description:** Pure abstract class defining the driven port for drone persistence. Defined in
the domain boundary so that infrastructure (adapter) depends on domain, not the reverse.
`findById` returns `std::optional<Drone>` because a missing drone is a normal case (new drone),
not an error. `save` returns void and throws on storage failure. Virtual destructor required for
safe polymorphic destruction.

**Dependency ordering note:** This interface declares `findById` returning `std::optional<Drone>`,
so `drone.hpp` must be included. However `Drone` is not yet implemented — only its header
declaration is needed. `drone.hpp` is created in Step 6 (header declaration, no implementation).
These two files can be created in either order as long as both exist before any build.

**Exact content:**
```cpp
#pragma once

#include <optional>
#include <string>

#include "drone.hpp"

class IDroneRepository {
public:
  virtual ~IDroneRepository() = default;

  virtual std::optional<Drone> findById(const std::string& drone_id) = 0;
  virtual void save(const Drone& drone) = 0;
};
```

**No namespace.** Header-only. No .cpp file.

---

## Step 6 — Port Interface: IAlertNotifier

**File to create:** `domain/include/i_alert_notifier.hpp`

**Description:** Pure abstract class defining the driven port for alert notification. Single
method `notify` receives the drone ID and all transitions that occurred in a single
`updateFrom` call. Called only when transitions are non-empty (use case responsibility). Throws
on transmission failure.

**Exact content:**
```cpp
#pragma once

#include <string>
#include <vector>

#include "alert_types.hpp"

class IAlertNotifier {
public:
  virtual ~IAlertNotifier() = default;

  virtual void notify(const std::string& drone_id,
                      const std::vector<AlertTransition>& transitions) = 0;
};
```

**No namespace.** Header-only. No .cpp file.

---

## Step 7 — Drone Entity Header (Interface Definition)

**File to create:** `domain/include/drone.hpp`

**Description:** Class declaration for the `Drone` entity. Rich entity pattern: owns its own
state and all alert evaluation logic. Alert state is `std::set<AlertType>` for extensibility.
`updateFrom` is `noexcept` — pure arithmetic, cannot fail. All member variables use snake_case
with trailing underscore. Public methods use camelBack. Constructor takes by value for efficiency
(caller can move in).

**Exact content:**
```cpp
#pragma once

#include <set>
#include <string>
#include <vector>

#include "alert_policy.hpp"
#include "alert_types.hpp"
#include "telemetry.hpp"

class Drone {
public:
  explicit Drone(std::string drone_id);

  const std::string& getId() const noexcept;
  const std::set<AlertType>& getAlertState() const noexcept;

  std::vector<AlertTransition> updateFrom(const Telemetry& telemetry,
                                          const AlertPolicy& policy) noexcept;

private:
  std::string drone_id_;
  double latitude_{};
  double longitude_{};
  double altitude_{};
  double speed_{};
  uint64_t timestamp_{};
  std::set<AlertType> alert_state_;
};
```

**No namespace.** Has a corresponding `.cpp` file (Step 9 stub, Step 11 green implementation).

---

## Step 8 — Drone Entity Stub Implementation

**File to create:** `domain/src/drone.cpp`

**Description:** Minimal stub that satisfies the linker. `updateFrom` returns an empty vector
(guaranteed to fail the RED tests). `getId` and `getAlertState` return the member variables
directly.

**Exact content:**
```cpp
#include "drone.hpp"

Drone::Drone(std::string drone_id) : drone_id_(std::move(drone_id)) {}

const std::string& Drone::getId() const noexcept { return drone_id_; }

const std::set<AlertType>& Drone::getAlertState() const noexcept {
  return alert_state_;
}

std::vector<AlertTransition> Drone::updateFrom(const Telemetry& /*telemetry*/,
                                               const AlertPolicy& /*policy*/) noexcept {
  return {};
}
```

**Dependencies:** `drone.hpp` must exist before this file compiles.

---

## Step 9 — ProcessTelemetry Use Case Header (Interface Definition)

**File to create:** `domain/include/process_telemetry.hpp`

**Description:** Use case class that orchestrates the domain operation. Stores references to the
port interfaces (not owned — borrowed from composition root). Stores `AlertPolicy` by value (it
is a value object, copyable, small). Constructor takes interfaces by reference (non-owning).
`execute` is NOT `noexcept` — it calls port implementations that may throw.

**Exact content:**
```cpp
#pragma once

#include "alert_policy.hpp"
#include "i_alert_notifier.hpp"
#include "i_drone_repository.hpp"
#include "telemetry.hpp"

class ProcessTelemetry {
public:
  ProcessTelemetry(IDroneRepository& repository, IAlertNotifier& notifier,
                   AlertPolicy policy);

  void execute(const Telemetry& telemetry);

private:
  IDroneRepository& repository_;
  IAlertNotifier& notifier_;
  AlertPolicy policy_;
};
```

**No namespace.**

---

## Step 10 — ProcessTelemetry Stub Implementation

**File to create:** `domain/src/process_telemetry.cpp`

**Description:** Minimal stub that satisfies the linker. `execute` is a no-op (guaranteed to
fail the RED tests that check repository and notifier are called).

**Exact content:**
```cpp
#include "process_telemetry.hpp"

ProcessTelemetry::ProcessTelemetry(IDroneRepository& repository,
                                   IAlertNotifier& notifier, AlertPolicy policy)
    : repository_(repository), notifier_(notifier), policy_(std::move(policy)) {}

void ProcessTelemetry::execute(const Telemetry& /*telemetry*/) {}
```

**Dependencies:** `process_telemetry.hpp`, `i_drone_repository.hpp`, `i_alert_notifier.hpp`
must all exist.

---

## Step 11 — Test Fake: FakeDroneRepository

**File to create:** `tests/domain/fakes/fake_drone_repository.hpp`

**Description:** In-memory implementation of `IDroneRepository` for use in unit tests.
Stores drones in an `std::unordered_map` keyed by `drone_id`. `findById` returns the stored
drone wrapped in `std::optional`, or `std::nullopt` for unknown IDs. `save` upserts by ID.
Also provides a `contains(id)` helper for test assertions. Uses public include path via the
`target_include_directories` set on the tests target.

**Exact content:**
```cpp
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "drone.hpp"
#include "i_drone_repository.hpp"

class FakeDroneRepository : public IDroneRepository {
public:
  std::optional<Drone> findById(const std::string& drone_id) override {
    auto it = drones_.find(drone_id);
    if (it == drones_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void save(const Drone& drone) override {
    drones_.insert_or_assign(drone.getId(), drone);
  }

  bool contains(const std::string& drone_id) const {
    return drones_.count(drone_id) > 0;
  }

  std::size_t size() const { return drones_.size(); }

private:
  std::unordered_map<std::string, Drone> drones_;
};
```

**No namespace.** Header-only. No .cpp file.

---

## Step 12 — Test Fake: FakeAlertNotifier

**File to create:** `tests/domain/fakes/fake_alert_notifier.hpp`

**Description:** Records all calls to `notify` for test assertion. Stores each call as a
struct containing the drone ID and the transitions vector. Tests can inspect `calls()` to
verify what was (or was not) notified.

**Exact content:**
```cpp
#pragma once

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

  const std::vector<Call>& calls() const { return calls_; }

  bool wasNotified() const { return !calls_.empty(); }

private:
  std::vector<Call> calls_;
};
```

**No namespace.** Header-only. No .cpp file.

---

## Step 13 — Drone Tests (RED Phase)

**File to create:** `tests/domain/drone_test.cpp`

**Description:** Unit tests for the `Drone` entity using GTest. At this point `updateFrom`
returns `{}` (stub), so tests checking alert transitions will FAIL — this is the expected RED
state. Tests for basic construction (`getId`, `getAlertState`) will pass.

**Build expectation at this step:** Build succeeds (stub compiles). `ctest` shows RED for all
alert transition tests. This confirms the test harness is wired correctly.

**Test cases to implement:**

1. **NewDroneHasEmptyAlertState** — construct `Drone("D001")`, assert `getAlertState()` is empty.
2. **GetIdReturnsConstructorId** — construct `Drone("D001")`, assert `getId() == "D001"`.
3. **AltitudeAboveLimitEntersAlert** — call `updateFrom` with `altitude = 125.0` (limit 120.0).
   Assert transitions has exactly one entry with `type == AlertType::ALTITUDE` and `entered == true`.
   Assert `getAlertState()` contains `AlertType::ALTITUDE`.
4. **AltitudeBelowLimitAfterAlertClearsAlert** — first call enters ALTITUDE alert, second call
   with `altitude = 100.0` clears it. Assert second call returns transition `{ALTITUDE, false}`.
   Assert `getAlertState()` is empty after clearing.
5. **NoTransitionWhenAlreadyInAlert** — call `updateFrom` twice above altitude limit. Assert
   second call returns empty transitions (state did not change).
6. **SpeedAboveLimitEntersAlert** — call with `speed = 60.0` (limit 50.0). Assert transition
   `{SPEED, true}`.
7. **SpeedBelowLimitClearsAlert** — enter speed alert then clear. Assert `{SPEED, false}` on
   second call.
8. **BothAlertsActiveSimultaneously** — call with both `altitude = 125.0` and `speed = 60.0`.
   Assert transitions has exactly two entries (order not prescribed). Assert `getAlertState()`
   contains both `ALTITUDE` and `SPEED`.
9. **NoTransitionWhenBelowBothLimits** — call with normal values on a fresh drone. Assert empty
   transitions.

**Include structure:**
```cpp
#include <gtest/gtest.h>

#include "alert_policy.hpp"
#include "alert_types.hpp"
#include "drone.hpp"
#include "telemetry.hpp"
```

**Helper:** Define a `makeTelemetry` free function or lambda within the test file to reduce
boilerplate. Example:
```cpp
static Telemetry makeTelemetry(double altitude, double speed) {
  return {"D001", 0.0, 0.0, altitude, speed, 1000};
}
```

---

## Step 14 — Build Verification (RED Checkpoint)

**Command:** `cmake --build --preset=dev && ctest --preset=dev`

**Expected outcome:**
- Build: SUCCESS (zero warnings, zero clang-tidy errors)
- Tests: FAIL (drone alert transition tests fail — stub returns empty vector)

This is the correct RED state. If the build fails, fix the build before proceeding. If ALL
tests pass, the stub is not being used correctly (check CMakeLists.txt sources).

---

## Step 15 — Drone Entity GREEN Implementation

**File to modify:** `domain/src/drone.cpp`

**Description:** Replace the stub `updateFrom` with the real implementation. The algorithm:
1. Update all state fields from the telemetry parameter.
2. For each `AlertType`, evaluate the threshold against the current state.
3. Compare the new threshold result against the current `alert_state_` set membership.
4. If the condition is newly triggered and not in `alert_state_`: add to set, append
   `{type, true}` to transitions.
5. If the condition is no longer triggered and IS in `alert_state_`: remove from set, append
   `{type, false}` to transitions.
6. If state did not change for this type: append nothing.
7. Return the transitions vector.

**Complete GREEN implementation:**
```cpp
#include "drone.hpp"

Drone::Drone(std::string drone_id) : drone_id_(std::move(drone_id)) {}

const std::string& Drone::getId() const noexcept { return drone_id_; }

const std::set<AlertType>& Drone::getAlertState() const noexcept {
  return alert_state_;
}

std::vector<AlertTransition> Drone::updateFrom(const Telemetry& telemetry,
                                               const AlertPolicy& policy) noexcept {
  latitude_ = telemetry.latitude;
  longitude_ = telemetry.longitude;
  altitude_ = telemetry.altitude;
  speed_ = telemetry.speed;
  timestamp_ = telemetry.timestamp;

  std::vector<AlertTransition> transitions;

  auto evaluate = [&](AlertType type, bool triggered) {
    bool active = alert_state_.count(type) > 0;
    if (triggered && !active) {
      alert_state_.insert(type);
      transitions.push_back({type, true});
    } else if (!triggered && active) {
      alert_state_.erase(type);
      transitions.push_back({type, false});
    }
  };

  evaluate(AlertType::ALTITUDE, telemetry.altitude > policy.altitude_limit);
  evaluate(AlertType::SPEED, telemetry.speed > policy.speed_limit);

  return transitions;
}
```

**Dependencies:** All headers from Steps 2–4 must exist.

---

## Step 16 — Build Verification (Drone GREEN Checkpoint)

**Command:** `cmake --build --preset=dev && ctest --preset=dev`

**Expected outcome:**
- Build: SUCCESS (zero warnings, zero clang-tidy errors)
- Tests: All drone tests PASS. ProcessTelemetry tests not yet added.

Do not proceed until this checkpoint is GREEN.

---

## Step 17 — ProcessTelemetry Tests (RED Phase)

**File to create:** `tests/domain/process_telemetry_test.cpp`

**Description:** Unit tests for `ProcessTelemetry` using `FakeDroneRepository` and
`FakeAlertNotifier`. At this point `execute` is a no-op, so all tests will FAIL — correct RED.

**Include structure:**
```cpp
#include <gtest/gtest.h>

#include "alert_policy.hpp"
#include "domain/fakes/fake_alert_notifier.hpp"
#include "domain/fakes/fake_drone_repository.hpp"
#include "process_telemetry.hpp"
#include "telemetry.hpp"
```

**Test cases to implement:**

1. **NewDroneCreatedAndSavedOnFirstTelemetry** — start with empty `FakeDroneRepository`.
   Call `execute` with telemetry for `"D001"`. Assert `repo.contains("D001")` is true.

2. **ExistingDroneRetrievedUpdatedAndSaved** — pre-populate repo with a `Drone("D001")`.
   Call `execute` with new telemetry. Assert `repo.contains("D001")` is still true and
   the saved drone reflects the update (check via `repo.findById("D001")->getAlertState()`
   after sending over-threshold telemetry).

3. **NotifierCalledWhenAlertEntered** — send telemetry with `altitude = 125.0` (above limit).
   Assert `notifier.calls().size() == 1`. Assert the call's `drone_id == "D001"`. Assert
   transitions in the call contain `{AlertType::ALTITUDE, true}`.

4. **NotifierNotCalledWhenNoTransition** — send telemetry with `altitude = 50.0` (below limit)
   to a fresh drone (no active alerts). Assert `notifier.wasNotified() == false`.

5. **NotifierNotCalledOnRepeatedAlertAboveThreshold** — call `execute` twice with altitude above
   limit. Assert `notifier.calls().size() == 1` (only the entering transition, not the second).

6. **NotifierCalledWhenAlertCleared** — first call enters ALTITUDE alert. Second call sends
   altitude below limit. Assert total `notifier.calls().size() == 2`. Assert second call's
   transitions contain `{AlertType::ALTITUDE, false}`.

7. **MultipleTransitionsForwardedInOneCall** — send telemetry with both `altitude = 125.0` and
   `speed = 60.0`. Assert `notifier.calls().size() == 1` (single notify call with both
   transitions). Assert transitions vector has two entries.

**Build expectation at this step:** Add `domain/process_telemetry_test.cpp` to
`tests/CMakeLists.txt` sources. Build succeeds. `ctest` shows RED for all ProcessTelemetry
tests (no-op stub).

---

## Step 18 — Update tests/CMakeLists.txt for ProcessTelemetry Tests

**File to modify:** `tests/CMakeLists.txt`

The file must already include `domain/process_telemetry_test.cpp`. If you followed Step 1 and
included it there, this step is already done. Verify the file lists both test sources:

```cmake
add_executable(tests
  domain/drone_test.cpp
  domain/process_telemetry_test.cpp
)
```

---

## Step 19 — Build Verification (ProcessTelemetry RED Checkpoint)

**Command:** `cmake --build --preset=dev && ctest --preset=dev`

**Expected outcome:**
- Build: SUCCESS
- Tests: drone tests PASS, ProcessTelemetry tests FAIL (no-op stub)

This is the correct RED state. Confirm before writing the GREEN implementation.

---

## Step 20 — ProcessTelemetry GREEN Implementation

**File to modify:** `domain/src/process_telemetry.cpp`

**Description:** Replace the no-op `execute` with the real implementation. The algorithm
precisely follows the execution flow documented in the master plan and architecture:

1. Call `repository_.findById(telemetry.drone_id)` → `std::optional<Drone>`
2. If `nullopt`, construct `Drone(telemetry.drone_id)`; otherwise use the found drone
3. Call `drone.updateFrom(telemetry, policy_)` → `std::vector<AlertTransition>`
4. Call `repository_.save(drone)`
5. If transitions is non-empty, call `notifier_.notify(telemetry.drone_id, transitions)`

**Complete GREEN implementation:**
```cpp
#include "process_telemetry.hpp"

#include "drone.hpp"

ProcessTelemetry::ProcessTelemetry(IDroneRepository& repository,
                                   IAlertNotifier& notifier, AlertPolicy policy)
    : repository_(repository), notifier_(notifier), policy_(std::move(policy)) {}

void ProcessTelemetry::execute(const Telemetry& telemetry) {
  auto maybe_drone = repository_.findById(telemetry.drone_id);
  Drone drone = maybe_drone ? std::move(*maybe_drone)
                            : Drone(telemetry.drone_id);

  auto transitions = drone.updateFrom(telemetry, policy_);
  repository_.save(drone);

  if (!transitions.empty()) {
    notifier_.notify(telemetry.drone_id, transitions);
  }
}
```

**Note on `std::move(*maybe_drone)`:** `std::optional<Drone>` stores by value. Dereferencing
gives an lvalue. Moving out of it is correct here because we own the optional and do not use
it again. `Drone` is move-constructible (compiler-generated move from `std::string` + `std::set`
members, both movable).

---

## Step 21 — Final Build Verification (Phase 2 Complete)

**Command:** `cmake --build --preset=dev && ctest --preset=dev`

**Expected outcome:**
- Build: SUCCESS (zero warnings, zero clang-tidy errors, clang-format clean)
- All domain tests PASS:
  - `DroneTest.NewDroneHasEmptyAlertState` PASS
  - `DroneTest.GetIdReturnsConstructorId` PASS
  - `DroneTest.AltitudeAboveLimitEntersAlert` PASS
  - `DroneTest.AltitudeBelowLimitAfterAlertClearsAlert` PASS
  - `DroneTest.NoTransitionWhenAlreadyInAlert` PASS
  - `DroneTest.SpeedAboveLimitEntersAlert` PASS
  - `DroneTest.SpeedBelowLimitClearsAlert` PASS
  - `DroneTest.BothAlertsActiveSimultaneously` PASS
  - `DroneTest.NoTransitionWhenBelowBothLimits` PASS
  - `ProcessTelemetryTest.NewDroneCreatedAndSavedOnFirstTelemetry` PASS
  - `ProcessTelemetryTest.ExistingDroneRetrievedUpdatedAndSaved` PASS
  - `ProcessTelemetryTest.NotifierCalledWhenAlertEntered` PASS
  - `ProcessTelemetryTest.NotifierNotCalledWhenNoTransition` PASS
  - `ProcessTelemetryTest.NotifierNotCalledOnRepeatedAlertAboveThreshold` PASS
  - `ProcessTelemetryTest.NotifierCalledWhenAlertCleared` PASS
  - `ProcessTelemetryTest.MultipleTransitionsForwardedInOneCall` PASS

If any test fails, fix before declaring Phase 2 complete.

---

## Naming Convention Reference

Use this table throughout Phase 2 to resolve any ambiguity:

| Element | Convention | Example |
|---------|------------|---------|
| Classes/Structs | CamelCase | `Drone`, `Telemetry`, `AlertPolicy` |
| Interfaces | CamelCase with `I` prefix | `IDroneRepository`, `IAlertNotifier` |
| Methods | camelBack | `updateFrom`, `findById`, `getId` |
| Parameters | snake_case | `drone_id`, `altitude_limit` |
| Private member variables | snake_case + trailing `_` | `drone_id_`, `alert_state_` |
| Public struct fields | snake_case, no trailing `_` | `drone_id`, `latitude`, `altitude_limit` |
| Enum class | CamelCase class, UPPER_CASE values | `AlertType::ALTITUDE` |
| Test names | CamelCase fixture, CamelCase test | `DroneTest.AltitudeAboveLimitEntersAlert` |
| File names | snake_case | `drone.hpp`, `alert_types.hpp`, `fake_drone_repository.hpp` |

---

## Files Created in Phase 2

### Domain headers (header-only, no .cpp)
| File | Type | Step |
|------|------|------|
| `domain/include/telemetry.hpp` | Value Object | 2 |
| `domain/include/alert_types.hpp` | Value Object + Enum | 3 |
| `domain/include/alert_policy.hpp` | Value Object | 4 |
| `domain/include/i_drone_repository.hpp` | Port Interface | 5 |
| `domain/include/i_alert_notifier.hpp` | Port Interface | 6 |
| `domain/include/drone.hpp` | Entity Declaration | 7 |
| `domain/include/process_telemetry.hpp` | Use Case Declaration | 9 |

### Domain sources
| File | Type | Step |
|------|------|------|
| `domain/src/drone.cpp` | Entity (stub then GREEN) | 8, 15 |
| `domain/src/process_telemetry.cpp` | Use Case (stub then GREEN) | 10, 20 |

### Test fakes (header-only)
| File | Type | Step |
|------|------|------|
| `tests/domain/fakes/fake_drone_repository.hpp` | Test Double | 11 |
| `tests/domain/fakes/fake_alert_notifier.hpp` | Test Double | 12 |

### Test sources
| File | Type | Step |
|------|------|------|
| `tests/domain/drone_test.cpp` | Unit Tests | 13 |
| `tests/domain/process_telemetry_test.cpp` | Unit Tests | 17 |

### Modified files
| File | Change | Step |
|------|--------|------|
| `domain/CMakeLists.txt` | Replace stub.cpp with drone.cpp + process_telemetry.cpp | 1 |
| `tests/CMakeLists.txt` | Replace stub_test.cpp with domain test files; add include dir | 1 |

### Deleted files
| File | Reason | Step |
|------|--------|------|
| `domain/src/stub.cpp` | Replaced by real sources | 1 |
| `tests/src/stub_test.cpp` | Replaced by real tests | 1 |

---

## Common Pitfalls to Avoid

**Circular include in i_drone_repository.hpp:** The interface includes `drone.hpp`. If
`drone.hpp` in turn includes `i_drone_repository.hpp`, a circular include results. It does not
— `drone.hpp` only includes `alert_policy.hpp`, `alert_types.hpp`, and `telemetry.hpp`.

**Missing `target_include_directories` for tests:** The test sources in `tests/domain/`
use `#include "domain/fakes/fake_drone_repository.hpp"` with the path relative to the
`tests/` directory. The `tests/CMakeLists.txt` must add:
```cmake
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```
This makes `${CMAKE_CURRENT_SOURCE_DIR}` (i.e., the `tests/` directory) a search root, so
`#include "domain/fakes/fake_drone_repository.hpp"` resolves correctly.

**Domain headers are found via the `domain` target:** The `tests` target links `domain`, which
has `target_include_directories(domain PUBLIC include)`. This means `#include "drone.hpp"` and
`#include "alert_types.hpp"` etc. resolve to `domain/include/...` without any additional
include path setup on the tests target.

**clang-tidy will flag `int` comparisons with `size_t`:** Use `std::size_t` for counts. The
`contains` check `drones_.count(drone_id) > 0` is idiomatic C++20 — `count` returns
`size_type` and comparison with `0` (an `int` literal) may trigger a signed/unsigned warning.
Use `> 0U` or restructure to `drones_.contains(drone_id)` (C++20).

**Update `FakeDroneRepository::contains` to use C++20 `contains`:**
```cpp
bool contains(const std::string& drone_id) const {
  return drones_.contains(drone_id);
}
```

**`noexcept` on Drone methods:** `getId`, `getAlertState`, and `updateFrom` are all declared
`noexcept` in the header. The `.cpp` definitions must NOT add `noexcept` as a keyword in the
definition — the `noexcept` is part of the declaration, and the definition implicitly inherits
it via the header. clang-tidy will enforce consistency.

**`AlertPolicy` fields use `= 120.0` not `= 120.0F`:** The fields are `double`. Using `120.0F`
(float literal) would cause a narrowing conversion warning. Use plain `120.0` (double literal).

---

## Build After Every Change

Per project CLAUDE.md, after EVERY source file change:
```bash
cmake --build --preset=dev && ctest --preset=dev
```

Minimum build points in this phase:
- After Step 1 (CMakeLists changes) + Steps 8 and 10 (stubs created): first full build
- After Step 13 (drone tests added): RED checkpoint
- After Step 15 (drone GREEN): drone tests must pass before writing ProcessTelemetry tests
- After Step 17 (ProcessTelemetry tests added): RED checkpoint
- After Step 20 (ProcessTelemetry GREEN): all tests must pass — Phase 2 complete
