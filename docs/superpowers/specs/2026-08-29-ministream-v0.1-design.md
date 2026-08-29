# MiniStream v0.1 Playable Alpha Design

**Status:** Approved for implementation planning  
**Date:** 2026-08-29

## Goal

Build one lightweight cross-platform project whose Windows runtime acts as the game-streaming host and whose macOS runtime acts as the client. The first playable alpha must support a real living-room gaming session over a single Wi‑Fi 6 router where both endpoints are wireless.

## Product target

- Windows host -> Wi‑Fi 6 router -> macOS client -> TV
- 4K60 target
- HEVC Main10
- HDR10
- NVENC hardware encode
- VideoToolbox hardware decode
- Metal presentation
- Stereo game audio
- One controller
- Basic rumble feedback
- UDP-first low-latency transport
- packet reordering
- FEC
- IDR recovery
- adaptive bitrate
- queue-aware congestion response
- telemetry
- modern Qt 6 / QML desktop frontend

## Non-goals for v0.1

- Internet/NAT traversal
- accounts/cloud
- GameStream compatibility
- multi-host fleet management
- multi-controller
- 5.1/7.1/Atmos
- AV1
- DualSense adaptive trigger/touchpad/gyro features
- automatic resolution switching
- browser/Web UI
- software encode/decode fallback


## Build and dependency policy

Pinned/open-source dependencies for v0.1:
- Qt 6.11.2 (external Qt installation, discovered with `find_package`)
- Catch2 3.15.3
- standalone Asio 1.38.2
- SDL 3.4.14
- Opus 1.5.2
- libsodium 1.0.20
- Leopard-RS pinned to commit `6e5725ebdf9da4370b0bcc4f70fa8eb66f4e6198`
- ViGEmClient 1.16.18.0 for the private Windows alpha backend; ViGEmBus 1.22.0 installed separately on the Windows test machine

NVIDIA Video Codec SDK is an external developer prerequisite referenced through `NV_VIDEO_CODEC_SDK_ROOT`; SDK binaries are not vendored.

All dependency versions live in one CMake dependency file. Platform/media source files may not download dependencies on their own.

## Design principles

1. **Latest frame wins.** Stale video and stale controller state are disposable.
2. **No unbounded queues.** Every queue has an explicit maximum size/time and drop policy.
3. **Keep raw video on hardware paths.** Windows raw frames stay in GPU/D3D/NVENC; Mac decode output stays in CVPixelBuffer/Metal.
4. **Telemetry is a product feature.** Performance changes must be measurable.
5. **Latency before quality.** When Wi‑Fi degrades, reduce bitrate/FEC strategy before accumulating delay.
6. **One project, two runtimes.** Windows and macOS share protocol, state, telemetry and UI model code where practical.
7. **No speculative abstraction.** Platform boundaries and protocol boundaries are explicit; factories/backends are added only when a second implementation exists.

## Core architecture

```text
Windows Host
  DXGI Desktop Duplication
    -> D3D11 texture
    -> color conversion (NV12/P010)
    -> NVENC H.264/HEVC
    -> video packetizer
    -> FEC
    -> packet scheduler
    -> UDP

  WASAPI loopback
    -> Opus
    -> audio packets
    -> packet scheduler
    -> UDP

  UDP input/control receive
    -> virtual gamepad sink
    -> game
    -> rumble feedback
    -> control/feedback packets

                      Wi‑Fi 6

macOS Client
  UDP
    -> reassembly/FEC
    -> VideoToolbox
    -> CVPixelBuffer
    -> CVMetalTexture
    -> Metal/CAMetalLayer
    -> TV

  UDP audio
    -> small jitter buffer
    -> Opus
    -> CoreAudio
    -> HDMI/TV

  SDL3 gamepad
    -> latest-state packets
    -> UDP
```

## Transport

Logical channels over one session:
- CONTROL
- VIDEO
- VIDEO_FEC
- AUDIO
- INPUT
- FEEDBACK
- TELEMETRY

Every post-pairing datagram is authenticated and encrypted with a per-session AEAD key. Packet headers required for routing/versioning remain authenticated associated data; media/control payloads are encrypted.

Default datagram cap: **1200 bytes**.

Reliability policy:
- VIDEO: FEC, stale-frame drop, on-demand IDR
- AUDIO: bounded jitter buffer, Opus packet-loss concealment; no long retransmission
- INPUT: latest state wins; no retransmission
- CONTROL: small reliable messages with sequence/ACK/retry
- TELEMETRY: best effort

Priority:
1. INPUT
2. CONTROL
3. AUDIO
4. VIDEO / VIDEO_FEC
5. TELEMETRY

## Congestion control

The first controller is deliberately simple and inspectable.

Every 100 ms, consume:
- host send-queue delay
- RTT / jitter
- receiver loss
- unrecoverable FEC count
- dropped frames
- receiver backlog

Overload if any is true:
- send queue > 4 ms
- loss > 1.0%
- unrecoverable video frame in current feedback window

Reaction:
- bitrate *= 0.85 immediately
- clamp to configured minimum

Stable if for 2 s:
- send queue < 2 ms
- loss < 0.2%
- jitter < 3 ms
- no unrecoverable frame

Recovery:
- +1 Mbps per second
- clamp to configured maximum

FEC starting policy:
- loss < 0.1% -> 3%
- 0.1%..0.5% -> 5%
- 0.5%..1.0% -> 10%
- >1.0% -> 15%

These thresholds are configuration constants and telemetry-visible, not hidden heuristics.


## Pairing and session security

MiniStream is LAN-first but must not trust every device on the Wi-Fi network.

