# Phase 4: Common Utility — BlockingQueue Implementation Plan

**Date:** 2026-03-08
**Status:** Active
**Branch:** phase-3-protocol-boundary (to be continued or branched from here)
**Standard:** C++20 | GCC 15.2.1 | CMake 4.2.3 | Linux

---

## Context

Phase 4 delivers `BlockingQueue<T>` — the thread-safe, bounded queue that connects
all three pipeline stages in the server. It is the only synchronization primitive
in the entire system; every inter-thread handoff goes through it.

**What this phase delivers:**
- `common/include/blocking_queue.hpp` — header-only template, no .cpp file
- `tests/common/blocking_queue_test.cpp` — full unit test coverage
- Updated `tests/CMakeLists.txt` to compile the new test file

**Why it is isolated here:**
`common` has no dependencies on `domain` or `protocol`. It is a pure concurrency
primitive analogous to Go's buffered channel. Phase 5 (Infrastructure) cannot be
written without it, so Phase 4 is a hard prerequisite. However, nothing in Phases
2 or 3 depends on it, making it fully self-contained.

**TDD workflow for this phase:**
interface declaration → stub (compile-only) → tests (RED) → implementation (GREEN) → verify clean build

---

## Key Design Decisions

### Template class, header-only INTERFACE library

`BlockingQueue<T>` is a template class. Templates must be defined in headers
because the compiler instantiates them at each point of use. There is no `.cpp`
file. The CMake target is `INTERFACE` (already declared in `common/CMakeLists.txt`).
No change to the library type is needed — only the header file itself is added.

### Two condition variables, not one

The implementation uses two condition variables:
- `not_empty_` — signals `pop()` waiters when an item is pushed
- `not_full_` — signals `push()` waiters when an item is popped

Using a single `condition_variable::notify_all()` is correct but wastes CPU by
waking threads that cannot proceed. The two-variable pattern is the standard
design for bounded producer-consumer queues and matches the architecture doc's
internal state description.

### Member naming follows project convention

All member variables use trailing underscore (`capacity_`, `closed_`, `buffer_`,
`mutex_`, `not_empty_`, `not_full_`). Public members of structs (of which there
are none here) would omit the suffix — not applicable.

### noexcept decisions

| Method   | noexcept? | Rationale |
|----------|-----------|-----------|
| Constructor | no | `std::deque` construction may allocate; `std::bad_alloc` is legitimate |
| `push()` | no | Acquires `std::mutex` (lock constructor may throw in extreme cases per standard; practically never on Linux, but contract must be correct). Also calls `buffer_.push_back()` which may allocate. |
| `pop()`  | no | Same mutex acquisition rationale. Returns `std::optional<T>` by value (move). |
| `close()` | yes | No allocation. Lock/notify on a valid mutex cannot throw on Linux. Matches the shutdown cascade model: `close()` is called from signal context — it must not throw. |

This matches the project's noexcept boundary map: `close()` is infrastructure
signaling (pure flag + notify), so `noexcept` is correct. `push()` and `pop()`
interact with allocating containers and mutexes, so they are not marked noexcept.

### close() semantics: items pushed before close remain retrievable

When `close()` is called, the queue is marked closed but existing items in
`buffer_` are NOT discarded. `pop()` continues to drain them, returning items
one by one until the buffer is empty, then returns `std::nullopt`. This matches
the shutdown cascade design in the architecture: the parse stage must drain Q1
before closing Q2 — no data is silently dropped.

### push() after close() is a no-op, not an error

Once `closed_` is true, `push()` returns immediately without inserting. This is
not an assertion failure because `close()` and `push()` may race during shutdown.
The TcpServer calls `Q1.close()` after its recv loop exits; there is no guarantee
that the recv thread has fully stopped before a stray push arrives. A no-op is
the correct response.

### T must be move-constructible

The template does not place explicit `requires` constraints (to keep it simple),
but the queue moves items into and out of `buffer_`. The caller is responsible for
passing move-constructible types. Both `std::vector<uint8_t>` and `Telemetry` are
move-efficient by construction, satisfying this in all planned uses.

### Stress test uses std::thread directly, not GTest fixtures

The concurrency stress test spawns real threads. It uses a fixed item count
(e.g., 1000 items), multiple producers (e.g., 4) and multiple consumers (e.g., 4),
and an `std::atomic<int>` counter to verify exactly N items were consumed. The
test uses a timeout-style join to avoid hanging the test suite on deadlock: all
threads are joined before assertions. If a deadlock occurs, `ctest` timeout kills
the process.

