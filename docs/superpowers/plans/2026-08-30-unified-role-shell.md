# MiniStream Unified Role Shell Implementation Plan

> **For agentic workers:** Execute this plan inline in the current checkout. Do not dispatch subagents. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Build one Windows/macOS MiniStream application whose top-center mode switch selects a fully working controlled-device or remote-control session.

**Architecture:** A shared Qt RoleController owns the role state machine and delegates to independent controlled and remote runtimes. Shared runtimes own discovery, pairing, encrypted transport, bounded media queues, and input-lease cleanup; platform adapters own capture, hardware codec, rendering, audio, permissions, and native input APIs. One CMake target named ministream packages the same QML entry point on both platforms.

**Tech Stack:** C++20, CMake, Qt 6.11.2 + Qt Quick/QML, Catch2 3.15.3, standalone Asio 1.38.2, SDL 3.4.14, Opus 1.5.2, libsodium 1.0.20, Leopard-RS, DXGI/D3D11/NVENC/Media Foundation/WASAPI/ViGEm on Windows, ScreenCaptureKit/VideoToolbox/Metal/CoreAudio/CGEvent on macOS.

**Spec:** docs/superpowers/specs/2026-08-30-unified-role-shell-design.md

## Global Constraints

- The user chooses the current role with the top-center Allow control / Remote control switch; the choice is per session and not persisted as an operating-system mapping.
- One Qt application target named ministream is built and packaged for Windows and macOS; the QML entry point is ui/Main.qml.
- QML calls only RoleController; it never owns sockets, codecs, capture, render, audio, permission, or native input APIs.
- ControlledRuntime and RemoteRuntime have independent lifecycles and transactional start/stop; switching roles releases all leases and clears session state before constructing the next runtime.
- Windows controlled mode uses DXGI/D3D11/NVENC/WASAPI loopback and optional ViGEm; Windows remote mode uses hardware Media Foundation/D3D11 decode, WASAPI render, and window-local input.
- macOS controlled mode uses ScreenCaptureKit/VideoToolbox/CoreAudio capture and Accessibility-approved input; macOS remote mode uses VideoToolbox/CVPixelBuffer/Metal/CoreAudio output and SDL3 input.
- No software video fallback is used when a required hardware path is unavailable.
- Discovery metadata remains bounded and versioned; only controllable=true advertisements are visible, and invalid/old packets are rejected.
- Hello and Accept carry explicit HandshakeRole values (Controller and Controlled); inverse role combinations are rejected.
- All post-pairing CONTROL, INPUT, AUDIO, VIDEO, FEC, FEEDBACK, and TELEMETRY datagrams remain authenticated and encrypted, with a 1200-byte datagram limit.
- Queues and latest-frame slots are bounded; stale video and input state may be dropped, but input leases are always released.
- Remote mode never intercepts Esc or F11. Windows/Linux use Ctrl+Alt+R and Ctrl+Alt+F; macOS uses Command+Option+R and Command+Option+F.
- User-facing copy is short, factual, and limited to software state. Do not add AI, marketing, or platform-fixed role wording.
- Do not commit Qt SDKs, NVIDIA headers/binaries, driver installers, DMGs, installers, build directories, logs, captures, or performance dumps.

## File Map

- src/core/session/role.hpp/.cpp: role mode/state values and transition validation.
- src/core/session/handshake.hpp/.cpp: role-bound Hello/Accept wire messages.
- src/app/controlled/controlled_runtime.hpp/.cpp: shared controlled lifecycle, discovery advertisement, pairing, media send, and cleanup.
- src/app/remote/remote_runtime.hpp/.cpp: shared remote lifecycle, discovery list, pairing, media receive, and cleanup.
- src/app/ui/role_controller.hpp/.cpp: Qt-facing facade and properties/signals for both runtimes.
- src/platform/controlled_backend.hpp and src/platform/remote_backend.hpp: native backend interfaces.
- src/windows/platform/controlled_backend.* and src/windows/platform/remote_backend.*: Windows adapters.
- src/macos/platform/controlled_backend.* and src/macos/platform/remote_backend.*: macOS adapters.
- src/app/qt_main.cpp: shared Qt executable entry point.
- ui/Main.qml: one window, mode switch, page loader, fullscreen/input shortcuts.
- ui/components/RoleModeSwitch.qml: bounded top-center two-segment switch.
- ui/pages/ControlledPage.qml: capability and broadcast view.
- ui/pages/RemotePage.qml: discovery cards and remote-session view.
- ui/pages/PairingPage.qml and ui/pages/StreamPage.qml: shared pairing and stream states.

