# Dynamic Device Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user explicitly broadcast a controllable device, discover it from another device, see its actual system identity and stream capabilities, and pair without fixed Windows/macOS role labels.

**Architecture:** Extend the existing versioned LAN discovery packet with bounded identity, role, and capability metadata. Keep the current platform entry points, but drive labels and target selection from the decoded advertisement. The controlled-device action owns discovery visibility; the controller renders a bounded, scrollable list and carries the selected identity into pairing.

**Tech Stack:** C++20, standalone Asio, Catch2, Qt 6.11.2, Qt Quick/QML.

**Spec:** `docs/superpowers/specs/2026-08-30-dynamic-device-discovery-design.md`

## Global Constraints

- The user chooses **Allow control** before the device answers discovery queries.
- Discovery metadata is versioned, bounded, and rejects unknown flags and invalid lengths.
- User-visible identity is system type plus device name.
- The parameter line contains codec, resolution, frame rate, HDR, and audio only; input details stay hidden.
- Long identity and parameter strings remain inside the card at the minimum window size and after resizing.
- Existing authenticated pairing, encrypted media, input lease release, and real capability checks remain in force.
- A ViGEm gamepad is optional; keyboard/mouse control must not be blocked by a missing gamepad driver.
- In remote mode, Esc and F11 pass through to the remote application; `Ctrl+Alt+R`/`⌘+Option+R` switches input mode. In local mode, F11 toggles fullscreen and Esc leaves fullscreen.
- Do not add global input hooks, a new service framework, or permanent role assignments.
- No subagents; work inline and commit each functional boundary.

---

### Task 1: Versioned discovery identity and capabilities

**Files:**
- Modify: `src/core/session/discovery.hpp`
- Modify: `src/core/session/discovery.cpp`
- Modify: `tests/session/discovery_test.cpp`

**Interfaces:**
- Add `enum class DiscoverySystem : std::uint8_t { Unknown, Windows, MacOS, Linux };`.
- Add `struct DiscoveryCapabilities` with `h264`, `hevc`, `hdr10`, `audio`, `keyboard_mouse`, and `gamepad` booleans.
- Extend `DiscoveryAdvertisement` with system, device name, session port, capabilities, maximum width/height/fps, and `controllable`.
- Extend `DiscoveredHost` with the decoded identity and capability fields plus address.
- Keep `encode_discovery_advertisement` and `decode_discovery_advertisement` as the wire boundary.

- [ ] **Step 1: Write the failing wire tests.**

Construct a Windows advertisement with H.264/HEVC, 3840x2160@60, HDR10, audio,
and keyboard/mouse capability; assert round-trip equality. Assert decoding
rejects a non-controllable advertisement, unknown flags, zero port,
empty/49-byte name, and truncated parameters. Assert discovered hosts preserve
the decoded fields.

- [ ] **Step 2: Run the focused test and verify failure.**

Run:

~~~powershell
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
./build-ui/Debug/ministream_tests.exe "*LAN discovery*"
~~~

Expected: compilation or assertion failures because the new fields do not yet
exist.

- [ ] **Step 3: Implement the bounded wire format.**

Use discovery protocol version 2 and set `kMaxDiscoveryBytes` to 128. Encode:

~~~text
magic[4], version[1], type[1], flags[1], system[1], port[2],
max_width[2], max_height[2], max_fps[2], name_length[1], name[0..48]
~~~

Define the known flag mask for controllable, H.264, HEVC, HDR10, audio,
keyboard/mouse, and gamepad. Encode integers big-endian, reject unknown flags,
unsupported systems, invalid bounds, and advertisements without
`controllable`. Copy all decoded metadata into `DiscoveredHost`. Make
`DiscoveryHost::poll` return without sending when its advertisement is not
controllable.

- [ ] **Step 4: Run the focused test and verify it passes.**

Run the same focused command. Expected: all discovery tests pass.

- [ ] **Step 5: Commit the protocol boundary.**

~~~powershell
git add src/core/session/discovery.hpp src/core/session/discovery.cpp tests/session/discovery_test.cpp
git commit -m "feat(discovery): advertise dynamic device identity and capabilities"
git push origin feat/ministream-v0.1
~~~

### Task 2: Controlled-device broadcast lifecycle