---

## Commit-by-Commit Breakdown

### Commit 1 — Header skeleton: class declaration, no implementation

**Goal:** The header compiles. All methods are declared. No logic yet.

**Files created:**
- `common/include/blocking_queue.hpp`

**Content of the header:**

```
Standard includes at top:
  <condition_variable>, <deque>, <mutex>, <optional>, <cstddef>

template<typename T>
class BlockingQueue {
public:
  explicit BlockingQueue(size_t capacity);

  void push(T&& item);
  std::optional<T> pop();
  void close() noexcept;

private:
  size_t capacity_;
  std::deque<T> buffer_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  bool closed_{false};
};
```

No include guards using `#pragma once` — use the standard `#ifndef` guard
matching the file name: `BLOCKING_QUEUE_HPP`. This is consistent with
`stream_parser.hpp` which uses `#ifndef STREAM_PARSER_HPP`.

Method bodies are not yet provided in this commit. The class compiles but
cannot be instantiated usefully (linker errors if called). This commit
establishes the interface contract.

**Verification:**
```
cmake --build --preset=dev
```
Build must succeed with zero warnings and zero clang-tidy errors. No tests
added yet — this commit is the "interface" step of the TDD workflow.

---

### Commit 2 — Test file: blocking_queue_test.cpp (RED)

**Goal:** All seven tests exist and compile. They fail at runtime because
the implementation is absent (undefined behavior or linker failure — this is
acceptable since the methods are not yet defined, but see note below).

**Note on stub approach:** Because `BlockingQueue` is a template defined in
a header, there is no separate stub `.cpp` file. To make the RED state compile
without UB, the method bodies in the header will be minimal stubs:
- Constructor: empty body
- `push()`: empty body
- `pop()`: returns `std::nullopt`
- `close()`: empty body

The stub is added in this same commit alongside the tests so that everything
compiles. The tests will fail (FAIL, not compile-error) because the stubs
do nothing. This is the RED state.

**Files created:**
- `tests/common/blocking_queue_test.cpp`

**Files modified:**
- `tests/CMakeLists.txt` — add `common/blocking_queue_test.cpp` to the
  `add_executable(tests ...)` source list

**CMakeLists.txt change — exact diff:**

In `tests/CMakeLists.txt`, the `add_executable` call currently lists:
```
domain/drone_test.cpp
domain/process_telemetry_test.cpp
protocol/crc16_test.cpp
protocol/serializer_test.cpp
protocol/parser_test.cpp
```

Add one line:
```
common/blocking_queue_test.cpp
```

Also add `common` to the `target_link_libraries` call:
```
target_link_libraries(tests PRIVATE protocol domain common GTest::gtest_main GTest::gmock)
```

This is required because `common` is an INTERFACE library that propagates
its include directory. Without linking to it, `#include "blocking_queue.hpp"`
will not resolve.

**Test file structure:**

```
File-level includes (in order):
  <atomic>
  <chrono>
  <optional>
  <thread>
  <vector>
  <gtest/gtest.h>
  "blocking_queue.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {
  // No helpers needed — tests are self-contained
}  // namespace

[seven TEST() blocks — see Test Details section]

// NOLINTEND(readability-magic-numbers)
```

**Verification:**
```
cmake --build --preset=dev && ctest --preset=dev
```
Build must succeed with zero warnings. Tests for BlockingQueue must
compile and run. The new BlockingQueue tests will FAIL (RED). All previously
passing domain and protocol tests must remain GREEN.

---

### Commit 3 — Implementation: blocking_queue.hpp GREEN

**Goal:** Replace stubs with correct implementation. All tests pass GREEN.

**Files modified:**
- `common/include/blocking_queue.hpp` — replace stub bodies with implementation

**Implementation content (method by method):**

**Constructor:**
```
BlockingQueue(size_t capacity)
  : capacity_(capacity)
{}
```
Capacity is stored. `buffer_` starts empty. `closed_` is false (in-class init).

**push(T&& item):**
```
1. Lock mutex_ with std::unique_lock<std::mutex>
2. Wait on not_full_ while: buffer_.size() >= capacity_ AND NOT closed_
3. If closed_: return immediately (no-op)
4. buffer_.push_back(std::move(item))
5. Unlock (unique_lock destructor)
6. not_empty_.notify_one()
```

