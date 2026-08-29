# MiniStream v0.1 Playable Alpha Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a playable Windows-to-macOS game-streaming alpha with 4K60 HDR video, stereo audio, controller + rumble, Wi‑Fi-aware FEC/ABR, telemetry and a modern Qt/QML desktop UI.

**Architecture:** One C++20 repository produces Windows Host and macOS Client runtimes. Shared code owns session protocol, transport, telemetry and application-state contracts; platform folders own hardware capture/encode/decode/render/audio/input. Work is split into focused plans so each subsystem can be reviewed and benchmarked independently.

**Tech Stack:** C++20, CMake, Qt 6.11.2 + Qt Quick/QML, Catch2 3.15.3, standalone Asio 1.38.2, SDL 3.4.14, Opus 1.5.2, libsodium 1.0.20, Leopard-RS, DXGI/D3D11, NVIDIA Video Codec SDK/NVENC, VideoToolbox/CoreVideo/Metal, WASAPI, CoreAudio.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

## Global Constraints

- Windows host and macOS client are built from one repository.
- No Web UI, Electron, Tauri, account system, cloud or GameStream compatibility.
- C++ language level is C++20.
- Qt desktop frontend uses Qt Quick/QML and never owns streaming hot-path work.
- UDP datagram target is <=1200 bytes.
- Every queue must have a maximum size/time and explicit drop policy.
- Video and input use latest-state semantics; stale state is disposable.
- Raw Windows frames must not make a GPU->CPU->GPU round-trip before NVENC.
- Decoded macOS frames must reach Metal through CVPixelBuffer/CVMetalTexture rather than CPU RGB copies.
- Software video encode/decode fallback is not accepted for v0.1.
- Telemetry is mandatory from the first end-to-end stream.
- First pairing authenticates the peers; post-pairing datagrams are AEAD-protected and replay-checked.
- v0.1 final target is 3840x2160 @ 60 fps, HEVC Main10, HDR10, stereo audio, one controller, rumble.
- Automatic resolution switching, AV1, multi-controller and Internet transport remain out of scope.
- UI must follow the non-AI design-language constraints in the spec.

---

## Why this is split into separate plans

The approved v0.1 contains several independently reviewable systems. A single giant plan would force an implementation agent to reason about DXGI, FEC, audio, QML and virtual HID simultaneously. Instead, implement these plans in order.

## Dependency graph

```text
00 Foundation / contracts
        |
        +------------------+
        |                  |
01 Transport/FEC/ABR       |
        |                  |
        +-------> 02 Video |
        |                  |
        +-------> 03 Audio |
        |                  |
        +-------> 04 Input |
        |                  |
        +------------------+
                 |
          05 Desktop UI
                 |
          06 Integration / Alpha
```

## Plan files

1. `2026-08-29-ministream-00-foundation.md`
2. `2026-08-29-ministream-01-transport-fec-abr.md`
3. `2026-08-29-ministream-02-video-hdr.md`
4. `2026-08-29-ministream-03-audio.md`
5. `2026-08-29-ministream-04-controller-rumble.md`
6. `2026-08-29-ministream-05-desktop-ui.md`
7. `2026-08-29-ministream-06-integration-alpha.md`

## Review gates

Do not merge a later plan merely because it compiles. Each plan has its own acceptance gate:

| Plan | Gate |
|---|---|
| 00 | Windows + macOS builds and shared unit tests pass |
| 01 | authenticated netprobe + loss injection + FEC + ABR simulations pass |
| 02 | 1080p60 first, then 4K60 HDR hardware stream passes |
| 03 | 30-minute stereo loopback stream with bounded jitter/drift passes |
| 04 | one real controller controls a Windows test/game and rumble returns |
| 05 | Host/Client UI drives fake + real application models without hot-path coupling |
| 06 | 60-minute real game session and fault-injection matrix pass |

## Commit policy

Use one commit per independently reviewable task. Preferred prefixes:
- `build:`
- `test:`
- `feat(protocol):`
- `feat(transport):`
- `feat(video):`
- `feat(audio):`
- `feat(input):`
- `feat(ui):`
- `perf:`
- `fix:`
- `docs:`

Do not commit generated build directories, NVIDIA SDK binaries, Qt SDK files, controller driver installers, captured raw video, or benchmark dumps larger than the small curated fixtures under `tests/fixtures/`.
