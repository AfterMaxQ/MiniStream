# MiniStream 01 Transport, FEC and Adaptive Bitrate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and validate the low-latency UDP session layer, packet scheduler, frame fragmentation/reassembly, FEC, clock sync, fault injection and inspectable queue-aware bitrate controller before real video/audio are attached.

**Architecture:** One UDP socket carries channel-tagged packets. Small reliable control messages use explicit ACK/retry; media stays deadline-driven. A netprobe executable uses the same transport primitives and provides the first real Windows<->Mac Wi‑Fi benchmark.

**Tech Stack:** C++20, standalone Asio 1.38.2, Catch2, libsodium 1.0.20, Leopard-RS (BSD-3-Clause) behind `FecCodec`, platform socket APIs via Asio.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

## Global Constraints

- Max datagram 1200 bytes.
- INPUT > CONTROL > AUDIO > VIDEO/FEC > TELEMETRY.
- Video never waits indefinitely for missing packets.
- Send and reassembly queues are bounded.
- ABR decisions and thresholds are observable in telemetry.

---

### Task 1: Channel packet headers and frame fragmentation

**Files:**
- Create: `src/core/transport/media_header.hpp`
- Create: `src/core/transport/packetizer.hpp`
- Create: `src/core/transport/packetizer.cpp`
- Create: `tests/transport/packetizer_test.cpp`

**Interfaces:**
- Consumes: `CommonHeader`, `kMaxDatagramBytes`
- Produces:
  ```cpp
  struct MediaHeader {
    std::uint32_t packet_seq;
    std::uint32_t frame_id;
    std::uint16_t shard_index;
    std::uint16_t shard_count;
    std::uint64_t capture_timestamp_us;
  };

  struct EncodedFrame {
    std::uint32_t frame_id;
    std::uint64_t capture_timestamp_us;
    bool keyframe;
    std::vector<std::byte> bytes;
  };

  std::vector<Datagram> packetize_video(const EncodedFrame&, SessionId);
  ```

- [ ] **Step 1:** Test a 100 KiB frame round-trips into datagrams all <=1200 bytes.
- [ ] **Step 2:** Verify failure before implementation.
- [ ] **Step 3:** Implement deterministic slicing with a fixed header size.
- [ ] **Step 4:** Test zero-length rejection and maximum shard-count rejection.
- [ ] **Step 5:** Commit:
  ```bash
  git add src/core/transport tests/transport
git commit -m "feat(transport): add bounded video fragmentation"
  ```

---

### Task 2: Deadline-based frame reassembler

**Files:**
- Create: `src/core/transport/reassembler.hpp`
- Create: `src/core/transport/reassembler.cpp`
- Create: `tests/transport/reassembler_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct ReassemblyConfig {
    Microseconds deadline{5000};
    std::size_t max_incomplete_frames{2};
  };

  class FrameReassembler {
   public:
    std::optional<EncodedFrame> push(const Datagram&, SteadyClock::time_point now);
    std::vector<std::uint32_t> expire(SteadyClock::time_point now);
  };
  ```

- [ ] **Step 1:** Test in-order, shuffled, duplicate and missing shards.
- [ ] **Step 2:** Test that a third incomplete frame drops the oldest incomplete frame.
- [ ] **Step 3:** Implement with explicit per-frame deadline.
- [ ] **Step 4:** Run with randomized packet order for 10,000 generated frames.
- [ ] **Step 5:** Commit:
  ```bash
  git add src/core/transport/reassembler.hpp src/core/transport/reassembler.cpp tests/transport
  git commit -m "feat(transport): add deadline-based frame reassembly"
  ```

---

### Task 3: UDP endpoint and session handshake

**Files:**
- Create: `src/core/net/udp_endpoint.hpp`
- Create: `src/core/net/udp_endpoint.cpp`
- Create: `src/core/session/handshake.hpp`
- Create: `src/core/session/handshake.cpp`
- Create: `tests/session/handshake_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct Hello {
    VideoCodec codec;
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t fps;
    std::uint32_t target_bitrate_bps;
    std::uint64_t nonce;
  };

  struct Accept {
    SessionId session_id;
    VideoCodec codec;
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t fps;
    std::uint32_t bitrate_bps;
  };
  ```
  HELLO retry interval: 250 ms.