The condition `buffer_.size() >= capacity_ AND NOT closed_` is the wait
predicate. Using a lambda: `[this]{ return closed_ || buffer_.size() < capacity_; }`
The push waits until there is room OR the queue is closed. If it wakes because
it is closed, it returns without pushing.

**pop():**
```
1. Lock mutex_ with std::unique_lock<std::mutex>
2. Wait on not_empty_ while: buffer_.empty() AND NOT closed_
3. If buffer_ is empty (woke because closed, nothing left): return std::nullopt
4. T item = std::move(buffer_.front()); buffer_.pop_front()
5. Unlock (unique_lock destructor)
6. not_full_.notify_one()
7. return item
```

Wait predicate: `[this]{ return closed_ || !buffer_.empty(); }`
Pop waits until there is an item OR the queue is closed. After waking, it
checks for items first — items pushed before `close()` are drained completely
before returning nullopt.

**close():**
```
1. Lock mutex_ with std::lock_guard<std::mutex> (or unique_lock)
2. closed_ = true
3. Unlock
4. not_empty_.notify_all()
5. not_full_.notify_all()
```

Both condition variables are notified with `notify_all()` to wake every
blocked `push()` and `pop()` simultaneously. Using `notify_one()` here would
be incorrect — there may be multiple threads blocked on each CV.

`close()` is `noexcept`. Lock acquisition and `notify_all()` on a valid,
non-destroyed mutex do not throw on Linux. The noexcept contract holds.

**Verification:**
```
cmake --build --preset=dev && ctest --preset=dev
```
All tests must pass GREEN including all seven BlockingQueue tests. Zero
warnings. Zero clang-tidy errors.

---

## Test Details

File: `tests/common/blocking_queue_test.cpp`
Test suite name: `BlockingQueueTest`

All tests use `BlockingQueue<int>` unless the test specifically requires a
different type (the move-only type test uses a unique_ptr).

---

### Test 1 — PushThenPopReturnsSameItem

**Setup:** `BlockingQueue<int> q{4}` (capacity 4, well above what the test needs)

**Action:** `q.push(42)`, then `auto val = q.pop()`

**Assertion:**
```
ASSERT_TRUE(val.has_value());
EXPECT_EQ(*val, 42);
```

**Purpose:** The minimal round-trip. Verifies that push stores and pop retrieves
the correct value, and that pop returns a populated optional when an item is present.

---

### Test 2 — PopOnEmptyQueueBlocksUntilPush

**Setup:** `BlockingQueue<int> q{4}`

**Action:**
1. Spawn a producer thread that sleeps 20ms, then calls `q.push(99)`
2. Main thread calls `q.pop()` immediately (blocks because queue is empty)
3. Join producer thread
4. Record the popped value

**Assertion:**
```
ASSERT_TRUE(val.has_value());
EXPECT_EQ(*val, 99);
```

**Purpose:** Verifies that `pop()` blocks rather than returning nullopt on an
empty open queue, and that it wakes correctly when an item arrives from another
thread. The 20ms sleep ensures pop is already blocked before push is called.

---

### Test 3 — PushOnFullQueueBlocksUntilPop

**Setup:** `BlockingQueue<int> q{2}` (capacity 2). Push two items to fill it:
`q.push(1)`, `q.push(2)`.

**Action:**
1. Spawn a consumer thread that sleeps 20ms, then calls `q.pop()` (discards result)
2. Main thread calls `q.push(3)` immediately (blocks because queue is full at capacity 2)
3. Join consumer thread

**Assertion:**
```
// push(3) returned — it was unblocked by the consumer's pop
// The queue now has one item: the push succeeded
auto remaining = q.pop();
ASSERT_TRUE(remaining.has_value());
// remaining is either 1 or 3 depending on which was consumed; 3 was definitely pushed
```

A simpler assertion: verify that the push call returned at all (i.e., the test
did not hang). To make this deterministic, after joining the consumer thread,
pop twice and collect both values. Verify the multiset {1, 2, 3} minus the one
consumed equals the two remaining.

**Concrete assertion approach:**
```
std::vector<int> collected;
auto v = q.pop();
ASSERT_TRUE(v.has_value());
collected.push_back(*v);
v = q.pop();
ASSERT_TRUE(v.has_value());
collected.push_back(*v);
// All three items must appear across the consumer's pop + the two remaining pops
// (consumer popped one, main popped two: total 3 distinct items)
```