---

### Task 1: Shared role state and role-bound handshake

**Files:**
- Create: src/core/session/role.hpp
- Create: src/core/session/role.cpp
- Modify: src/core/session/handshake.hpp
- Modify: src/core/session/handshake.cpp
- Modify: tests/session/handshake_test.cpp
- Create: tests/session/role_test.cpp
- Modify: src/core/CMakeLists.txt
- Modify: tests/CMakeLists.txt

**Interfaces:**
- enum class RoleMode : uint8_t { Controlled = 1, Remote = 2 };
- enum class RoleState : uint8_t { Idle, ControlledReady, Broadcasting, RemoteBrowsing, Pairing, Streaming };
- bool valid_role_transition(RoleState from, RoleState to) noexcept;
- enum class HandshakeRole : uint8_t { Controller = 1, Controlled = 2 };
- Hello::sender_role and Accept::sender_role are validated by decoders.

- [ ] Step 1: Write failing role and handshake tests.

Add tests for valid transitions Idle -> ControlledReady, Idle -> RemoteBrowsing, Broadcasting -> Pairing, and Pairing -> Streaming. Add invalid transitions Idle -> Streaming and Streaming -> Broadcasting. Encode/decode a controller Hello and controlled Accept, then assert inverse role bytes are rejected and HandshakeRetrier::accept ignores an inverse Accept.

- [ ] Step 2: Run focused tests and verify failure.

~~~
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
.\build-ui\Debug\ministream_tests.exe "*role*"
.\build-ui\Debug\ministream_tests.exe "*handshake*"
~~~

Expected: compilation or assertion failures because RoleState and the role field are not available.

- [ ] Step 3: Implement the shared values and wire layout.

Use a one-byte role immediately after each handshake version byte. Keep Hello at 21 bytes and Accept at 25 bytes. decode_hello accepts only HandshakeRole::Controller; decode_accept accepts only HandshakeRole::Controlled. Include the role in equality and retry checks. valid_role_transition allows cleanup transitions from any active state to Idle and rejects starting a second active role without cleanup.

- [ ] Step 4: Run focused tests and commit.

~~~
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
.\build-ui\Debug\ministream_tests.exe "*role*"
.\build-ui\Debug\ministream_tests.exe "*handshake*"
git diff --check
git add src/core/session/role.hpp src/core/session/role.cpp src/core/session/handshake.hpp src/core/session/handshake.cpp tests/session/role_test.cpp tests/session/handshake_test.cpp src/core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(session): bind unified roles to handshake"
git push origin feat/ministream-v0.1
~~~

### Task 2: Platform contracts and shared controlled runtime

**Files:**
- Create: src/platform/capabilities.hpp
- Create: src/platform/controlled_backend.hpp
- Create: src/platform/remote_backend.hpp
- Create: src/app/controlled/controlled_runtime.hpp
- Create: src/app/controlled/controlled_runtime.cpp
- Create: tests/session/controlled_runtime_test.cpp
- Modify: src/core/CMakeLists.txt
- Modify: tests/CMakeLists.txt

**Interfaces:**
- struct PlatformCapability { bool ready; std::string detail; };
- struct ControlledCapabilities { PlatformCapability video, audio, input, network; PlatformCapability optional_gamepad; bool ready() const noexcept; };
- class ControlledBackend { virtual ControlledCapabilities inspect() const = 0; virtual bool start() = 0; virtual void stop() noexcept = 0; virtual std::optional<EncodedFrame> next_video() = 0; virtual std::optional<PcmBlock> next_audio() = 0; virtual ~ControlledBackend() = default; };
- class ControlledRuntime exposes inspect(), start(), stop(), hosting(), pairing(), pairing_code(), advertisement(), and tick().