**Files:**
- Modify: `src/windows/platform/host_capabilities.hpp`
- Modify: `src/windows/platform/host_capabilities.cpp`
- Modify: `src/windows/ui/host_controller.hpp`
- Modify: `src/windows/ui/host_controller.cpp`
- Modify: `ui/pages/HostHomePage.qml`
- Modify: `ui/pages/PairingPage.qml`
- Modify: `tests/windows/host_capabilities_test.cpp`

**Interfaces:**
- Keep `HostController::startHost()` and `stopHost()` as the QML actions.
- Add `Q_PROPERTY(QString deviceLabel READ deviceLabel CONSTANT)` and
  `Q_PROPERTY(QString broadcastStatus READ broadcastStatus NOTIFY hostingChanged)`.
- Add `QString HostController::deviceLabel() const` and
  `QString HostController::broadcastStatus() const`.
- Treat keyboard/mouse injection as the required input capability; ViGEm is
  optional metadata and must not make `HostCapabilities::ready()` false.

- [ ] **Step 1: Add failing capability/advertisement tests.**

Test that a host with working video, audio, keyboard/mouse, and network is
ready even when the optional gamepad driver is absent. Test the advertisement
helper produces the system type, device name, stream profile, input flags, and
`controllable=true`.

- [ ] **Step 2: Run the focused Windows test and verify failure.**

~~~powershell
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
./build-ui/Debug/ministream_tests.exe "*host*"
~~~

- [ ] **Step 3: Build the advertisement and gate broadcast.**

Use `QSysInfo::kernelType()` for the short system enum and
`QSysInfo::machineHostName()` for the first-version device name, capped at 48
bytes. Map real host capabilities and the selected stream profile to the
advertisement. Call `DiscoveryHost::poll` only while `hosting_` is true.
A readiness failure leaves broadcasting off and preserves the specific
capability detail.

- [ ] **Step 4: Replace fixed role copy.**

Use only concise, state-based strings:

~~~text
<system type> · <device name>
Not visible on local network
Visible on local network
Allow control
Stop broadcast
~~~

Use a bounded one-line identity text and a status line that can wrap. Change
the pairing instruction to “Confirm the same code on both devices.”

- [ ] **Step 5: Run the copy check and commit.**

~~~powershell
python tools/check_ui_copy.py ui
git add src/windows/platform/host_capabilities.hpp src/windows/platform/host_capabilities.cpp src/windows/ui/host_controller.hpp src/windows/ui/host_controller.cpp ui/pages/HostHomePage.qml ui/pages/PairingPage.qml tests/windows/host_capabilities_test.cpp
git commit -m "feat(host): require explicit control broadcast"
git push origin feat/ministream-v0.1
~~~

### Task 3: Dynamic controller discovery cards

**Files:**
- Modify: `src/macos/ui/client_controller.hpp`
- Modify: `src/macos/ui/client_controller.cpp`
- Modify: `ui/pages/ClientHomePage.qml`
- Modify: `ui/pages/PairingPage.qml`

**Interfaces:**
- Add `Q_PROPERTY(QString selectedDeviceLabel READ selectedDeviceLabel NOTIFY selectedDeviceChanged)`.
- Add `QString ClientController::selectedDeviceLabel() const` and
  `void selectedDeviceChanged()`.
- Keep `QStringList hosts()`; each element is a plain-text two-line card
  generated from `DiscoveredHost`.

- [ ] **Step 1: Add failing formatting tests.**

Add a pure formatter that returns:

~~~text
<system type> · <device>
<codec> · <width>×<height> <fps> fps · <HDR> · <audio>
~~~

Cover H.264, HEVC, HDR10, SDR, audio, no-audio, and a long name. Assert that
the result contains no controller/gamepad field.

- [ ] **Step 2: Run the focused test and verify failure.**

Run the focused discovery/controller filter. Expected: the formatter is missing
or the old single-name model fails.

- [ ] **Step 3: Preserve decoded advertisements and filter targets.**

In `refreshHosts()`, keep only `controllable=true` advertisements, format
their two-line cards, and retain the matching `DiscoveredHost` vector for
`connectToHost(index)`. Set `selectedDeviceLabel` before sending `Hello`
and clear it on disconnect.

- [ ] **Step 4: Make the QML list bounded.**