**Purpose:** Verifies back-pressure — a full queue blocks the producer until
a consumer makes room.

---

### Test 4 — CloseUnblocksBlockedPop

**Setup:** `BlockingQueue<int> q{4}` (empty)

**Action:**
1. Spawn a thread that calls `auto result = q.pop()` (blocks immediately)
2. Main thread sleeps 20ms, then calls `q.close()`
3. Join the thread
4. Check the result captured in the spawned thread

**Assertion:**
```
EXPECT_FALSE(result.has_value());
```

**Purpose:** Verifies that `close()` wakes a thread blocked in `pop()` and that
the unblocked pop returns `std::nullopt` (because the queue is closed and empty).

---

### Test 5 — CloseUnblocksBlockedPush

**Setup:** `BlockingQueue<int> q{1}`. Fill it: `q.push(0)` (capacity 1, now full).

**Action:**
1. Spawn a thread that calls `q.push(42)` (blocks immediately, queue is full)
2. Main thread sleeps 20ms, then calls `q.close()`
3. Join the thread

**Assertion:**
After joining, the spawned thread must have returned (not hung). Verify the
queue still holds exactly the original item (the blocked push was a no-op):
```
auto val = q.pop();
ASSERT_TRUE(val.has_value());
EXPECT_EQ(*val, 0);  // the original item, not 42
auto val2 = q.pop();
EXPECT_FALSE(val2.has_value());  // queue was closed, 42 was never pushed
```

**Purpose:** Verifies that `close()` wakes a thread blocked in `push()` and
that the unblocked push discards its item rather than inserting it into a
closed queue.

---

### Test 6 — ItemsPushedBeforeCloseAreStillRetrievable

**Setup:** `BlockingQueue<int> q{8}`

**Action:**
1. Push 3 items: `q.push(10)`, `q.push(20)`, `q.push(30)`
2. Call `q.close()`
3. Pop in a loop until nullopt

**Assertion:**
```
std::vector<int> drained;
while (auto v = q.pop()) {
  drained.push_back(*v);
}
ASSERT_EQ(drained.size(), 3U);
EXPECT_EQ(drained[0], 10);
EXPECT_EQ(drained[1], 20);
EXPECT_EQ(drained[2], 30);
```

**Purpose:** Verifies the shutdown draining contract. The pipeline requires that
closing the queue does not lose buffered items — stages must drain before
terminating. This test also implicitly verifies FIFO ordering.

---

### Test 7 — MultipleProducersAndConsumersDeliverAllItemsExactlyOnce

**Setup:** `BlockingQueue<int> q{16}` (bounded, smaller than total items to
exercise back-pressure). Use 4 producer threads and 4 consumer threads.
Total items: 1000 (250 per producer).

**Action:**
1. Launch 4 producer threads. Each pushes 250 distinct integers
   (producer 0 pushes 0..249, producer 1 pushes 250..499, etc.)
2. Launch 4 consumer threads. Each pops in a loop until nullopt,
   atomically incrementing a shared `std::atomic<int> consumed_count`
   and recording each value into a thread-local vector.
3. After all producers complete (join all producer threads), call `q.close()`
4. Join all consumer threads
5. Aggregate all consumer results

**Assertion:**
```
EXPECT_EQ(consumed_count.load(), 1000);
// Verify each value 0..999 appears exactly once in the union of all
// consumer results (use a sorted vector or std::set)
std::vector<int> all_values = /* union of per-thread results */;
std::sort(all_values.begin(), all_values.end());
for (int i = 0; i < 1000; ++i) {
  EXPECT_EQ(all_values[i], i);
}
```

**Purpose:** The definitive correctness test for concurrent use. Verifies no
items are lost (all 1000 received), no items are duplicated (each appears
exactly once), and no deadlock occurs under concurrent producer/consumer load
with back-pressure.

**Implementation note for the test:** Consumer threads must tolerate `pop()`
returning nullopt only after the queue is closed. The loop structure is:
```cpp
while (auto v = q.pop()) {
  // record *v
}
```
This naturally terminates when pop returns nullopt after close + drain.

---

## CMake Integration

### `tests/CMakeLists.txt` — required changes