- [ ] Step 1: Write failing runtime lifecycle tests.

Use a fake ControlledBackend to assert that start() does not advertise until every required capability is ready, a backend failure calls stop() exactly once, and stop() clears discovery, session keys, pairing, media queues, and input leases. A missing optional gamepad leaves ControlledCapabilities::ready() true.

- [ ] Step 2: Run the focused test and verify failure.

~~~
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
.\build-ui\Debug\ministream_tests.exe "*controlled runtime*"
~~~

Expected: the fake backend cannot compile because the shared runtime contracts do not exist.

- [ ] Step 3: Implement the contracts and runtime.

Move common portions of the current Windows host controller into ControlledRuntime: bind UDP, generate identity and ephemeral keys, start DiscoveryHost, answer only while hosting() is true, perform role-bound Hello/Accept, derive session keys, and run MediaSender and its scheduler. Keep native frame/audio acquisition behind ControlledBackend. On every failure call stop() and return one actionable capability detail. tick() processes at most one input datagram and one video/audio batch per invocation.

- [ ] Step 4: Run focused test and commit.

~~~
.\build-ui\Debug\ministream_tests.exe "*controlled runtime*"
git diff --check
git add src/platform src/app/controlled src/core/CMakeLists.txt tests/session/controlled_runtime_test.cpp tests/CMakeLists.txt
git commit -m "feat(session): add shared controlled runtime"
git push origin feat/ministream-v0.1
~~~

### Task 3: Shared remote runtime and discovery model

**Files:**
- Create: src/app/remote/remote_runtime.hpp
- Create: src/app/remote/remote_runtime.cpp
- Modify: src/core/session/discovery.hpp
- Modify: src/core/session/discovery.cpp
- Modify: src/macos/ui/client_controller.cpp
- Modify: src/macos/ui/client_controller.hpp
- Create: tests/session/remote_runtime_test.cpp
- Modify: tests/session/discovery_test.cpp
- Modify: tests/CMakeLists.txt

**Interfaces:**
- class RemoteBackend { virtual PlatformCapability inspect() const = 0; virtual bool start() = 0; virtual void stop() noexcept = 0; virtual bool configure_video(const CodecConfig&) = 0; virtual bool decode_video(std::span<const std::byte>, uint64_t) = 0; virtual bool play_audio(std::span<const float>) = 0; virtual ~RemoteBackend() = default; };
- class RemoteRuntime exposes refresh(), hosts(), connect(index), confirm_pairing(), cancel_pairing(), toggle_input(), release_input(), connected(), pairing(), streaming(), selected_host(), and tick().
- format_discovered_host(const DiscoveredHost&) remains the only formatter used by QML.

- [ ] Step 1: Write failing remote lifecycle tests.

Use fake discovery and backend objects to assert that only controllable advertisements become list entries, selecting a host sends a HandshakeRole::Controller Hello, a controlled Accept is required before pairing, and failed connect/cancel/disconnect release keyboard, mouse, and gamepad leases.

- [ ] Step 2: Run focused test and verify failure.

~~~
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
.\build-ui\Debug\ministream_tests.exe "*remote runtime*"
~~~

Expected: compilation failure because the runtime and fake boundaries are not present.

- [ ] Step 3: Implement the runtime.

Move common portions of the current macOS client controller into RemoteRuntime: discover and retain full advertisements, bind the selected address, send the role-bound Hello, validate the controlled Accept, run numeric pairing confirmation, create MediaReceiver, and pass encoded video to RemoteBackend::decode_video and decoded PCM to RemoteBackend::play_audio. Keep latest-frame ownership and jitter/drift limits unchanged. release_input() calls both RemoteInputRouter::end() and InputCapture::leave_remote().

