# MiniStream 02 Video and HDR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Attach a zero/low-copy hardware video path to the transport, starting at 1080p60 H.264 SDR and ending at verified 4K60 HEVC Main10 HDR10.

**Architecture:** Windows uses DXGI Desktop Duplication -> D3D11 textures -> GPU color conversion -> NVENC. macOS uses reassembled access units -> VideoToolbox -> CVPixelBuffer -> CVMetalTexture. A standalone Metal probe validates decoding first; the product path then imports those frames into a Qt Quick scene-graph video surface so QML overlay/full-screen UI shares one window. Each milestone is benchmarked before increasing format complexity.

**Tech Stack:** DXGI, D3D11, NVIDIA Video Codec SDK, VideoToolbox, CoreMedia, CoreVideo, Metal, QuartzCore.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

---

### Task 1: Windows DXGI capture probe

**Files:**
- Create: `src/windows/video/dxgi_capture.hpp`
- Create: `src/windows/video/dxgi_capture.cpp`
- Create: `tools/capture_probe/main.cpp`
- Create: `tests/windows/dxgi_capture_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct CapturedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    std::uint64_t frame_id;
    SteadyClock::time_point captured_at;
    DXGI_FORMAT format;
    std::uint32_t width;
    std::uint32_t height;
  };

  class DxgiCapture {
   public:
    Result<CapturedFrame, CaptureError> acquire(Microseconds timeout);
  };
  ```

- [ ] **Step 1:** Add a Windows-only test that initializes D3D11 and enumerates at least one output.
- [ ] **Step 2:** Implement `DuplicateOutput1`, falling back to `DuplicateOutput`.
- [ ] **Step 3:** Handle timeout and access-lost as explicit error variants.
- [ ] **Step 4:** Run `capture_probe` for 60 s and report frame interval mean/p95 without saving every frame.
- [ ] **Step 5:** Commit.

---

### Task 2: NVENC H.264 low-latency encoder

**Files:**
- Create: `src/windows/video/nvenc_encoder.hpp`
- Create: `src/windows/video/nvenc_encoder.cpp`
- Create: `tests/windows/nvenc_encoder_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct EncoderConfig {
    VideoCodec codec;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t fps;
    std::uint64_t bitrate_bps;
    bool hdr10;
  };

  class NvencEncoder {
   public:
    Result<void, EncodeError> reconfigure_bitrate(std::uint64_t bps);
    Result<EncodedFrame, EncodeError> encode(const CapturedFrame&, bool force_idr);
  };
  ```

H.264 bootstrap settings:
- 1920x1080 @60
- CBR 20 Mbps
- B frames 0
- lookahead 0
- low-latency tuning
- async/in-flight depth 1 where supported

- [ ] **Step 1:** Write a capability test that fails clearly if NVENC unavailable.
- [ ] **Step 2:** Encode 300 captured frames to Annex-B and verify SPS/PPS + decodable IDR are present.
- [ ] **Step 3:** Add encoder latency telemetry around submit/output.
- [ ] **Step 4:** Run 10-minute local encode benchmark; record avg/p95 and encoded bitrate.
- [ ] **Step 5:** Commit.

---

### Task 3: End-to-end 1080p60 H.264 transport

**Files:**
- Create: `src/windows/video/video_sender.cpp`
- Create: `src/macos/video/video_receiver.mm`
- Modify: host/client application startup

**Interfaces:**
- Sender consumes `EncodedFrame`; transport returns `RequestIdr`.
- Receiver produces complete H.264 access units.

- [ ] **Step 1:** Add a fixture-based transport test using a small H.264 IDR fixture.
- [ ] **Step 2:** Wire encoded access units to packetizer/scheduler.
- [ ] **Step 3:** Wire receiver to reassembler/FEC and report missing frames.
- [ ] **Step 4:** Stream 1080p60 for 30 min; prove reassembly queues remain bounded.
- [ ] **Step 5:** Commit.

---

### Task 4: VideoToolbox hardware decoder

**Files:**
- Create: `src/macos/video/vt_decoder.hpp`
- Create: `src/macos/video/vt_decoder.mm`
- Create: `tests/macos/vt_decoder_test.mm`

**Interfaces:**
- Produces:
  ```cpp
  struct DecodedFrame {
    CVPixelBufferRef pixel_buffer;
    std::uint32_t frame_id;
    SteadyClock::time_point decoded_at;
  };

  class VtDecoder {
   public:
    Result<void, DecodeError> configure(const CodecConfig&);
    Result<void, DecodeError> submit(const EncodedFrame&);
    std::optional<DecodedFrame> take_latest();
  };
  ```

- [ ] **Step 1:** Build a test fixture from a known H.264 sample and verify decoder callback fires.
- [ ] **Step 2:** Configure VideoToolbox to require hardware decode and fail if unavailable.
- [ ] **Step 3:** Store only latest decoded frame for renderer handoff.
- [ ] **Step 4:** Record decode avg/p95 for 1080p60.
- [ ] **Step 5:** Commit.

---

### Task 5: Metal renderer without CPU RGB copies

**Files:**
- Create: `src/macos/video/metal_renderer.hpp`
- Create: `src/macos/video/metal_renderer.mm`
- Create: `src/macos/video/shaders/video.metal`

**Interfaces:**
- Consumes `CVPixelBufferRef`
- Uses `CVMetalTextureCacheCreateTextureFromImage`
- Diagnostic probe presents through `CAMetalLayer`; product presentation is moved to the Qt Quick surface task