v0.1 uses:
- libsodium 1.0.20
- ephemeral X25519 key exchange via `crypto_kx`
- first-pairing numeric comparison (SAS): both Host and Client display the same six-digit code derived from the ephemeral-key/nonces transcript
- the user must explicitly confirm that the two displayed codes match
- no six-digit secret is used as a cryptographic key
- derived directional session keys
- `crypto_aead_chacha20poly1305_ietf_*` for packet confidentiality + integrity
- monotonically increasing 64-bit per-direction packet nonce/counter; nonce reuse is a fatal session error
- replay window of 1024 authenticated packet sequence numbers

The six-digit SAS is not secret and is never used as a key. It is a human comparison value that binds the first unauthenticated key exchange. After explicit confirmation, each peer stores the other device's long-term Ed25519 public identity. Future sessions sign/authenticate fresh ephemeral key material with the stored identity before deriving new directional session keys.

CONTROL, INPUT, AUDIO, VIDEO, FEC and TELEMETRY all use authenticated packets. This avoids a separate unauthenticated fast path that could inject controller state or terminate a session.

Pairing/session crypto remains behind `src/core/security/`; transport callers do not construct nonces or call libsodium directly.

## Time synchronization

Use NTP-style four-timestamp exchange:
- client t0
- host receive t1
- host send t2
- client receive t3

Estimate:
- RTT = (t3 - t0) - (t2 - t1)
- clock offset = ((t1 - t0) + (t2 - t3)) / 2

Maintain a rolling window and use the lowest-RTT recent sample as the preferred clock offset.

## Video profiles

Development order:
1. 1080p60 H.264 SDR
2. 1440p60 SDR
3. 4K60 SDR
4. HEVC Main10
5. 4K60 HDR10

Final v0.1 target:
- 3840x2160 @ 60 fps
- HEVC Main10
- P010
- HDR10 / BT.2020 / PQ ST.2084
- low-latency NVENC
- B-frames 0
- lookahead 0
- short encoder queue
- IDR on session start and recovery request

## Audio

- WASAPI loopback
- 48 kHz stereo
- Opus
- 10 ms initial packet duration
- 20 ms maximum normal client jitter target
- bounded audio queue
- gradual drift correction, never by growing an unbounded buffer

## Controller

Client:
- SDL3
- one controller
- buttons, sticks, triggers, rumble
- event driven + ~1 ms coalescing
- full latest state packet

Host:
- one narrow `VirtualGamepad` platform wrapper
- v0.1 target: Xbox-style virtual controller
- local alpha backend: ViGEmBus 1.22.0 + ViGEmClient 1.16.18.0
- ViGEm is retired, so all ViGEm symbols remain behind the Windows input boundary and are replaceable later without changing the wire protocol
- output feedback becomes rumble messages to client

## Desktop frontend

Framework:
- C++20 backend
- Qt 6 + Qt Quick/QML
- Qt Quick Controls with custom design tokens

Architecture:
```text
QML
 -> SessionController / SettingsModel / TelemetryModel / DeviceModel / CapabilityModel
 -> MiniStream Core
```

QML must not directly manipulate sockets, DXGI, NVENC, VideoToolbox, Metal, WASAPI or virtual controller APIs.

### Design language

Keywords:
- calm
- precise
- native
- technical
- quiet
- fast

Visual rules:
- neutral surfaces + one accent color
- no purple/blue/cyan gradient identity
- no decorative glow
- no glassmorphism as the base style
- no card-within-card dashboards
- no excessive pill controls or huge radii
- no landing-page hero
- no emoji icons
- no decorative mesh gradients, particles or fake charts
- technical feel comes from real state and metrics
- system fonts
- 4 px spacing grid
- radii: 6 / 10 / 14 px
- motion: 120-220 ms and must respect reduced-motion preferences
- system/light/dark appearance
- telemetry UI refresh 4-10 Hz; charts <=30 Hz

Copy rules:
- short, factual, falsifiable
- no “AI-powered”, “magic”, “supercharge”, sparkle wording
- errors state what happened, impact, and next useful action

Information architecture:
- Home
- Stream
- Performance
- Settings
- in-stream overlay

Settings:
- Streaming
- Video
- Audio
- Controller
- Network
- Application
- Advanced

Default presets:
- Automatic
- Quality
- Balanced
- Low Latency
- Custom

Advanced telemetry is progressively disclosed.


## Qt Quick / video-surface integration

The streaming renderer and the desktop UI share one application window on macOS.

The client does **not** create an independent unmanaged `CAMetalLayer` window once the Qt frontend is integrated. Instead:

- `VideoSurfaceItem` is a custom Qt Quick scene-graph item owned by QML.
- The render node consumes only the latest retained `CVPixelBufferRef`.
- On macOS, the render implementation imports the pixel-buffer planes through `CVMetalTextureCache`.
- Rendering happens on the Qt Quick render thread / scene-graph lifecycle, not on the network or VideoToolbox callback thread.
- The QML in-stream overlay is a normal sibling item above `VideoSurfaceItem`, so full-screen controls do not require a second native overlay window.
- The decoder-to-UI handoff is a bounded latest-frame slot.
- The pre-UI video probe may use a standalone `CAMetalLayer`; that probe is disposable and is not the final product window architecture.

The application layer owns full-screen/window state. The renderer owns texture conversion and drawing only.

## Acceptance

A v0.1 release candidate passes only if:
- 4K60 HEVC Main10 HDR stream works end-to-end on target Windows/Mac machines
- hardware encode/decode paths are verified
- stereo audio remains stable without increasing drift
- controller controls a real Windows game
- rumble returns to client controller
- FEC recovers injected packet loss
- IDR recovery repairs unrecoverable reference damage
- bitrate controller responds to injected/network congestion without growing queue delay
- 60-minute real game session has no crash, unbounded memory growth, queue growth or deadlock
- GUI exposes clear connection/health states and does not materially affect stream latency