- [ ] Step 4: Run focused tests and commit.

~~~
.\build-ui\Debug\ministream_tests.exe "*remote runtime*"
.\build-ui\Debug\ministream_tests.exe "*discovered device formatting*"
git diff --check
git add src/app/remote src/core/session/discovery.hpp src/core/session/discovery.cpp src/macos/ui/client_controller.cpp src/macos/ui/client_controller.hpp tests/session/remote_runtime_test.cpp tests/session/discovery_test.cpp tests/CMakeLists.txt
git commit -m "feat(session): add shared remote runtime"
git push origin feat/ministream-v0.1
~~~

### Task 4: Unified Qt controller and mode switch

**Files:**
- Create: src/app/ui/role_controller.hpp
- Create: src/app/ui/role_controller.cpp
- Create: src/app/qt_main.cpp
- Create: ui/Main.qml
- Create: ui/components/RoleModeSwitch.qml
- Create: ui/pages/ControlledPage.qml
- Create: ui/pages/RemotePage.qml
- Create: ui/pages/StreamPage.qml
- Modify: ui/pages/PairingPage.qml
- Modify: CMakeLists.txt
- Modify: src/windows/CMakeLists.txt
- Modify: src/macos/CMakeLists.txt
- Modify: tests/ui/shortcut_policy_test.py
- Create: tests/ui/role_shell_copy_test.py

**Interfaces:**
- Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged).
- Q_PROPERTY(int state READ state NOTIFY stateChanged).
- Q_PROPERTY(QObject* controlled READ controlled CONSTANT) and Q_PROPERTY(QObject* remote READ remote CONSTANT).
- Q_INVOKABLE setMode(int), startBroadcast(), stopBroadcast(), findDevices(), connectToDevice(int), confirmPairing(), cancelPairing(), toggleRemoteInput(), releaseRemoteInput(), and toggleFullscreen().
- RoleModeSwitch.qml has required property int mode, signal modeSelected(int), and width Math.min(parent.width - 32, 360).

- [ ] Step 1: Write failing shell and copy checks.

Extend the shortcut test to require ui/Main.qml, both platform shortcuts, and disabled F11/Esc actions while remote input is active. Add copy checks for Allow control, Remote control, Find devices, Nearby devices, Control remote, and Use this device, and absence of Windows PCs, This Mac, Gamepad, and Controller in discovery cards.

- [ ] Step 2: Run checks and verify failure.

~~~
python tests/ui/shortcut_policy_test.py
python tests/ui/role_shell_copy_test.py
~~~

Expected: ui/Main.qml and the unified role copy are missing.

- [ ] Step 3: Implement the Qt facade and QML shell.

Register RoleController with the MiniStream QML module from src/app/qt_main.cpp. The facade creates both runtimes for the current platform, exposes read-only models, and performs cleanup before a mode change. Main.qml places RoleModeSwitch at the top center, loads ControlledPage or RemotePage, opens PairingPage and StreamPage for either runtime, and binds the approved fullscreen/input shortcuts. ControlledPage contains only capabilities and broadcast controls. RemotePage contains only discovery, pairing, stream, and input controls. Use bounded columns, wrapped parameter lines, and clipped ListView delegates.

- [ ] Step 4: Update CMake to one application target.

Replace separate Qt ministream_host and ministream_client targets with one ministream target per platform. Keep each platform's native backend sources in its target, link shared app/controller sources, and install the same QML module. Non-Qt diagnostic targets remain available for tests.

- [ ] Step 5: Run checks, build, and commit.

~~~
python tests/ui/shortcut_policy_test.py
python tests/ui/role_shell_copy_test.py
python tools/check_ui_copy.py ui
cmake --build build-ui --config Debug --parallel 4 --target ministream
git diff --check
git add src/app/ui src/app/qt_main.cpp ui/Main.qml ui/components ui/pages CMakeLists.txt src/windows/CMakeLists.txt src/macos/CMakeLists.txt tests/ui
git commit -m "feat(ui): add unified role shell"
git push origin feat/ministream-v0.1
~~~