- [ ] **Step 1:** Unit-test encode/decode and nonce/session matching.
- [ ] **Step 2:** Add local loopback integration test with one intentionally dropped HELLO.
- [ ] **Step 3:** Implement async receive/send without blocking media worker threads.
- [ ] **Step 4:** Run loopback test 100 times.
- [ ] **Step 5:** Commit.

---

### Task 4: Reliable CONTROL messages

**Files:**
- Create: `src/core/transport/reliable_control.hpp`
- Create: `src/core/transport/reliable_control.cpp`
- Create: `tests/transport/reliable_control_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class ControlKind : std::uint8_t {
    Start, Stop, RequestIdr, CodecConfig, BitrateUpdate, Ack
  };

  struct ControlMessage {
    ControlSeq sequence;
    ControlKind kind;
    std::vector<std::byte> payload;
  };

  class ReliableControl {
   public:
    ControlSeq send(ControlMessage msg, SteadyClock::time_point now);
    void acknowledge(ControlSeq);
    std::vector<ControlMessage> due_retries(SteadyClock::time_point now);
  };
  ```
  Retry schedule: 20 ms, 40 ms, 80 ms; then fail the operation.

- [ ] **Step 1:** Test ACK removal.
- [ ] **Step 2:** Test exact retry schedule and terminal failure.
- [ ] **Step 3:** Implement bounded pending map with maximum 64 in-flight control messages.
- [ ] **Step 4:** Run tests.
- [ ] **Step 5:** Commit.

---

### Task 5: Packet scheduler and pacing

**Files:**
- Create: `src/core/transport/packet_scheduler.hpp`
- Create: `src/core/transport/packet_scheduler.cpp`
- Create: `tests/transport/packet_scheduler_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class Priority { Input, Control, Audio, Video, Telemetry };

  class PacketScheduler {
   public:
    bool enqueue(Priority, Datagram, SteadyClock::time_point deadline);
    std::optional<Datagram> next(SteadyClock::time_point now);
    Microseconds estimated_video_queue_delay() const;
    void set_video_rate(std::uint64_t bits_per_second);
  };
  ```

- [ ] **Step 1:** Test that INPUT overtakes queued VIDEO.
- [ ] **Step 2:** Test expired telemetry/video packets are dropped.
- [ ] **Step 3:** Test token-bucket video pacing at 20 Mbps with +/-5% tolerance over a synthetic second.
- [ ] **Step 4:** Implement using five bounded queues; do not use one giant FIFO.
- [ ] **Step 5:** Commit.

---


### Task 6: Pairing, key derivation, AEAD and replay protection

**Files:**
- Create: `src/core/security/identity.hpp`
- Create: `src/core/security/pairing.hpp`
- Create: `src/core/security/pairing.cpp`
- Create: `src/core/security/session_crypto.hpp`
- Create: `src/core/security/session_crypto.cpp`
- Create: `src/core/security/replay_window.hpp`
- Create: `tests/security/pairing_test.cpp`
- Create: `tests/security/session_crypto_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct DeviceIdentity {
    std::array<std::byte, 32> public_key;
    std::array<std::byte, 64> secret_key;
  };

  struct SessionKeys {
    std::array<std::byte, 32> tx;
    std::array<std::byte, 32> rx;
  };

  class SessionCrypto {
   public:
    Result<Datagram, CryptoError> seal(PacketType type, std::span<const std::byte> plaintext);
    Result<std::vector<std::byte>, CryptoError> open(const Datagram&);
  };
  ```

