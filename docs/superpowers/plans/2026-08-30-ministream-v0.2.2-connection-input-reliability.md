# MiniStream v0.2.2 Connection & Input Reliability Implementation Plan

> Implementation uses `superpowers:executing-plans`, test-driven development, and verification-before-completion. Work is executed serially without subagents.

**Goal:** Make Windows/macOS peers interpret input identically and make every connection stage converge after packet loss, process exit, or network loss without changing the existing media architecture.

**Architecture:** Keep one UDP session socket, the existing SAS/AEAD/media pipeline, and one controller per host. Add a versioned neutral key contract, a small ordered reliable envelope only for input ownership edges, bounded state leases, confirmation grace, authenticated liveness, and platform cleanup. Use default production timings with short injectable test timings.

**Spec:** `docs/superpowers/specs/2026-08-30-ministream-v0.2.2-connection-input-reliability-design.md`

## Constraints

- Work on `codex/v0.2.2-reliability` and push each verified phase.
- Start every behavior change with a focused failing test.
- Preserve encrypted session traffic, replay protection, current media/FEC/ABR flow, and single-controller semantics.
- Never claim macOS, permission, firewall, Wi-Fi, hardware codec, or cross-device gates from mock/loopback tests.
- Do not implement a cursor-warp substitute for raw relative mouse input.
- Generated builds, installers, checksums, and local test evidence stay untracked.
- Inspect `git diff --name-status`, every public-doc diff, and the complete staged diff before each delivery commit.

## Task 1: Version the neutral keyboard contract

**Create:**
- `src/core/input/desktop_key.hpp`
- `src/core/input/desktop_key.cpp`
- `src/windows/input/desktop_key_windows.hpp`
- `src/windows/input/desktop_key_windows.cpp`

**Modify:**
- `src/core/input/desktop_input.hpp/.cpp`
- `src/core/protocol/wire.hpp`
- `src/core/session/handshake.cpp`
- `src/core/security/pairing_wire.cpp`
- `src/core/session/discovery.cpp`
- `src/windows/input/window_input_source.cpp`
- `src/windows/input/remote_input_sink.hpp/.cpp`
- `src/macos/input/accessibility_input.hpp/.mm`
- platform/core/test CMake files and protocol/input tests

1. Add failing tests for Qt W/A/S/D/Space/punctuation/modifiers to HID, HID to Windows scan code, wire-owned release flags, protocol-version rejection, and macOS mapping contracts.
2. Implement only the shared HID enum and pure maps needed by those tests.
3. Change both input sources to emit HID and both sinks to translate HID locally.
4. Bump common, handshake, pairing, transcript, and discovery compatibility versions; add tests that old-version bytes are rejected.
5. Run focused input/protocol/security/discovery tests, then the full C++ suite.
6. Commit and push: `fix(input): use a platform-neutral keyboard protocol`.

## Task 2: Make critical input edges ordered and releasable

**Create:**
- `src/core/input/reliable_desktop_input.hpp`
- `src/core/input/reliable_desktop_input.cpp`
- `tests/input/reliable_desktop_input_test.cpp`

**Modify:**
- `src/core/input/desktop_input.*`
- `src/core/session/session_control.hpp`
- `src/core/input/remote_input_router.*`
- `src/platform/controlled_backend.hpp`
- both runtimes
- Windows/macOS controlled backends and native input sinks
- integration and platform tests

1. Add failing codec tests for reliable edge sequence/ack and invalid lengths.
2. Add failing receiver tests for duplicate suppression, out-of-order buffering, bounded window, injection failure, and wrap-safe ordering.
3. Implement a 4-byte sequence envelope and ack payload. Use `ReliableControl` for 20/40/80 ms retries; re-seal every retry so AEAD replay protection remains intact.
4. Route Key, MouseButton, and ReleaseAll through the reliable path. Keep MouseMove, MouseWheel, and gamepad lossy.
5. Add `clear_input()` to the controlled backend contract. Track held keys/buttons in both native sinks and release them on every ownership-loss path.
6. Disconnect on retry exhaustion or a full reliable window; reset sender/receiver state per session.
7. Extend the loopback test to assert canonical key semantics, ReleaseAll, ack convergence, and exactly-once injection.
8. Run focused input/integration tests and the full suite.
9. Commit and push: `fix(input): make ownership edges reliable and releasable`.

## Task 3: Make handshake, pairing, and peer filtering converge

**Create:**
- `src/core/session/session_timing.hpp`

**Modify:**
- `src/core/net/udp_endpoint.hpp/.cpp`
- `src/app/controlled/controlled_runtime.hpp/.cpp`
- `src/app/remote/remote_runtime.hpp/.cpp`
- session/net/integration tests

1. Add failing tests for an abandoned Hello unlocking, a 60-second human pairing lease, confirmation after the old one-second retry window, final-confirmation loss recovery in both directions, and prefetched foreign datagram rejection.
2. Add `UdpEndpoint::matches_peer()` and recheck each prefetched packet after any state change that locks the endpoint.
3. Add a two-second host handshake lease and a 60-second pairing lease to both runtimes. Validate that an initiator pairing nonce matches the accepted Hello.
4. Stop treating exhausted confirmation retries as a pairing failure; only the pairing lease cancels human confirmation.
5. Add one-second post-pair confirmation grace with bounded repeats and replies while Streaming.
6. Poll discovery only while Broadcasting and unlocked; verify a second controller cannot discover a busy host.
7. Run focused session/net/integration tests and the full suite.
8. Commit and push: `fix(session): converge handshake and pairing state`.