### Task 5: Windows controlled and remote backends

**Files:**
- Create: src/windows/platform/controlled_backend.hpp/.cpp
- Create: src/windows/platform/remote_backend.hpp/.cpp
- Create: src/windows/video/mf_decoder.hpp/.cpp
- Create: src/windows/video/d3d11_video_surface.hpp/.cpp
- Create: src/windows/audio/wasapi_output.hpp/.cpp
- Create: src/windows/input/window_input_source.hpp/.cpp
- Modify: src/windows/platform/host_capabilities.cpp
- Modify: src/windows/CMakeLists.txt
- Create: tests/windows/remote_backend_test.cpp
- Modify: tests/windows/host_capabilities_test.cpp

**Interfaces:**
- WindowsControlledBackend implements ControlledBackend with existing DxgiCapture, NvencEncoder, WasapiLoopback, RemoteInputSink, and optional VirtualGamepad.
- WindowsRemoteBackend implements RemoteBackend with hardware Media Foundation/D3D11 decode, a latest-frame D3D11 surface, WASAPI render, and window-local DesktopInput forwarding. It reports a direct error when hardware decode or the output device cannot start.

- [ ] Step 1: Add failing backend capability tests.

Test controlled capability inspection reports keyboard/mouse as required and ViGEm as optional. Test remote backend rejects an unsupported codec, does not allocate a second frame queue, and releases WASAPI output and D3D11 surface on stop().

- [ ] Step 2: Run focused Windows tests and verify failure.

~~~
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
.\build-ui\Debug\ministream_tests.exe "*Windows*"
~~~

Expected: the remote backend symbols are missing.

- [ ] Step 3: Implement controlled adapter.

Wrap existing host components without copying frames to CPU. Map NVENC configuration and CodecConfig parameter sets to the shared runtime; read WASAPI loopback blocks in bounded 10 ms chunks; inject authenticated desktop input through RemoteInputSink; create ViGEm only when available.

- [ ] Step 4: Implement hardware remote adapter.

Create a D3D11 Media Foundation hardware decoder, import decoded textures into a Qt Quick-compatible latest-frame surface, add a bounded WASAPI render ring buffer, and feed window-local keyboard/mouse events to RemoteInputRouter. SDL3 gamepad input remains optional.

- [ ] Step 5: Run focused tests, build, and commit.

~~~
.\build-ui\Debug\ministream_tests.exe "*Windows*"
.\build-ui\Debug\ministream_tests.exe "*NVENC*"
git diff --check
git add src/windows/platform/controlled_backend.* src/windows/platform/remote_backend.* src/windows/video/mf_decoder.* src/windows/video/d3d11_video_surface.* src/windows/audio/wasapi_output.* src/windows/input/window_input_source.* src/windows/platform/host_capabilities.cpp src/windows/CMakeLists.txt tests/windows/remote_backend_test.cpp tests/windows/host_capabilities_test.cpp
git commit -m "feat(windows): add unified controlled and remote backends"
git push origin feat/ministream-v0.1
~~~

### Task 6: macOS controlled and remote backends

**Files:**
- Create: src/macos/platform/controlled_backend.hpp/.mm
- Create: src/macos/platform/remote_backend.hpp/.mm
- Create: src/macos/video/videotoolbox_encoder.hpp/.mm
- Create: src/macos/video/screencapturekit_capture.hpp/.mm
- Create: src/macos/audio/coreaudio_capture.hpp/.mm
- Create: src/macos/input/accessibility_input.hpp/.mm
- Modify: src/macos/video/video_surface_bridge.mm
- Modify: src/macos/video/videotoolbox_decoder.mm
- Modify: src/macos/audio/coreaudio_output.mm
- Modify: src/macos/CMakeLists.txt
- Create: tests/macos/remote_backend_test.mm
- Create: tests/macos/controlled_backend_test.mm

