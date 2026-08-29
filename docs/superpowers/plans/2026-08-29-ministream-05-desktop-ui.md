# MiniStream 05 Modern Desktop UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a polished Qt/QML desktop frontend shared by Windows Host and macOS Client, with simple defaults, progressively disclosed technical detail and strict non-AI visual/copy rules.

**Architecture:** QML consumes stable C++ application models only. Streaming hot paths publish snapshots into application models at bounded UI refresh rates; QML never calls transport/media APIs directly. The client stream page owns a `VideoSurface` Qt Quick item supplied by the video plan; QML overlays are siblings above that item rather than separate native windows.

**Tech Stack:** Qt 6.11.2, Qt Quick, QML, Qt Quick Controls, C++20.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

---

### Task 1: Application model boundary

**Files:**
- Create: `src/app/session_controller.hpp/.cpp`
- Create: `src/app/telemetry_model.hpp/.cpp`
- Create: `src/app/settings_model.hpp/.cpp`
- Create: `src/app/device_model.hpp/.cpp`
- Create: `src/app/capability_model.hpp/.cpp`
- Create: `tests/app/application_models_test.cpp`

**Interfaces:**
Expose Qt properties for:
- session state
- peer name/address
- codec/resolution/fps/HDR
- network health
- bitrate/FEC
- RTT/jitter/loss
- encode/decode latency
- audio buffer/underruns
- controller connected
- capabilities

- [ ] Write tests using fake core snapshots.
- [ ] Verify a 1000 Hz telemetry producer is coalesced to <=10 UI model updates/s by default.
- [ ] Implement models without media-thread dependencies.
- [ ] Commit.

---

### Task 2: Theme tokens and controls

**Files:**
- Create: `ui/theme/Tokens.qml`
- Create: `ui/theme/Theme.qml`
- Create: `ui/components/AppButton.qml`
- Create: `ui/components/StatusBadge.qml`
- Create: `ui/components/SettingRow.qml`
- Create: `ui/components/MetricRow.qml`
- Create: `ui/components/SectionHeader.qml`

**Required tokens:**
- spacing: 4, 8, 12, 16, 24, 32
- radius: 6, 10, 14
- motion: 120, 180, 220 ms
- one accent role
- semantic success/warning/error roles
- system/light/dark theme modes

- [ ] Build a component gallery page used only in development.
- [ ] Verify controls render in light and dark mode.
- [ ] Verify there is no gradient, glow or nested-card dependency in base components.
- [ ] Commit.

---

### Task 3: Host Home and Stream pages

**Files:**
- Create: `ui/pages/HostHomePage.qml`
- Create: `ui/pages/HostStreamPage.qml`

Host Home must answer:
1. ready or not?
2. current stream profile?
3. what action starts hosting?

Stream page shows:
- peer
- elapsed session time
- 4K/60/HDR/codec
- network health
- latency
- bitrate
- packet loss
- Performance Details
- Stop

- [ ] Drive pages entirely from fake `SessionController`.
- [ ] Verify failed states have factual copy such as “Hardware HEVC decode unavailable” rather than generic error text.
- [ ] Connect real controller methods only after fake-state UI is stable.
- [ ] Commit.

---

### Task 4: Client Home, Connecting and in-stream overlay

**Files:**
- Create: `ui/pages/ClientHomePage.qml`
- Create: `ui/pages/ConnectingPage.qml`
- Create: `ui/overlays/StreamOverlay.qml`
- Modify: `ui/components/VideoSurface.qml`

Connecting stages:
- Network
- Session
- Video
- Audio
- Controller

- [ ] Test all stage combinations with fake state.
- [ ] Embed `VideoSurface` as the full-bleed stream content and verify the overlay remains a normal QML sibling above it.
- [ ] Overlay defaults hidden during streaming.
- [ ] Add keyboard/controller action to show overlay without stealing permanent screen space.
- [ ] Respect reduced-motion setting for transitions.
- [ ] Commit.

---

### Task 5: Settings and capability-driven UI

**Files:**
- Create: `ui/pages/SettingsPage.qml`
- Create: `ui/settings/StreamingSettings.qml`
- Create: `ui/settings/VideoSettings.qml`
- Create: `ui/settings/AudioSettings.qml`
- Create: `ui/settings/ControllerSettings.qml`
- Create: `ui/settings/NetworkSettings.qml`
- Create: `ui/settings/AdvancedSettings.qml`

Presets:
- Automatic
- Quality
- Balanced
- Low Latency
- Custom

- [ ] Test HEVC/HDR controls disable when capabilities say unsupported.
- [ ] Test QML contains no direct `if (Windows)`/`if (Mac)` capability logic.
- [ ] Mark reconnect-required settings explicitly.
- [ ] Commit.

---

### Task 6: Performance page and restrained charts

**Files:**
- Create: `ui/pages/PerformancePage.qml`
- Create: `ui/components/MiniChart.qml`

Charts allowed:
- latency
- bitrate
- packet loss

Default window: last 30 s.

- [ ] Feed deterministic telemetry fixture and snapshot expected point counts.
- [ ] Clamp chart refresh to <=30 Hz and normal metric labels to 4-10 Hz.
- [ ] Use thin lines, low-noise grid, no gradient fill, no 3D effects.
- [ ] Commit.

---

### Task 7: Design-language lint/checklist

**Files:**
- Create: `docs/ui-style-checklist.md`
- Create: `tools/check_ui_copy.py`

Copy checker rejects case-insensitive banned phrases:
- AI-powered
- magic
- supercharge
- unlock unparalleled
- next-generation experience

Manual checklist rejects:
- purple/blue/cyan brand gradient
- glow
- glassmorphism base surfaces
- emoji functional icons
- hero marketing layout
- card nesting
- arbitrary radius values outside token set

- [ ] Add failing unit fixtures for every banned phrase and one allowed factual error message.
- [ ] Run `python tools/check_ui_copy.py ui tests/ui_copy_fixtures` and verify the banned fixture causes a non-zero exit.
- [ ] Implement the checker with case-insensitive phrase matching and QML/text-file traversal.
- [ ] Run the checker on the real `ui/` tree and verify exit code 0.
- [ ] Add the command to CI and commit:
  ```bash
  git add docs/ui-style-checklist.md tools/check_ui_copy.py ui tests
  git commit -m "test(ui): enforce design-language and copy constraints"
  ```

---

## UI acceptance gate

- both Windows and Mac launch the same design system
- ordinary path to connect/start stream uses <=3 primary actions
- status is clear without opening telemetry
- expert metrics are available via progressive disclosure
- UI update work does not run on streaming hot-path threads
- opening Performance page does not materially change encode/decode/network latency in A/B benchmark
- style checklist passes