## Task 4: Add authenticated session liveness

**Modify:**
- `src/core/session/session_control.hpp`
- both runtime headers/implementations
- session-control and integration tests

1. Add failing heartbeat codec tests and short-timing loopback tests for a silent controller and silent host.
2. Send an authenticated host heartbeat every 500 ms. Treat valid Remote feedback/input/control packets as controller activity.
3. Refresh activity only after successful AEAD authentication. Initialize the deadline when Streaming begins.
4. After three seconds of silence, return Controlled to Broadcasting and Remote to RemoteBrowsing, clearing keyboard, mouse, gamepad, rumble, crypto, and peer lock.
5. Verify clean Disconnect loss is recovered by the same timeout path.
6. Run focused liveness/integration tests and the full suite.
7. Commit and push: `fix(session): expire silent authenticated peers`.

## Task 5: Correct capability preflight and bounded playout

**Modify:**
- `src/windows/video/dxgi_capture.hpp/.cpp`
- `src/windows/platform/host_capabilities.cpp`
- `src/windows/platform/controlled_backend.cpp`
- `src/app/controlled/controlled_runtime.cpp`
- `src/app/remote/remote_runtime.cpp`
- Windows capability/backend and audio/integration tests

1. Add failing pure/contract tests that Windows does not advertise HEVC from DLL presence alone and that a started backend reports only codecs preflighted on its active D3D11 device.
2. Expose capture output dimensions and preflight H.264/HEVC sessions at the profile dimensions actually offered. Refresh the runtime advertisement after backend start.
3. Reject an unsupported codec before peer lock and Accept. Do not add a new fallback negotiation in this release.
4. Add a failing playout-clock test for a delayed tick, then play at most four due 10 ms frames and resynchronize a larger lag.
5. Run focused Windows/audio tests, hardware-tagged tests only when available, then the full suite.
6. Commit and push: `fix(platform): advertise proven codecs and bound audio catch-up`.

## Task 6: Separate role UI and add a local escape path

**Create:**
- `ui/pages/ControlledActivePage.qml`

**Modify:**
- `ui/Main.qml`
- `ui/pages/StreamPage.qml`
- `cmake/QtUi.cmake`
- `README.md`
- UI policy tests

1. Add failing source-contract tests that StreamPage is Remote-only, Controlled Streaming has its own page, and the reserved exit shortcut remains enabled during remote input.
2. Add the controlled active page with stable status copy and Disconnect only.
3. Keep Ctrl+Alt+R / Command+Option+R as the entry shortcut. Add Ctrl+Alt+Shift+R / Command+Option+Shift+R as the always-local exit chord while remote input is active.
4. On application focus loss, call `releaseRemoteInput()` so ReleaseAll is sent before local ownership resumes.
5. Update README only for stable compatibility, shortcut, and release-all behavior. Do not add execution notes.
6. Build the UI and run UI policy tests plus the full suite.
7. Commit and push: `fix(ui): separate controlled sessions and local input escape`.

## Task 7: Fault simulation and release gate

**Modify/Create only as supported by final public scope:**
- integration tests and test helpers
- `docs/release/v0.2.2-connection-input-reliability-checklist.md`
- `CMakeLists.txt`
- `tests/packaging/version_contract_test.py`

1. Add deterministic loopback scenarios for dropped/late Hello, delayed human confirmation, final-confirmation recovery, silent peers, busy discovery, reliable KeyUp/MouseUp, ReleaseAll, and reconnect.
2. Run Debug and Release C++ suites, Python contract tests, `ctest`, `git diff --check`, and tracked-file/diff audits.
3. Configure and build the Release Windows UI, run the applicable hidden hardware probes, and package NSIS.
4. Use Computer Use to launch the built app and installed app, verify role switching, controlled/remote page separation, shortcuts/focus cleanup where observable, and clean exit. Do not claim a second-device stream from one-machine UI checks.
5. Install/uninstall the Windows package, verify executable path, Private-profile firewall rule lifecycle, running process cleanup, and SHA-256.
6. Bump project version to 0.2.2, write durable release checklist/notes, commit, push, fast-forward `main`, tag `v0.2.2`, and publish the Windows installer plus checksum. Mark macOS DMG pending.
7. Produce the Mac handoff prompt: remove/stop v0.1.0, checkout v0.2.2, build/test/package/sign as available, install, run Windows/macOS four-direction input and failure-recovery checks, then upload the DMG to the existing release.

## Verification commands

Use the locally configured dependency sources when network FetchContent is unavailable. Representative commands from the isolated worktree are:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build-v022 -G 'Visual Studio 17 2022' -A x64 -DMINISTREAM_BUILD_UI=OFF -DMINISTREAM_ENABLE_PACKAGING=OFF <local FetchContent overrides>
& 'C:\Program Files\CMake\bin\cmake.exe' --build build-v022 --config Debug --target ministream_tests -- /m:1
& '.\build-v022\tests\Debug\ministream_tests.exe' --reporter compact
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build-v022 -C Debug --output-on-failure
python tests/ui/role_shell_copy_test.py
python tests/ui/shortcut_policy_test.py
python tests/packaging/packaging_contract_test.py
python tests/packaging/version_contract_test.py
git diff --check
git diff --name-status v0.2.1...HEAD
```