**Interfaces:**
- MacControlledBackend implements ScreenCaptureKit display capture, VideoToolbox hardware encode, CoreAudio capture, Accessibility-approved input injection, and optional SDL3 gamepad polling.
- MacRemoteBackend implements the existing VideoToolbox decoder, VideoSurfaceBridge, Metal texture import, CoreAudio output, and SDL3 input/rumble. It keeps the newest CVPixelBufferRef only.

- [ ] Step 1: Add failing macOS backend tests.

Add compile-time and fake-device tests for permission-denied capability reports, hardware-only codec selection, latest-frame replacement, bounded CoreAudio output, and teardown of ScreenCaptureKit/VideoToolbox/CoreAudio objects.

- [ ] Step 2: Run the macOS test target where available.

On a macOS checkout with Xcode and Qt configured:

~~~
cmake --build build-macos --config Debug --parallel --target ministream_tests
./build-macos/tests/ministream_tests "*macOS*"
~~~

On Windows, verify only that shared headers and CMake source lists remain platform-guarded; do not claim macOS hardware validation.

- [ ] Step 3: Implement controlled capture, encode, audio, and input.

Request Screen Recording, Microphone, and Accessibility permissions only when controlled mode starts. Use ScreenCaptureKit frames directly in the VideoToolbox encoder, CoreAudio 48 kHz stereo capture, and approved CGEvent input injection. Return actionable permission details without a software fallback.

- [ ] Step 4: Complete remote decode, render, and output.

Configure VideoToolbox from CodecConfig, publish only the newest pixel buffer to VideoSurfaceBridge, import NV12/P010 with CVMetalTextureCache, apply Rec.709/BT.2020/PQ in the Metal shader, and output decoded Opus through a bounded CoreAudio ring buffer. Keep the QML overlay in the same window.

- [ ] Step 5: Commit the macOS boundary.

~~~
git diff --check
git add src/macos/platform src/macos/video src/macos/audio src/macos/input src/macos/CMakeLists.txt tests/macos
git commit -m "feat(macos): add unified controlled and remote backends"
git push origin feat/ministream-v0.1
~~~

### Task 7: Session integration, permissions, and cleanup

**Files:**
- Modify: src/app/ui/role_controller.cpp
- Modify: src/app/controlled/controlled_runtime.cpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/core/input/remote_input_router.cpp
- Modify: src/core/input/input_capture.cpp
- Modify: ui/Main.qml
- Modify: ui/pages/ControlledPage.qml
- Modify: ui/pages/RemotePage.qml
- Modify: ui/pages/PairingPage.qml
- Modify: ui/pages/StreamPage.qml
- Modify: tests/input/remote_input_router_test.cpp
- Create: tests/session/role_controller_test.cpp

**Interfaces:**
- RoleController::setMode() performs releaseRemoteInput(), stops the old runtime, clears pairing/session keys, and emits modeChanged only after the new page is safe to use.
- RoleController::statusText() exposes one actionable failure string per mode; permission actions use Q_INVOKABLE openPermissionSettings().

- [ ] Step 1: Write failing cleanup and transition tests.

Test mode changes from broadcasting and streaming, window-close cleanup, pairing cancellation, failed backend start, and object destruction. Assert InputCapture::remote() and every device bit are false after each case.

- [ ] Step 2: Implement the single cleanup path.

Route disconnect, stop, mode switch, close, failed pairing, and failed backend paths through one RoleController::resetSession(). Keep RemoteInputRouter::end() as the lease boundary; do not add global hooks. Make Esc/F11 shortcuts conditional on remoteInputActive so remote games keep their normal menu/fullscreen behavior.

- [ ] Step 3: Add permission actions and state copy.

Expose details such as Screen Recording permission required and Accessibility permission required, with one button that opens platform settings. Keep the mode switch usable after denial and never mark a denied capability green.

- [ ] Step 4: Run focused tests and commit.

