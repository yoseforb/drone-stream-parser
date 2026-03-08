# Phase 5 Code Review Fixes — Implementation Plan

**Date:** 2026-03-08
**Status:** Active
**Depends on:** Phase 5 (server infrastructure, completed)
**Scope:** 7 issues found during Phase 5 code review. All are hardening fixes — no behavioral changes.

---

## Overview

These fixes address signal safety, error-checking consistency, input validation, and future-proofing found during Phase 5 code review. Grouped by file to minimize context switches.

**Order:** Tasks 1-2 (signal_handler.cpp) → Tasks 3-4 (tcp_server.cpp) → Tasks 5,7 (main.cpp) → Task 6 (console_alert_notifier.cpp). Each task ends with a full build+test cycle.

---

## Task 1: SignalHandler — Make g_stop_flag an atomic pointer (Medium)

**File:** `server/src/signal_handler.cpp`

### Current Code

```cpp
std::atomic<bool>* g_stop_flag{nullptr};

void handleSignal(int /*sig*/) {
  g_stop_flag->store(true, std::memory_order_relaxed);
}

SignalHandler::SignalHandler(std::atomic<bool>& stop_flag) {
  g_stop_flag = &stop_flag;
```

### Changes

1. Change type from `std::atomic<bool>*` to `std::atomic<std::atomic<bool>*>`, initialized to `nullptr`.
2. In `handleSignal`: load pointer atomically into local variable with `memory_order_relaxed`. Check null. If non-null, store `true`.
3. In constructor: use `.store(&stop_flag, std::memory_order_relaxed)`.

### Result

```cpp
std::atomic<std::atomic<bool>*> g_stop_flag{nullptr};

void handleSignal(int /*sig*/) {
  auto* flag = g_stop_flag.load(std::memory_order_relaxed);
  if (flag != nullptr) {
    flag->store(true, std::memory_order_relaxed);
  }
}

SignalHandler::SignalHandler(std::atomic<bool>& stop_flag) {
  g_stop_flag.store(&stop_flag, std::memory_order_relaxed);
```

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

---

## Task 2: SignalHandler — Check sigaction return values (Low)

**File:** `server/src/signal_handler.cpp`

### Changes

1. Add `#include <spdlog/spdlog.h>`.
2. Check return values of both `sigaction()` calls. Log warning on failure.

### Result

```cpp
if (sigaction(SIGINT, &sig_action, nullptr) == -1) {
  spdlog::warn("SignalHandler: sigaction(SIGINT) failed");
}
if (sigaction(SIGTERM, &sig_action, nullptr) == -1) {
  spdlog::warn("SignalHandler: sigaction(SIGTERM) failed");
}
```

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

---

## Task 3: TcpServer — Check setsockopt return value (Low)

**File:** `server/src/tcp_server.cpp`, in `createListeningSocket`

### Changes

Check return value of `setsockopt(SO_REUSEADDR)`. Log warning on failure. Don't throw — non-fatal.

### Result

```cpp
int opt = 1;
// NOLINTNEXTLINE(misc-include-cleaner)
if (setsockopt(Sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
  spdlog::warn("TcpServer: setsockopt(SO_REUSEADDR) failed: {}",
               std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
}
```

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

---

## Task 4: TcpServer — Add poll() timeout to recvLoop (Low)

**File:** `server/src/tcp_server.cpp`, function `recvLoop`

### Changes

Add `poll()` with 200ms timeout before `recv()`, matching the accept loop pattern. On timeout, continue to re-check `stop_flag_`. On EINTR, continue. On error, log and return.

### Result

```cpp
void TcpServer::recvLoop(int client_fd) {
  constexpr std::size_t RecvBufSize = 4096;
  std::array<uint8_t, RecvBufSize> buf{};

  while (!stop_flag_) {
    pollfd pfd{};
    pfd.fd = client_fd;
    pfd.events = POLLIN;

    constexpr int PollTimeoutMs = 200;
    const int PollRet = poll(&pfd, 1, PollTimeoutMs);
    if (PollRet < 0) {
      if (errno == EINTR) {
        continue;
      }
      spdlog::warn("TcpServer: recv poll() failed: {}",
                   std::strerror(errno));
      return;
    }
    if (PollRet == 0) {
      continue;
    }

    const ssize_t BytesRead = recv(client_fd, buf.data(), buf.size(), 0);
    // ... rest unchanged
  }
}
```

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

---

## Task 5: main.cpp — Add parsePort input validation (Low)

**File:** `server/src/main.cpp`, function `parsePort`

### Changes

1. Wrap `std::stoi` in try-catch for `std::exception`.
2. Validate range 1-65535.
3. On invalid input: `spdlog::error` + `std::exit(EXIT_FAILURE)`.

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

---

## Task 6: ConsoleAlertNotifier — Replace fragile ternary with switch (Low)

**File:** `server/src/console_alert_notifier.cpp`

### Changes

Replace ternary with a `switch` statement without `default`. Add unreachable `return "UNKNOWN"` after the switch for control-flow analysis.

### Result

```cpp
const char* type_str = [](AlertType type) -> const char* {
  switch (type) {
  case AlertType::ALTITUDE:
    return "ALTITUDE";
  case AlertType::SPEED:
    return "SPEED";
  }
  return "UNKNOWN";
}(transition.type);
```

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

---

## Task 7: main.cpp — Make packets_processed atomic (Low)

**File:** `server/src/main.cpp`

### Changes

1. Change `uint64_t packets_processed{0}` to `std::atomic<uint64_t> packets_processed{0}`.
2. Use `fetch_add(1, std::memory_order_relaxed)` for increment.
3. Add comment explaining intent.

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

---

## Summary

| Task | Issue | Severity | File |
|------|-------|----------|------|
| 1 | g_stop_flag atomic pointer | Medium | signal_handler.cpp |
| 2 | sigaction return unchecked | Low | signal_handler.cpp |
| 3 | setsockopt return unchecked | Low | tcp_server.cpp |
| 4 | recvLoop missing poll | Low | tcp_server.cpp |
| 5 | parsePort no validation | Low | main.cpp |
| 6 | AlertType ternary fragile | Low | console_alert_notifier.cpp |
| 7 | packets_processed not atomic | Low | main.cpp |
