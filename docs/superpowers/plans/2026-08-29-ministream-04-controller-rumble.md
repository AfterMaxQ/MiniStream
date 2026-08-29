# MiniStream 04 Controller and Rumble Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow one SDL3-compatible controller connected to the Mac to control Windows games through a virtual Xbox-style controller, with rumble returning to the physical controller.

**Architecture:** Client maps SDL3 state into one normalized shared `GamepadState`, coalesces axis churn for ~1 ms and sends latest-state INPUT packets. Windows maps that state into a narrow `VirtualGamepad` wrapper. The wrapper owns whichever Windows virtual-controller dependency is selected; the network/UI never sees driver-specific APIs.

**Tech Stack:** SDL 3.4.14, Windows virtual gamepad integration.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

## Driver decision

Do **not** make paid `libvirtualhid` a mandatory v0.1 dependency. Its current Windows driver path requires licensing. Do **not** spread retired ViGEm-specific types through MiniStream either.

For the first local alpha, use **ViGEmBus 1.22.0 + ViGEmClient 1.16.18.0** as the concrete Xbox 360 backend because it is free and sufficient for a private Windows test machine. Both upstream projects are retired, so every ViGEm symbol must remain inside `src/windows/input/vigem_gamepad.cpp`; protocol, session and QML code may depend only on `VirtualGamepad`. A later backend replacement must not require a wire-protocol change.

---

### Task 1: Normalized gamepad state and wire packet

**Files:**
- Create: `src/core/input/gamepad_state.hpp`
- Create: `src/core/input/gamepad_packet.hpp`
- Create: `src/core/input/gamepad_packet.cpp`
- Create: `tests/input/gamepad_packet_test.cpp`

**Interfaces:**
```cpp
struct GamepadState {
  std::uint32_t buttons;
  std::uint16_t left_trigger;
  std::uint16_t right_trigger;
  std::int16_t left_x, left_y;
  std::int16_t right_x, right_y;
};

struct GamepadPacket {
  std::uint32_t sequence;
  std::uint64_t client_timestamp_us;
  GamepadState state;
};
```

- [ ] Test exact wire round-trip and axis extremes.
- [ ] Test old sequence rejection using wrap-safe sequence comparison.
- [ ] Implement.
- [ ] Commit.

---

### Task 2: SDL3 controller capture on macOS

**Files:**
- Create: `src/macos/input/sdl_gamepad.hpp`
- Create: `src/macos/input/sdl_gamepad.mm`
- Create: `tools/gamepad_probe/main.cpp`

**Interfaces:**
```cpp
class SdlGamepad {
 public:
  std::optional<GamepadState> poll_latest();
  bool rumble(std::uint16_t low, std::uint16_t high, Microseconds duration);
};
```

- [ ] Enumerate one connected controller.
- [ ] Print normalized state in `gamepad_probe`.
- [ ] Verify dead-zone handling is not silently added; raw normalized state goes over wire unless configured later.
- [ ] Test local rumble manually through probe.
- [ ] Commit.

---

### Task 3: 1 ms state coalescer

**Files:**
- Create: `src/core/input/input_coalescer.hpp`
- Create: `src/core/input/input_coalescer.cpp`
- Create: `tests/input/input_coalescer_test.cpp`

**Interfaces:**
```cpp
class InputCoalescer {
 public:
  void update(GamepadState, SteadyClock::time_point);
  std::optional<GamepadPacket> flush_if_due(SteadyClock::time_point);
};
```

- [ ] Simulate 20 axis events within 1 ms.
- [ ] Assert one packet is emitted containing the latest full state.
- [ ] Assert button edge is not delayed beyond the same 1 ms window.
- [ ] Implement.
- [ ] Commit.

---

### Task 4: Windows `VirtualGamepad` boundary

**Files:**
- Create: `src/windows/input/virtual_gamepad.hpp`
- Create: `src/windows/input/virtual_gamepad.cpp`
- Create: `src/windows/input/vigem_gamepad.cpp`
- Create: `src/windows/input/vigem_gamepad.hpp`
- Create: `tests/windows/virtual_gamepad_contract_test.cpp`

**Interfaces:**
```cpp
struct RumbleState { std::uint16_t low, high; };

class VirtualGamepad {
 public:
  Result<void, InputError> start();
  Result<void, InputError> submit(const GamepadState&);
  void set_rumble_callback(std::function<void(RumbleState)>);
  void stop();
};
```

- [ ] Build a fake backend contract test first.
- [ ] Implement an Xbox 360 backend with ViGEmClient 1.16.18.0 in `vigem_gamepad.cpp`; `virtual_gamepad.cpp` owns the MiniStream-facing wrapper and contains no ViGEm headers.
- [ ] Verify Windows Game Controllers panel/test program sees one controller.
- [ ] Verify submit changes buttons/sticks.
- [ ] Commit.

---

### Task 5: End-to-end INPUT priority path

**Files:**
- Modify client input sender
- Modify host input receiver
- Modify packet scheduler tests

- [ ] Queue a synthetic 4K-sized burst of VIDEO packets, then enqueue INPUT.
- [ ] Assert INPUT is selected before remaining VIDEO.
- [ ] Send real controller state Mac->Windows.
- [ ] Measure client event timestamp to host submit timestamp using synced clocks; expose p50/p95 transport+dispatch latency.
- [ ] Commit.

---

### Task 6: Rumble return channel

**Files:**
- Create: `src/core/input/rumble_packet.hpp`
- Modify Windows virtual gamepad
- Modify macOS SDL gamepad

**Interfaces:**
```cpp
struct RumblePacket {
  std::uint16_t low;
  std::uint16_t high;
  std::uint16_t duration_ms;
};
```

- [ ] Unit-test wire format.
- [ ] Trigger output feedback on Windows virtual controller and send FEEDBACK packet.
- [ ] Client invokes SDL rumble.
- [ ] Verify in a real game/controller.
- [ ] Commit.

---

## Controller acceptance gate

- one physical Mac controller recognized via SDL3
- Windows exposes one Xbox-style virtual controller
- buttons/sticks/triggers work in a real game
- INPUT is not blocked by video bursts
- basic rumble returns to physical controller
- no driver-specific type leaks outside `src/windows/input/virtual_gamepad.cpp`
