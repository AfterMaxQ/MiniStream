# MiniStream v0.1 Implementation Plan Review

**Review date:** 2026-08-29  
**Result:** Revised before execution.

## Major findings fixed

### 1. Undefined shared types
The earlier plans referenced `Expected`, `SessionId`, `Datagram`, `VideoCodec`, `CodecConfig`, `ControlMessage`, `FecBlock` and `AudioPlayoutResult` without assigning ownership to a task.

**Fix:** Foundation now defines shared value types and project `Result<T,E>`; subsystem-local result structs are defined in the task that owns them.

### 2. Dependency integration was named but not planned
Qt/Catch2/Asio/SDL/Opus were listed in the master stack but no task owned deterministic CMake integration.

**Fix:** Foundation now owns the dependency graph. Current pinned versions include Qt 6.11.2, Catch2 3.15.3, standalone Asio 1.38.2, SDL 3.4.14, Opus 1.5.2 and libsodium 1.0.20.

### 3. No LAN authentication/integrity boundary
The previous design allowed unauthenticated INPUT/CONTROL datagrams on the home Wi-Fi.

**Fix:** Transport plan now contains first-pairing identity establishment with six-digit SAS numeric comparison, stored Ed25519 peer identities, fresh ephemeral session keys, ChaCha20-Poly1305 AEAD and replay protection.

### 4. Metal renderer and Qt frontend could have produced two incompatible window architectures
The video plan used a native `CAMetalLayer`, while the UI plan assumed QML could overlay the stream.

**Fix:** The standalone CAMetalLayer renderer is now explicitly a diagnostic probe. Product rendering uses a Qt Quick `VideoSurfaceItem` that imports CVPixelBuffer planes and allows QML overlay/full-screen controls in one window.

### 5. Audio conversion/drift steps were underspecified
“Use Windows resampling primitives” and “lightweight resampling” left implementation choices to the agent.

**Fix:** WASAPI shared-mode format conversion flags and a tightly clamped linear drift-resampler policy are specified.

### 6. Several plan steps were not executable TDD units
There were `git commit -am` commands that would omit new files, tasks with only three unchecked steps, and qualitative GUI gates such as “no obvious large regression”.

**Fix:** commit commands now stage new files, missing red/green/run steps were added, and GUI overhead gates are numeric.

## Remaining intentional risks

1. **ViGEm is retired.** It is acceptable only as a private-alpha backend and is isolated behind `VirtualGamepad`.
2. **4K60 HDR viability is hardware/network-dependent.** The profile ladder and network baseline are retained so failure is attributable rather than hidden.
3. **Qt Quick native Metal interop is one of the highest integration-risk tasks.** It now has its own explicit task and acceptance gate instead of being deferred to UI integration.
4. **First pairing still depends on the user actually comparing the six-digit SAS shown on both devices.** After confirmation the long-term Ed25519 identities are stored, so normal future sessions do not require repeated comparison.
5. **v0.1 remains large.** Execution should still proceed plan-by-plan with a fresh reviewer/subagent per task; do not ask one agent to implement all seven plans in one context.

## Execution recommendation

Safe order remains:

`00 Foundation -> 01 Transport/Security/FEC/ABR -> 02 Video/HDR -> 03 Audio -> 04 Controller -> 05 UI -> 06 Integration`

Do not start Plan 02 until Plan 01's real `netprobe` and authenticated transport gate passes.
