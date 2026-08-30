# MiniStream Unified Role Shell

**Status:** Draft for review  
**Date:** 2026-08-30

## Goal

Ship one MiniStream application for Windows and macOS. At launch the user
chooses the current direction with a top-center mode switch:

- **Allow control**: this device is the controlled endpoint. It checks local
  capture, audio, input, and network capability, then broadcasts only after
  the user starts it.
- **Remote control**: this device is the controller. It discovers devices that
  are currently broadcasting, pairs with one selected device, displays its
  stream, and sends keyboard, mouse, and optional gamepad input.

The role is a session choice, not an operating-system identity or a permanent
assignment. A Windows or macOS device can use either mode when the required
native backend and permissions are available.

## Product flow

The application opens to one window with a centered two-segment switch. The
switch is always visible while no pairing dialog is open and has equal-width
segments with a bounded maximum width. The selected segment controls the
content below it; no role is inferred from the platform name.

### Allow control

The controlled page contains:

- the actual system type and user device name;
- Video, Audio, Input, and Network capability rows;
- the current broadcast state;
- **Allow control** / **Stop broadcast**.

The page is idle and not visible on the LAN until the user presses **Allow
control**. Starting the mode validates the platform capture, hardware encoder,
audio capture, input injection, and UDP endpoint as one transaction. A failed
step rolls back every resource and keeps the page idle with the failed
capability detail. ViGEm is optional when keyboard and mouse input are ready;
its absence is shown as an optional detail and never blocks broadcasting.

When broadcasting, the advertisement contains the real system type, device
name, session port, codec/resolution/frame-rate limits, HDR and audio flags,
and `controllable=true`. Stopping the broadcast withdraws the advertisement,
stops media, clears pairing state, and releases all input leases.

### Remote control

The remote page contains:

- **Find devices**;
- a bounded, scrollable **Nearby devices** list;
- one card per current advertisement, formatted as:

  ```text
  <system type> · <device name>
  <codec> · <width>×<height> <fps> fps · <HDR> · <audio>
  ```

- **Connect** on each card;
- after pairing, **Control remote** / **Use this device** and a compact stream
  view.

Input capability details are not rendered in the card. Keyboard and mouse are
  the baseline input path; a gamepad is optional. Long names and parameter
  lines are elided or wrapped inside the card, and the list clips and scrolls
  instead of growing outside the window.

Selecting a card binds the target address and advertised identity to the
session. The pairing page displays that identity and the six-digit comparison
code. Both peers must confirm the same code. After confirmation the stream
page title is **Controlling · <system type> · <device name>**.

## Unified application architecture

The Qt executable is named `ministream` on both platforms and loads one QML
entry point, `Main.qml`. The QML layer talks only to `RoleController`:

```text
Main.qml
  -> RoleController (Qt state, mode switch, lifecycle)
       -> ControlledRuntime (shared session/discovery state)
            -> PlatformControlledBackend
       -> RemoteRuntime (shared discovery/pairing/media state)
            -> PlatformRemoteBackend
       -> MiniStream Core (wire, crypto, packetization, FEC, queues)
```

`RoleController` owns the mode and exposes separate read-only models for the
controlled and remote pages. It is the only object QML invokes for mode
changes, broadcast, discovery, pairing, input mode, and teardown. The
controlled and remote runtimes are independent so a failed start in one mode
cannot leave resources active in the other.

The shared runtime interfaces are small and synchronous at their boundaries:

```cpp
class ControlledBackend {
 public:
  virtual ~ControlledBackend() = default;
  virtual ControlledCapabilities inspect() const = 0;
  virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  virtual std::optional<EncodedFrame> next_video() = 0;
  virtual std::optional<PcmBlock> next_audio() = 0;
  virtual bool inject_input(const DesktopInput&) = 0;
};

class RemoteBackend {
 public:
  virtual ~RemoteBackend() = default;
  virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  virtual bool configure_video(const CodecConfig&) = 0;
  virtual bool decode_video(std::span<const std::byte>, std::uint64_t) = 0;
  virtual bool play_audio(std::span<const float>) = 0;
};
```

The exact native types stay inside platform adapters. No QML or shared core
code includes DXGI, NVENC, Media Foundation, ScreenCaptureKit, VideoToolbox,
Metal, WASAPI, CoreAudio, CGEvent, or ViGEm headers.

## Platform backends

### Windows

Controlled mode uses the existing DXGI Desktop Duplication, D3D11/NVENC,
WASAPI loopback, and ViGEm input sink. The encoder remains on the GPU and
supports H.264 SDR, HEVC SDR, and HEVC Main10/P010 HDR10 when the adapter
reports those capabilities.