Use a delegate `Column` with width equal to the card width minus margins.
Elide the identity line, wrap the parameter line to at most two lines, and set
delegate height from `implicitHeight + 24`. Bound the card to
`Math.min(280, Math.max(96, hostList.contentHeight))`, keep `clip: true`,
and let `ListView` scroll when there are many devices. Bind every text width
to the card width so long values cannot escape the container.

- [ ] **Step 5: Replace fixed controller copy and commit.**

Use **Nearby devices**, **Find devices**, and **Connect**. Show the selected
identity on the pairing page and use **Controlling · <system> · <device>** for
the connected-state label. Do not render controller/gamepad parameters.

~~~powershell
python tools/check_ui_copy.py ui
git add src/macos/ui/client_controller.hpp src/macos/ui/client_controller.cpp ui/pages/ClientHomePage.qml ui/pages/PairingPage.qml
git commit -m "feat(client): show dynamic discovered device cards"
git push origin feat/ministream-v0.1
~~~

### Task 4: Pairing direction and cleanup assertions

**Files:**
- Modify: `src/core/session/handshake.hpp`
- Modify: `src/core/session/handshake.cpp`
- Modify: `tests/session/handshake_test.cpp`
- Modify: `tests/input/remote_input_router_test.cpp`
- Modify: `src/macos/ui/client_controller.cpp`
- Modify: `src/windows/ui/host_controller.cpp`

**Interfaces:**
- Add a versioned role field to `Hello` and `Accept`; preserve retry/accept
  APIs and reject an inverse role combination.
- Keep `RemoteInputRouter::end()` and `InputCapture::leave_remote()` as
  the cleanup boundary.

- [ ] **Step 1: Write failing direction tests.**

Encode/decode a controller hello targeting a controllable peer, reject an
inverse role combination, and verify input leases release when a target
disconnects or stops broadcasting.

- [ ] **Step 2: Run the focused tests and verify failure.**

~~~powershell
./build-ui/Debug/ministream_tests.exe "*handshake*"
./build-ui/Debug/ministream_tests.exe "*remote input*"
~~~

- [ ] **Step 3: Bind roles to the existing handshake.**

Add the smallest versioned role field, validate it in decoders, and include it
in equality/retry checks. The controller sends the selected target metadata;
the controlled endpoint accepts only a controllable target. Never use role
metadata as a cryptographic key.

- [ ] **Step 4: Release on every lifecycle edge.**

Release remote input before clearing the selected device, on failed pairing,
disconnect, `stopHost()`, and window close. Stop discovery polling before
tearing down media services.

- [ ] **Step 5: Run focused tests and commit.**

~~~powershell
./build-ui/Debug/ministream_tests.exe "*handshake*"
./build-ui/Debug/ministream_tests.exe "*remote input*"
git add src/core/session/handshake.hpp src/core/session/handshake.cpp tests/session/handshake_test.cpp tests/input/remote_input_router_test.cpp src/macos/ui/client_controller.cpp src/windows/ui/host_controller.cpp
git commit -m "feat(session): bind control direction to pairing"
git push origin feat/ministream-v0.1
~~~

### Task 5: Full verification and documentation check

**Files:**
- Inspect: `README.md`
- Inspect: `docs/superpowers/specs/2026-08-30-dynamic-device-discovery-design.md`
- Inspect: `ui/pages/HostHomePage.qml`
- Inspect: `ui/pages/ClientHomePage.qml`

- [ ] **Step 1: Run source and hardware checks once.**

~~~powershell
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
./build-ui/Debug/ministream_tests.exe "[.hardware]"
ctest --test-dir build-ui -C Debug --output-on-failure
~~~

- [ ] **Step 2: Run UI copy and overflow checks.**

~~~powershell
python tools/check_ui_copy.py ui
~~~

Launch the Qt host with Computer Use, inspect the minimum window size, resize
to a wide window, and verify the identity and parameter lines stay inside the
card. Exercise Allow control, Stop broadcast, Find devices, and pairing with a
long-name fixture.

- [ ] **Step 3: Inspect the complete documentation diff.**

Run `git diff --name-status`, inspect every changed documentation file in full,
and run `git diff --check`. Remove any session-only status text from README.

- [ ] **Step 4: Commit and push the final verification boundary.**

~~~powershell
git status --short
git log -1 --oneline --decorate
git push origin feat/ministream-v0.1
~~~