~~~
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
.\build-ui\Debug\ministream_tests.exe "*input*"
.\build-ui\Debug\ministream_tests.exe "*role controller*"
python tests/ui/shortcut_policy_test.py
git diff --check
git add src/app/ui/role_controller.cpp src/app/controlled/controlled_runtime.cpp src/app/remote/remote_runtime.cpp src/core/input/remote_input_router.cpp src/core/input/input_capture.cpp ui/Main.qml ui/pages tests/input/remote_input_router_test.cpp tests/session/role_controller_test.cpp
git commit -m "feat(session): release input on every role transition"
git push origin feat/ministream-v0.1
~~~

### Task 8: Build, UI acceptance, packaging, and documentation

**Files:**
- Modify: README.md
- Modify: docs/superpowers/specs/2026-08-30-unified-role-shell-design.md
- Inspect: docs/superpowers/plans/2026-08-30-unified-role-shell.md
- Modify: packaging/CMakeLists.txt
- Modify: packaging/installer.nsi
- Modify: packaging/Info.plist.in
- Create: tests/integration/unified_loopback_test.cpp
- Modify: tests/CMakeLists.txt

- [ ] Step 1: Add one local loopback integration test.

Start fake controlled and remote runtimes on loopback, complete role-bound Hello/Accept and six-digit confirmation, send one encrypted video, audio, and input packet, then disconnect and assert input capture is clear. Exercise one FEC loss and one IDR request without running a second complete soak.

- [ ] Step 2: Run complete source checks once.

~~~
cmake --build build-ui --config Debug --parallel 4 --target ministream_tests ministream
.\build-ui\Debug\ministream_tests.exe "[.hardware]"
ctest --test-dir build-ui -C Debug --output-on-failure
python tests/ui/shortcut_policy_test.py
python tests/ui/role_shell_copy_test.py
python tools/check_ui_copy.py ui
~~~

- [ ] Step 3: Perform Windows Computer Use acceptance once.

Launch the unified Qt executable with Computer Use. Inspect the minimum and wide window, verify the top-center switch and both page layouts, click Allow control, confirm the advertisement appears only then, stop it, switch to Remote control, inspect a long-name discovery card, enter pairing, verify the six-digit code, toggle Control remote / Use this device, press the approved shortcuts, and confirm input returns locally. Start/stop again only if the first lifecycle action exposes a concrete failure; do not repeat a passing flow.

- [ ] Step 4: Build release artifacts.

On Windows configure CPack/NSIS for one MiniStream-Setup.exe containing the unified executable, Qt runtime, MSVC runtime, libsodium, and the optional ViGEm installer. On macOS configure one MiniStream.app and DMG containing the Qt runtime and native frameworks. Verify package file lists without committing generated artifacts.

- [ ] Step 5: Update stable documentation and commit.

README opens with the architecture and describes only the current unified application flow, prerequisites, permissions, role switch, pairing, input shortcuts, fullscreen, build, and release commands. It contains no local paths, progress notes, unverified claims, or AI wording. The spec status changes to Approved for implementation only after implementation is complete; unverified macOS hardware and 4K60 HDR remain outside the Windows acceptance claim.

Run:

~~~
git diff --name-status
git diff -- README.md docs/superpowers/specs/2026-08-30-unified-role-shell-design.md docs/superpowers/plans/2026-08-30-unified-role-shell.md
git diff --check
git status --short
git log -1 --oneline --decorate
~~~

Commit and push:

~~~
git add README.md docs/superpowers/specs/2026-08-30-unified-role-shell-design.md docs/superpowers/plans/2026-08-30-unified-role-shell.md packaging/CMakeLists.txt packaging/installer.nsi packaging/Info.plist.in tests/integration/unified_loopback_test.cpp tests/CMakeLists.txt
git commit -m "feat(release): package unified MiniStream roles"
git push origin feat/ministream-v0.1
~~~

## Handoff

Plan complete and saved to docs/superpowers/plans/2026-08-30-unified-role-shell.md. Execute it inline with checkpoints after each task; do not use subagents. Windows verification is performed in this checkout. macOS compilation, permissions, DMG, and hardware acceptance are performed on a Mac before claiming cross-platform release acceptance.