- [ ] **Step 1:** Render a synthetic NV12 CVPixelBuffer to a window.
- [ ] **Step 2:** Add Rec.709 YUV->RGB shader and pixel sanity test on a tiny known pattern.
- [ ] **Step 3:** Integrate latest decoded frame.
- [ ] **Step 4:** Verify no CPU-created full-frame RGB buffer exists in the path using code review + Instruments allocation trace.
- [ ] **Step 5:** Commit.

---


### Task 6: Qt Quick video surface bridge

**Files:**
- Create: `src/app/video/video_surface_bridge.hpp`
- Create: `src/app/video/video_surface_bridge.cpp`
- Create: `src/macos/video/video_surface_item.hpp`
- Create: `src/macos/video/video_surface_item.mm`
- Create: `ui/components/VideoSurface.qml`
- Create: `tests/app/video_surface_bridge_test.cpp`

**Interfaces:**
- Decoder publishes only a retained latest `CVPixelBufferRef` + frame metadata into `VideoSurfaceBridge`.
- `VideoSurfaceItem` is the macOS Qt Quick scene-graph item that consumes the latest frame on the Qt render thread.
- QML places `StreamOverlay.qml` as a sibling above `VideoSurface`.

- [ ] **Step 1: Test latest-frame ownership semantics**

  Push frames 100, 101, 102 before a render take; assert the bridge exposes 102 and releases superseded retained buffers exactly once.

- [ ] **Step 2: Verify the pre-UI standalone Metal renderer still works**

  Keep the standalone renderer only as a diagnostic probe; mark it non-product.

- [ ] **Step 3: Implement Qt Quick scene-graph integration**

  Import NV12/P010 planes using `CVMetalTextureCache` inside the render lifecycle. Network and VideoToolbox callback threads may only publish the latest frame; they may not issue Qt scene-graph or Metal presentation calls.

- [ ] **Step 4: Embed the stream inside a QML window and place a test overlay above it**

  Verify window resize/full-screen does not create a second native streaming window.

- [ ] **Step 5: Run 10-minute 1080p60 render test and inspect retained-buffer count**

  Pass if retained video frames stay bounded at <=2 across decoder/render handoff.

- [ ] **Step 6: Commit**
  ```bash
  git add src/app/video src/macos/video/video_surface_item.* ui/components/VideoSurface.qml tests/app
  git commit -m "feat(video): integrate Metal frames with Qt Quick surface"
  ```

---

### Task 7: IDR recovery

**Files:**
- Modify: `src/core/transport/reassembler.*`
- Modify: `src/windows/video/video_sender.cpp`
- Modify: `src/windows/video/nvenc_encoder.*`
- Modify: `src/macos/video/video_receiver.mm`
- Create: `tests/video/idr_recovery_test.cpp`

**Interfaces:**
- Receiver emits `ControlKind::RequestIdr`.
- Encoder honors `force_idr=true` on next frame.

- [ ] **Step 1:** Inject unrecoverable loss into an inter frame and assert client emits one IDR request, not a request storm.
- [ ] **Step 2:** Implement 100 ms request cooldown.
- [ ] **Step 3:** Force next NVENC frame to IDR and include parameter sets needed for decoder resync.
- [ ] **Step 4:** Run 100 induced-loss recoveries; assert stream resumes without reconnect.
- [ ] **Step 5:** Commit.

---

### Task 8: HEVC Main10 / P010

**Files:**
- Modify: `src/windows/video/nvenc_encoder.*`
- Create: `src/windows/video/color_convert.hpp`
- Create: `src/windows/video/color_convert.cpp`
- Modify: `src/macos/video/vt_decoder.mm`
- Modify: `src/macos/video/metal_renderer.mm`

**Interfaces:**
- Adds `VideoCodec::Hevc`
- Adds 10-bit P010 input path

- [ ] **Step 1:** Add NVENC capability test for HEVC Main10.
- [ ] **Step 2:** Implement GPU-side source-to-P010 conversion; reject CPU full-frame conversion.
- [ ] **Step 3:** Encode/decode 10-bit test frames.
- [ ] **Step 4:** Benchmark 1440p60 then 4K60 SDR.
- [ ] **Step 5:** Commit.

---

### Task 9: HDR10 metadata and presentation

**Files:**
- Create: `src/core/video/hdr_metadata.hpp`
- Modify: Windows capture/encoder
- Modify: macOS decoder/renderer
- Create: `tests/video/hdr_metadata_test.cpp`

**Interfaces:**
- Represents primaries, white point, mastering luminance and content-light metadata.
- Negotiates HDR capability before stream starts.

- [ ] **Step 1:** Unit-test HDR metadata wire round-trip.
- [ ] **Step 2:** Detect HDR-capable Windows source/output and capture color-space state.
- [ ] **Step 3:** Configure HEVC stream with BT.2020 + PQ/ST.2084 metadata.
- [ ] **Step 4:** Configure macOS HDR presentation surface/colorspace and verify with an HDR test pattern.
- [ ] **Step 5:** Commit.

---

## Video acceptance gate

Pass sequentially:
1. 1080p60 H.264 SDR 30 min
2. 1440p60 HEVC SDR
3. 4K60 HEVC Main10 SDR
4. 4K60 HDR10 on target TV

For final target:
- hardware NVENC verified
- hardware VideoToolbox verified
- encode/decode p95 visible
- no unbounded decode/capture queue
- no full-frame CPU RGB copy
- induced loss triggers bounded IDR recovery