**Protocol rules:**
- libsodium 1.0.20.
- First pairing exchanges ephemeral X25519 keys and both long-term Ed25519 public identities.
- Compute a six-digit SAS from a BLAKE2b hash of the ordered transcript: protocol version, both nonces, both identity public keys and both ephemeral public keys.
- Display the same SAS on Host and Client; session activation requires explicit user confirmation that the codes match.
- The SAS is not secret and is never used as a key.
- Successful pairing stores the peer Ed25519 public identity.
- Each new session uses fresh ephemeral X25519 keys; each side signs the ephemeral key + both nonces with its stored Ed25519 identity and verifies the peer signature before deriving directional keys.
- AEAD: ChaCha20-Poly1305 IETF.
- 64-bit logical packet counter is incorporated into the 96-bit AEAD nonce with a fixed per-session prefix.
- Replay window accepts a maximum reordering distance of 1024 authenticated packet counters.
- A repeated outbound nonce/counter is a fatal programming/session error.

- [ ] **Step 1: Write pairing transcript tests**

  Test: identical transcript yields identical SAS, any modified identity/ephemeral key/nonce changes the SAS, a session cannot enter Streaming before both local confirmation and peer confirmation, and future-session identity-signature verification rejects a substituted key.

- [ ] **Step 2: Write AEAD/replay tests**

  Test: seal/open round-trip, modified ciphertext fails, modified authenticated header fails, duplicate packet is rejected, packet reordered within 1024 succeeds exactly once.

- [ ] **Step 3: Implement identity generation, SAS transcript hashing and explicit confirmation state**

  Use libsodium primitives only; no home-grown cipher/hash implementation.

- [ ] **Step 4: Implement directional session keys and `SessionCrypto`**

  Transport code receives sealed datagrams; callers never manage AEAD nonces.

- [ ] **Step 5: Run security tests under randomized reorder/tamper input**

  ```bash
  ctest --test-dir build -R security --output-on-failure
  ```

- [ ] **Step 6: Commit**
  ```bash
  git add src/core/security tests/security
  git commit -m "feat(security): authenticate and encrypt LAN sessions"
  ```

---

### Task 7: Clock sync and RTT/jitter metrics

**Files:**
- Create: `src/core/time/clock_sync.hpp`
- Create: `src/core/time/clock_sync.cpp`
- Create: `tests/time/clock_sync_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct ClockSample {
    std::int64_t rtt_us;
    std::int64_t offset_us;
  };

  ClockSample compute_clock_sample(
      std::int64_t t0, std::int64_t t1,
      std::int64_t t2, std::int64_t t3);
  ```

- [ ] **Step 1:** Test known synthetic offsets of +5 ms and -7 ms.
- [ ] **Step 2:** Test preferred sample is lowest RTT among recent 20 samples.
- [ ] **Step 3:** Implement NTP-style formula from spec.
- [ ] **Step 4:** Run tests.
- [ ] **Step 5:** Commit.

---

### Task 8: Reed-Solomon erasure wrapper

**Files:**
- Create: `third_party/leopard/` vendored from `catid/leopard` commit `6e5725ebdf9da4370b0bcc4f70fa8eb66f4e6198`
- Create: `src/core/fec/fec_codec.hpp`
- Create: `src/core/fec/fec_codec.cpp`
- Create: `tests/fec/fec_codec_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct FecLayout {
    std::uint16_t data_shards;
    std::uint16_t parity_shards;
    std::size_t shard_bytes;
  };

  struct FecShard {
    std::uint16_t index;
    std::uint16_t original_payload_bytes;
    bool present;
    std::vector<std::byte> bytes;
  };

  struct FecBlock {
    FecLayout layout;
    std::vector<FecShard> shards;
  };

  class FecCodec {
   public:
    FecBlock encode(std::span<const Datagram> data, std::uint16_t parity_shards);
    bool recover(FecBlock&);
  };
  ```

- [ ] **Step 1:** Test 20 data + 2 parity recovers any 2 erased shards.
- [ ] **Step 2:** Test 20+2 fails cleanly with 3 erased shards.
- [ ] **Step 3:** Vendor Leopard-RS commit `6e5725ebdf9da4370b0bcc4f70fa8eb66f4e6198`, integrate it behind the wrapper, pad equal-sized shards to 64-byte alignment, and retain original payload lengths.
- [ ] **Step 4:** Benchmark encode/recover for 20x1168-byte blocks and record p50/p95 in test output; assert only correctness, not a machine-specific timing.
- [ ] **Step 5:** Commit.

