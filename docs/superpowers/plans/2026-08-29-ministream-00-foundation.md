# MiniStream 00 Foundation and Contracts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create the cross-platform repository skeleton, deterministic shared protocol types, bounded-queue primitives, timing/logging contracts and test harness that every later subsystem consumes.

**Architecture:** Shared code contains no DXGI, NVENC, VideoToolbox, Metal, WASAPI or SDL dependencies. Platform executables link the shared libraries but initially do nothing beyond startup/self-test. No streaming implementation is introduced in this plan.

**Tech Stack:** C++20, CMake, Qt 6.11.2, Catch2 3.15.3, standalone Asio 1.38.2, SDL 3.4.14, Opus 1.5.2, libsodium 1.0.20.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

## Global Constraints

- Shared core remains platform-neutral.
- No unbounded queue.
- Network integers are big-endian on the wire.
- UDP datagram cap constant is 1200 bytes.
- Monotonic time is used for durations.

---

### Task 1: Repository and build graph

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/Warnings.cmake`
- Create: `src/core/CMakeLists.txt`
- Create: `src/windows/CMakeLists.txt`
- Create: `src/macos/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `src/windows/main.cpp`
- Create: `src/macos/main.mm`
- Create: `.gitignore`

**Interfaces:**
- Produces targets: `ministream_core`, `ministream_host`, `ministream_client`, `ministream_tests`

- [ ] **Step 1: Add a failing smoke test target**

  Create `tests/smoke_test.cpp`:
  ```cpp
  #include <catch2/catch_test_macros.hpp>
  TEST_CASE("test harness is alive") { REQUIRE(2 + 2 == 4); }
  ```

- [ ] **Step 2: Configure Catch2 and verify the test target is initially missing**

  Run:
  ```bash
  cmake -S . -B build
  cmake --build build --target ministream_tests
  ```
  Expected before target definition: CMake/build fails because `ministream_tests` is not defined.

- [ ] **Step 3: Add CMake targets**

  Root configuration must:
  ```cmake
  cmake_minimum_required(VERSION 3.30)
  project(MiniStream VERSION 0.1.0 LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  enable_testing()
  add_subdirectory(src/core)
  if(WIN32)
    add_subdirectory(src/windows)
  elseif(APPLE)
    enable_language(OBJCXX)
    add_subdirectory(src/macos)
  endif()
  add_subdirectory(tests)
  ```

- [ ] **Step 4: Build and run**

  Run:
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure
  ```
  Expected: smoke test passes.

- [ ] **Step 5: Commit**
  ```bash
  git add CMakeLists.txt cmake src tests .gitignore
  git commit -m "build: bootstrap cross-platform CMake project"
  ```

---


### Task 2: Pin dependencies and define shared value/error types

**Files:**
- Create: `cmake/Dependencies.cmake`
- Create: `src/core/base/result.hpp`
- Create: `src/core/protocol/value_types.hpp`
- Create: `src/core/video/codec_config.hpp`
- Create: `tests/base/result_test.cpp`
- Modify: root `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  using SessionId = std::uint32_t;
  using ControlSeq = std::uint32_t;

  enum class VideoCodec : std::uint8_t { H264 = 1, Hevc = 2 };

  struct Datagram {
    std::vector<std::byte> bytes;
  };

  struct CodecConfig {
    VideoCodec codec;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t fps;
    bool hdr10;
    std::vector<std::byte> parameter_sets;
  };

  template<class T, class E>
  class Result;

  template<class E>
  class Result<void, E>;
  ```

**Dependency policy:**
- `find_package(Qt6 6.11.2 REQUIRED COMPONENTS Quick Qml QuickControls2)` only in UI/app targets.
- FetchContent/pinned source for Catch2 3.15.3, standalone Asio 1.38.2, SDL 3.4.14, Opus 1.5.2 and libsodium 1.0.20.
- Leopard-RS remains pinned in the transport plan.
- NVIDIA Video Codec SDK remains external and is found from `NV_VIDEO_CODEC_SDK_ROOT`.

- [ ] **Step 1: Write failing `Result` and core-type tests**

  Test value success, error success, `Result<void,E>`, `SessionId` width, and `kMaxDatagramBytes` compatibility.

- [ ] **Step 2: Run the focused test target and verify it fails**

  ```bash
  cmake --build build --target ministream_tests
  ctest --test-dir build -R "base|types" --output-on-failure
  ```

- [ ] **Step 3: Implement `Result` as a small `std::variant`-backed project type**

  Do not add another expected/monadic library only for this wrapper. Expose `has_value()`, `value()`, `error()` and static `ok(...)` / `err(...)`.

- [ ] **Step 4: Centralize dependency declarations and configure both platforms**

  No source directory may contain ad-hoc `FetchContent_Declare`.

- [ ] **Step 5: Reconfigure from an empty build directory and run tests**

  ```bash
  rm -rf build
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure
  ```

  PowerShell equivalent:
  ```powershell
  Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure
  ```

- [ ] **Step 6: Commit**
  ```bash
  git add cmake/Dependencies.cmake CMakeLists.txt src/core/base src/core/protocol/value_types.hpp src/core/video tests
  git commit -m "build: pin dependencies and define shared value types"
  ```

---

### Task 3: Wire protocol primitives

**Files:**
- Create: `src/core/protocol/types.hpp`
- Create: `src/core/protocol/wire.hpp`
- Create: `src/core/protocol/wire.cpp`
- Create: `tests/protocol/wire_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class PacketType : std::uint8_t;
  struct CommonHeader;
  constexpr std::uint32_t kProtocolMagic;
  constexpr std::uint8_t kProtocolVersion;
  constexpr std::size_t kMaxDatagramBytes = 1200;
  std::array<std::byte, 12> encode_common_header(const CommonHeader&);
  std::optional<CommonHeader> decode_common_header(std::span<const std::byte>);
  ```

- [ ] **Step 1: Write protocol round-trip and rejection tests**

  Cover:
  - exact byte order
  - bad magic
  - bad protocol version
  - undersized buffer
  - unknown packet type

- [ ] **Step 2: Run only protocol tests and verify failure**
  ```bash
  ctest --test-dir build -R protocol --output-on-failure
  ```
  Expected: compile failure because protocol API does not exist.

- [ ] **Step 3: Implement minimal deterministic wire encoder/decoder**

  Do not use `reinterpret_cast` of packed C++ structs as the wire format. Write bytes explicitly with big-endian helpers.

- [ ] **Step 4: Run tests**
  ```bash
  cmake --build build --parallel
  ctest --test-dir build -R protocol --output-on-failure
  ```

- [ ] **Step 5: Commit**
  ```bash
  git add src/core/protocol tests/protocol
  git commit -m "feat(protocol): add versioned common packet header"
  ```

---

### Task 4: Monotonic clock and sample statistics

**Files:**
- Create: `src/core/time/clock.hpp`
- Create: `src/core/telemetry/window_stats.hpp`
- Create: `src/core/telemetry/window_stats.cpp`
- Create: `tests/telemetry/window_stats_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  using SteadyClock = std::chrono::steady_clock;
  using Microseconds = std::chrono::microseconds;

  class WindowStats {
   public:
    explicit WindowStats(std::size_t capacity);
    void push(double value);
    double mean() const;
    double percentile(double p) const;
    std::size_t size() const;
  };
  ```

- [ ] **Step 1:** Test mean, p50, p95, bounded capacity and empty behavior.
- [ ] **Step 2:** Run test; verify compile failure.
- [ ] **Step 3:** Implement fixed-capacity rolling samples. No background thread.
- [ ] **Step 4:** Run `ctest --test-dir build -R telemetry --output-on-failure`.
- [ ] **Step 5:** Commit:
  ```bash
  git add src/core/time src/core/telemetry tests/telemetry
