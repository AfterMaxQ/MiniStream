# MiniStream 03 Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stream Windows game audio to macOS/TV as 48 kHz stereo Opus with a small bounded jitter buffer and stable long-session clock behavior.

**Architecture:** Windows captures the default render endpoint via WASAPI loopback, packetizes 10 ms Opus frames, and schedules them above video priority. macOS decodes into a bounded audio ring consumed by CoreAudio. Clock sync from the transport plan is used to observe and correct drift without growing latency.

**Tech Stack:** WASAPI, Opus 1.5.2, CoreAudio/AudioUnit.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

---

### Task 1: Audio wire format and sequence tracking

**Files:**
- Create: `src/core/audio/audio_packet.hpp`
- Create: `src/core/audio/audio_packet.cpp`
- Create: `tests/audio/audio_packet_test.cpp`

**Interfaces:**
```cpp
struct AudioPacket {
  std::uint32_t sequence;
  std::uint64_t host_timestamp_us;
  std::uint16_t sample_count;
  std::vector<std::byte> opus;
};
```

- [ ] Test encode/decode, sequence wrap and malformed lengths.
- [ ] Implement bounded payload validation.
- [ ] Run tests.
- [ ] Commit `feat(audio): define audio packet format`.

---

### Task 2: Windows WASAPI loopback capture

**Files:**
- Create: `src/windows/audio/wasapi_loopback.hpp`
- Create: `src/windows/audio/wasapi_loopback.cpp`
- Create: `tools/audio_probe/main.cpp`

**Interfaces:**
```cpp
struct PcmBlock {
  std::uint64_t host_timestamp_us;
  std::uint32_t frames;
  std::vector<float> interleaved_stereo;
};

class WasapiLoopback {
 public:
  Result<PcmBlock, AudioError> read();
};
```

- [ ] Add endpoint-enumeration smoke test.
- [ ] Capture default output endpoint in shared mode.
- [ ] Initialize WASAPI shared-mode loopback requesting 48 kHz stereo float with `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`; if the endpoint rejects the requested format, fail the probe explicitly rather than inventing a second resampler in v0.1.
- [ ] Run `audio_probe` for 60 s and verify no discontinuity counter growth during normal playback.
- [ ] Commit.

---

### Task 3: Opus encoder/decoder wrappers

**Files:**
- Create: `src/core/audio/opus_codec.hpp`
- Create: `src/core/audio/opus_codec.cpp`
- Create: `tests/audio/opus_codec_test.cpp`

**Interfaces:**
```cpp
class OpusEncoder48kStereo;
class OpusDecoder48kStereo;
```

Config:
- 48 kHz
- stereo
- 10 ms frames = 480 samples/channel
- `OPUS_APPLICATION_RESTRICTED_LOWDELAY`

- [ ] Generate 1 kHz stereo sine fixture.
- [ ] Encode/decode 10 s of fixture.
- [ ] Assert correct sample count and finite output values; do not assert lossy samples equal originals.
- [ ] Test packet-loss decode path using Opus PLC.
- [ ] Commit.

---

### Task 4: Bounded client jitter buffer

**Files:**
- Create: `src/core/audio/jitter_buffer.hpp`
- Create: `src/core/audio/jitter_buffer.cpp`
- Create: `tests/audio/jitter_buffer_test.cpp`

**Interfaces:**
```cpp
struct AudioJitterConfig {
  Microseconds target{10000};
  Microseconds max{20000};
};

enum class AudioPlayoutKind { Packet, Plc };

struct AudioPlayoutResult {
  AudioPlayoutKind kind;
  std::optional<AudioPacket> packet;
};

class AudioJitterBuffer {
 public:
  void push(AudioPacket);
  AudioPlayoutResult pop(std::uint32_t expected_sequence);
  Microseconds buffered_duration() const;
};
```

- [ ] Test reorder within target window.
- [ ] Test missing packet returns PLC request instead of waiting past max.
- [ ] Test buffer cannot grow beyond 20 ms under an artificial producer burst.
- [ ] Implement.
- [ ] Commit.

---

### Task 5: CoreAudio output

**Files:**
- Create: `src/macos/audio/coreaudio_output.hpp`
- Create: `src/macos/audio/coreaudio_output.mm`

**Interfaces:**
- Audio callback consumes decoded stereo samples from a lock-bounded ring.
- Callback never performs network I/O or Opus allocation.

- [ ] Play generated 1 kHz sine through CoreAudio.
- [ ] Attach decoded Opus stream.
- [ ] Add underrun counter.
- [ ] Run 30 min and verify callback stays active and queue duration remains bounded.
- [ ] Commit.

---

### Task 6: Audio drift observer and correction

**Files:**
- Create: `src/core/audio/drift_controller.hpp`
- Create: `src/core/audio/drift_controller.cpp`
- Create: `tests/audio/drift_controller_test.cpp`

**Interfaces:**
```cpp
struct DriftDecision {
  double resample_ratio; // clamped tightly around 1.0
};
```

Policy:
- observe difference between expected host-media time and local playout time
- ignore <2 ms error
- correct gradually
- clamp ratio to 0.995..1.005
- never fix drift by growing jitter buffer past configured maximum

- [ ] Simulate +100 ppm and -100 ppm clock drift for 30 min.
- [ ] Verify error converges without exceeding ratio clamp.
- [ ] Apply drift correction with a fixed linear-interpolation stereo resampler owned by `drift_controller.cpp`; ratio is recalculated once per 100 ms feedback window and clamped to 0.995..1.005. Unit-test the interpolator separately before connecting it to CoreAudio.
- [ ] Run 30-minute real stream; record underruns, buffer p95 and estimated drift.
- [ ] Commit.

---

## Audio acceptance gate

- 48 kHz stereo Opus
- 10 ms packets
- normal jitter buffer target 10 ms, hard max 20 ms
- missing packet uses PLC, not long retransmission
- 30-minute session has no increasing A/V/audio buffer drift
- CoreAudio callback has no network or blocking work