---

### Task 9: Fault injector

**Files:**
- Create: `src/core/net/fault_injector.hpp`
- Create: `src/core/net/fault_injector.cpp`
- Create: `tests/net/fault_injector_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct FaultConfig {
    double random_loss;
    Microseconds jitter;
    std::uint32_t burst_every_packets;
    std::uint32_t burst_length;
    std::uint64_t seed;
  };
  ```

- [ ] **Step 1:** Test deterministic output for seed 42.
- [ ] **Step 2:** Test a configured 5-packet burst is reproducible.
- [ ] **Step 3:** Implement the injector outside production send semantics so it can be disabled with zero behavioral change.
- [ ] **Step 4:** Run tests.
- [ ] **Step 5:** Commit.

---

### Task 10: Queue-aware bitrate + FEC controller

**Files:**
- Create: `src/core/adaptation/rate_controller.hpp`
- Create: `src/core/adaptation/rate_controller.cpp`
- Create: `tests/adaptation/rate_controller_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct NetworkFeedback {
    Microseconds send_queue;
    Microseconds rtt;
    Microseconds jitter;
    double loss_fraction;
    std::uint32_t unrecoverable_frames;
  };

  struct AdaptationDecision {
    std::uint64_t bitrate_bps;
    double fec_ratio;
    enum class Reason { None, QueueGrowth, Loss, UnrecoverableFrame, StableRecovery } reason;
  };
  ```

Initial exact policy:
- overload when queue >4 ms OR loss >1% OR unrecoverable_frames>0
- overload reaction: bitrate *=0.85
- stable window: 2 s with queue <2 ms, loss <0.2%, jitter <3 ms, no unrecoverable frames
- recovery: +1 Mbps/s
- FEC: 3%, 5%, 10%, 15% at spec thresholds

- [ ] **Step 1:** Encode every threshold as a failing table-driven unit test.
- [ ] **Step 2:** Verify bitrate cannot exceed user max or fall below user min.
- [ ] **Step 3:** Implement pure deterministic state machine; no socket calls inside controller.
- [ ] **Step 4:** Run a synthetic trace: stable -> congestion -> recovery and snapshot expected decisions.
- [ ] **Step 5:** Commit.

---

### Task 11: `ministream-netprobe`

**Files:**
- Create: `tools/netprobe/CMakeLists.txt`
- Create: `tools/netprobe/main.cpp`
- Create: `tools/netprobe/report.cpp`
- Modify: root `CMakeLists.txt`

**Interfaces:**
- Uses production UDP endpoint, scheduler, clock sync and telemetry.
- CLI modes:
  ```text
  ministream-netprobe --listen 47990
  ministream-netprobe --connect <host> --rate-mbps 20 --duration 30
  ```

- [ ] **Step 1:** Add CLI parse test for valid/invalid rates.
- [ ] **Step 2:** Implement server echo/feedback.
- [ ] **Step 3:** Implement client paced payload generator.
- [ ] **Step 4:** Run 20/40/60/80/100 Mbps tests on the actual Windows<->Mac Wi‑Fi path and save a small Markdown result under `benchmarks/network-baseline.md`.
- [ ] **Step 5:** Commit.

---

## Transport acceptance gate

Pass when:
- loopback tests survive reorder, duplicate, random loss and burst loss
- 20+2 FEC recovers any two erased shards in unit tests
- ABR simulation decreases immediately on overload and recovers slowly
- INPUT packets are never trapped behind queued VIDEO in scheduler tests
- real netprobe reports RTT, p95 RTT, jitter, loss and sustainable throughput across the target Wi‑Fi path
- unauthenticated/tampered/replayed INPUT or CONTROL datagrams are rejected before dispatch