Remote mode adds a hardware Media Foundation/D3D11 decode path, D3D11-backed
Qt Quick presentation, WASAPI render output, and window-local keyboard/mouse
event routing. SDL3 supplies optional gamepad input. No software video
fallback is used when the hardware decoder is unavailable; the remote page
reports the capability error.

### macOS

Controlled mode uses ScreenCaptureKit for display capture, VideoToolbox for
hardware H.264/HEVC encoding, CoreAudio for 48 kHz stereo capture, and the
Accessibility-approved event injector for keyboard/mouse input. SDL3 supplies
optional gamepad input.

Remote mode uses the existing VideoToolbox decoder, CVPixelBuffer latest-frame
bridge, Metal/Qt Quick surface, CoreAudio output, and SDL3 input. The Metal
surface remains in the same Qt window as the QML overlay; fullscreen never
creates a second native video window.

The application requests permissions only when a mode first needs them:
Screen Recording and Microphone for controlled media capture, and Accessibility
for controlled input injection. A denied permission returns a direct action
such as **Open System Settings** or **Allow access**, without pretending that
the capability is ready.

## Session and role state

The shared state machine is:

```text
Idle
  ControlledReady -> Broadcasting -> Pairing -> Streaming
  RemoteBrowsing  -> Pairing -> Streaming
```

Changing the top switch from an active state first performs the same cleanup as
disconnect: stop discovery or media, close the UDP endpoint, clear pairing and
session keys, release every input lease, and return to the selected page. The
switch is never blocked by a stale remote mouse or keyboard capture.

Discovery remains a LAN hint. The versioned advertisement rejects invalid
lengths, unknown flags, empty names, zero ports, unsupported systems, and
non-controllable targets. The versioned `Hello` and `Accept` messages carry a
`HandshakeRole`; a controller sends `Controller` and a controlled endpoint
answers `Controlled`. Inverse role combinations are rejected before media
starts. Pairing and all media/control packets retain authenticated encryption.

## Input behavior

Remote input is window-local. The controller acquires separate keyboard,
mouse, and optional gamepad leases only after the user presses **Control
remote**. **Use this device**, disconnect, mode switching, window close,
pairing cancellation, failed connection, and object destruction release every
lease.

While remote input is active, Esc and F11 are forwarded to the remote
application. The local mode shortcuts are intentionally uncommon:

- Windows/Linux: `Ctrl+Alt+R` toggles remote input, `Ctrl+Alt+F` toggles
  fullscreen.
- macOS: `⌘+Option+R` toggles remote input, `⌘+Option+F` toggles fullscreen.

In local mode, Esc exits fullscreen. No global keyboard or mouse hook is
installed.

## Failure handling and observability

Every start path is transactional. If capture, codec, audio, input, permission,
bind, pairing, or renderer setup fails, the runtime stops the components it
already started and exposes one actionable status. A lost session returns to
the appropriate browsing/idle page, releases input, and leaves discovery in a
known state. Reconnect starts a fresh handshake and does not reuse stale
session keys.

The existing bounded queues, latest-frame slots, packet scheduler, FEC, jitter
buffer, drift controller, telemetry, and IDR recovery remain the hot-path
contracts. Role switching must not add an unbounded queue or a second media
copy.

## Release and compatibility

The public artifacts are one Windows installer and one macOS DMG, each
containing the unified MiniStream application and its runtime dependencies.
The installer checks the platform prerequisites and offers ViGEm installation
only when the user enables a controller path. Qt, CMake, SDKs, SDL, Opus,
libsodium, and build tools are not end-user prerequisites.

The unified role shell is an additive application contract. The discovery wire
version and handshake role version are explicit; old role-less peers are
rejected rather than silently assigned a platform role.

## Verification

- Shared unit tests cover role state transitions, cleanup, handshake roles,
  discovery filtering, packet size limits, and latest-frame ownership.
- Windows tests cover DXGI/NVENC encode, Media Foundation decode, WASAPI
  capture/output, optional ViGEm, and one Host/Remote loopback.
- macOS tests cover ScreenCaptureKit/VideoToolbox encode/decode, Metal surface,
  CoreAudio capture/output, permissions, and one local loopback where hardware
  is available.
- UI checks cover the two-segment switch, minimum/wide window sizes, long
  names, fullscreen, mode switching, pairing, disconnect, and input release.
- Release checks build one `ministream` executable per platform, deploy Qt
  runtime, create the Windows installer and macOS DMG, and record hardware
  limitations without marking unverified 4K60 HDR as passed.

## Non-goals

- Permanent role assignments or remembered target ownership;
- Internet discovery, accounts, cloud services, or NAT traversal;
- global input hooks;
- software video fallback for missing hardware paths;
- AV1, multi-controller sessions, or automatic resolution switching.