**Change 1:** Add the new test source to `add_executable`:
```cmake
add_executable(tests
  domain/drone_test.cpp
  domain/process_telemetry_test.cpp
  protocol/crc16_test.cpp
  protocol/serializer_test.cpp
  protocol/parser_test.cpp
  common/blocking_queue_test.cpp   # <-- add this line
)
```

**Change 2:** Add `common` to `target_link_libraries`:
```cmake
target_link_libraries(tests PRIVATE protocol domain common GTest::gtest_main GTest::gmock)
```

`common` is an INTERFACE library. Linking to it propagates
`common/include` as an include directory to the `tests` target, making
`#include "blocking_queue.hpp"` resolve without a path prefix.

### `common/CMakeLists.txt` — no changes required

The file already declares the INTERFACE library and sets the include directory:
```cmake
add_library(common INTERFACE)
target_include_directories(common INTERFACE include)
```

No sources to add (header-only). No new targets. No dependencies to declare
(common has zero library dependencies by design).

### `common/include/.gitkeep` — remove after adding the header

The `.gitkeep` placeholder in `common/include/` should be deleted when
`blocking_queue.hpp` is created. Git tracks files, not directories.

---

## Naming Conventions Applied

Following the project's established conventions from `CLAUDE.md` and observed
in `stream_parser.hpp`:

| Element | Convention | Examples in this file |
|---------|-----------|----------------------|
| Class name | CamelCase | `BlockingQueue` |
| Template parameter | CamelCase (single letter or short) | `T` |
| Public methods | camelBack | `push`, `pop`, `close` |
| Private members | snake_case with trailing `_` | `capacity_`, `closed_`, `buffer_`, `mutex_`, `not_empty_`, `not_full_` |
| Include guard | `UPPER_SNAKE_CASE_HPP` | `BLOCKING_QUEUE_HPP` |
| Test suite name | CamelCase + `Test` suffix | `BlockingQueueTest` |
| Test case names | CamelCase description of behavior | `PushThenPopReturnsSameItem` |
| NOLINT blocks | bracket entire test file magic numbers | `// NOLINTBEGIN/END(readability-magic-numbers)` |
| Anonymous namespace | wrap helpers | `namespace {` ... `}  // namespace` |

The test file follows the exact same style as `parser_test.cpp` and `crc16_test.cpp`:
- System includes first, sorted alphabetically
- Third-party includes (`gtest/gtest.h`) after system includes
- Project includes last (`blocking_queue.hpp`)
- `// NOLINTBEGIN` / `// NOLINTEND` brackets around the magic-number-heavy test body
- No fixtures (use plain `TEST()`, not `TEST_F()`) unless shared setup would
  eliminate significant duplication — for these tests, plain `TEST()` is sufficient

---

## Build and Test Verification

After each commit, run:
```bash
cmake --build --preset=dev && ctest --preset=dev
```

This runs the full build including clang-tidy (via the dev preset) and all
tests. The dev preset enables `-Werror`, so zero warnings is enforced.

**After Commit 1:** Build passes. No new tests.
**After Commit 2:** Build passes. BlockingQueue tests compile and run RED (stubs return nullopt/no-op). All prior tests remain GREEN.
**After Commit 3:** Build passes. All tests GREEN including all 7 BlockingQueue tests.

**Final state check:**
```bash
ctest --preset=dev --output-on-failure
```
Expected output includes a line for each of the 7 new tests:
```
[  PASSED  ] BlockingQueueTest.PushThenPopReturnsSameItem
[  PASSED  ] BlockingQueueTest.PopOnEmptyQueueBlocksUntilPush
[  PASSED  ] BlockingQueueTest.PushOnFullQueueBlocksUntilPop
[  PASSED  ] BlockingQueueTest.CloseUnblocksBlockedPop
[  PASSED  ] BlockingQueueTest.CloseUnblocksBlockedPush
[  PASSED  ] BlockingQueueTest.ItemsPushedBeforeCloseAreStillRetrievable
[  PASSED  ] BlockingQueueTest.MultipleProducersAndConsumersDeliverAllItemsExactlyOnce
```

---

## Files Summary

| Action | Path |
|--------|------|
| Create | `common/include/blocking_queue.hpp` |
| Create | `tests/common/blocking_queue_test.cpp` |
| Modify | `tests/CMakeLists.txt` |
| Delete | `common/include/.gitkeep` |

No other files are touched in Phase 4.