git commit -m "feat(telemetry): add bounded rolling statistics"
  ```

---

### Task 5: Bounded latest-state and FIFO queues

**Files:**
- Create: `src/core/concurrency/latest_value.hpp`
- Create: `src/core/concurrency/bounded_queue.hpp`
- Create: `tests/concurrency/queue_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  template<class T> class LatestValue;
  template<class T> class BoundedQueue;
  ```
  `LatestValue<T>` keeps at most one unread value.  
  `BoundedQueue<T>` rejects or drops according to an explicit `OverflowPolicy`.

- [ ] **Step 1:** Test overwrite semantics for `LatestValue`.
- [ ] **Step 2:** Test `DropOldest` and `RejectNewest` behavior for `BoundedQueue`.
- [ ] **Step 3:** Implement only the tested semantics using mutex + condition_variable; do not build a custom lock-free queue.
- [ ] **Step 4:** Run concurrency tests 100 times:
  ```bash
  for i in {1..100}; do ctest --test-dir build -R concurrency --output-on-failure || exit 1; done
  ```
  On PowerShell:
  ```powershell
  1..100 | % { ctest --test-dir build -R concurrency --output-on-failure; if ($LASTEXITCODE) { exit 1 } }
  ```
- [ ] **Step 5:** Commit:
  ```bash
  git add src/core/concurrency tests/concurrency
  git commit -m "feat(core): add explicit bounded queue primitives"
  ```

---

### Task 6: Session/application state contracts

**Files:**
- Create: `src/core/session/session_state.hpp`
- Create: `src/core/session/capabilities.hpp`
- Create: `tests/session/session_state_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class SessionState {
    Idle, Connecting, Negotiating, Streaming, Recovering, Disconnecting, Failed
  };

  struct DeviceCapabilities {
    bool h264;
    bool hevc;
    bool hdr10;
    bool rumble;
    std::uint32_t max_width;
    std::uint32_t max_height;
    std::uint32_t max_fps;
  };
  ```

- [ ] **Step 1:** Test legal state transitions using a pure `can_transition(from,to)` function.
- [ ] **Step 2:** Verify illegal transitions such as `Idle -> Streaming` fail.
- [ ] **Step 3:** Implement explicit transition table; no implicit ordinal comparisons.
- [ ] **Step 4:** Run session tests.
- [ ] **Step 5:** Commit:
  ```bash
  git add src/core/session tests/session
  git commit -m "feat(session): define stable session and capability contracts"
  ```

---

## Foundation acceptance gate

On both Windows and macOS:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Pass when:
- both platform binaries start
- all shared tests pass
- `ministream_core` has no platform multimedia dependency
- no queue type in shared code is unbounded by default
