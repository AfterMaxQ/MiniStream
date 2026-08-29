# MiniStream 06 Integration and Playable Alpha Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate video, audio, input, adaptation and UI into one playable alpha, then prove it works for a real 60-minute gaming session on the target double-Wi‑Fi topology.

**Architecture:** Integration is staged. Each new subsystem is enabled only after the prior configuration has a baseline, so regressions can be assigned to a specific layer instead of debugged as an undifferentiated full system.

**Tech Stack:** Entire MiniStream stack.

**Spec:** `docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md`

---

### Task 1: Unified session lifecycle

**Files:**
- Modify: `src/app/session_controller.*`
- Create: `src/core/session/session_runtime.hpp/.cpp`
- Create: `tests/session/session_runtime_test.cpp`

**State order:**
```text
Idle
 -> Connecting
 -> Negotiating
 -> Streaming
 -> Recovering (optional transient)
 -> Disconnecting
 -> Idle
```

Failure may occur from any active startup/streaming state -> Failed -> Idle.

- [ ] Test legal and failure transitions.
- [ ] Start/stop transport, video, audio and input in deterministic order.
- [ ] On failure, tear down in reverse order and release hardware resources.
- [ ] Commit.

---

### Task 2: Telemetry snapshot aggregation

**Files:**
- Create: `src/core/telemetry/stream_snapshot.hpp`
- Create: `src/core/telemetry/stream_aggregator.hpp/.cpp`
- Create: `tests/telemetry/stream_aggregator_test.cpp`

Snapshot contains:
- capture fps
- encode avg/p95
- bitrate
- FEC ratio
- send queue delay
- RTT/p95/jitter/loss
- FEC recovered/unrecoverable
- decode avg/p95
- render fps
- audio buffer/underruns/drift
- controller connected
- input p50/p95 transport-dispatch latency

- [ ] Write a failing synthetic aggregation test containing known p95/queue/audio/input values.
- [ ] Run `ctest --test-dir build -R stream_aggregator --output-on-failure` and verify failure.
- [ ] Implement immutable snapshot aggregation and publish at 10 Hz maximum to the app layer.
- [ ] Run the focused test plus all telemetry tests.
- [ ] Commit:
  ```bash
  git add src/core/telemetry tests/telemetry
  git commit -m "feat(telemetry): aggregate immutable stream snapshots"
  ```

---

### Task 3: Profile ladder

**Files:**
- Create: `src/core/config/stream_profile.hpp/.cpp`
- Create: `tests/config/stream_profile_test.cpp`

Profiles:
```text
Debug1080:
  1920x1080 60 H264 SDR 20 Mbps

Balanced1440:
  2560x1440 60 HEVC SDR 35 Mbps

Quality4K:
  3840x2160 60 HEVC Main10 HDR 50 Mbps start
  min 20 Mbps
  max 80 Mbps
```

- [ ] Test profile values.
- [ ] Wire profile into negotiation and encoder.
- [ ] Keep automatic resolution switching disabled.
- [ ] Commit.

---

### Task 4: Fault-injection end-to-end matrix

**Files:**
- Create: `tests/integration/fault_matrix.md`
- Create: `tools/run_fault_matrix.py`

Run each for at least 5 minutes:
1. 0% injected loss
2. 0.1% random loss
3. 0.5% random loss
4. 1% random loss
5. 3-packet burst every 500 packets
6. 5-packet burst every 1000 packets
7. 5 ms injected jitter
8. 10 ms injected jitter

Record:
- bitrate trajectory
- FEC trajectory
- send queue p95
- frame drops
- IDR requests
- audio underruns
- input p95

Pass condition:
- queue does not trend upward without bound
- ABR reacts at defined threshold
- unrecoverable frame triggers recovery
- input remains responsive during video degradation

- [ ] Implement runner.
- [ ] Execute and store compact result tables.
- [ ] Fix only observed correctness/stability failures; do not tune by hiding problems with buffers.
- [ ] Commit.

---

### Task 5: 4K60 HDR real-game validation

**Files:**
- Create: `benchmarks/4k60-hdr-session.md`

Run:
- actual target Windows gaming PC
- actual Wi‑Fi 6 router
- actual Mac connected to TV
- TV game mode enabled

Record first 10 minutes and whole-session summaries:
- stream profile
- encode/decode p95
- RTT/jitter/loss
- bitrate/FEC
- send queue
- render fps
- audio buffer
- input p95
- frame drops / IDR recoveries

- [ ] Run 15-minute trial.
- [ ] For every blocker, add/reproduce a failing focused test or probe first, fix only that observed failure, and rerun the 15-minute trial until it completes.
- [ ] Run 60-minute acceptance session.
- [ ] Verify memory and queue metrics do not trend upward.
- [ ] Commit benchmark note.

---

### Task 6: GUI performance A/B

**Files:**
- Create: `benchmarks/gui-overhead.md`

Compare same scene for 5 minutes each:
A. UI minimized, Performance page closed  
B. Performance page visible

Record:
- host CPU
- client CPU
- encode p95
- decode p95
- send queue
- render FPS

Acceptance:
- median host or client CPU increase from opening Performance page <=5 percentage points
- median encode p95 increase <=1.0 ms
- median decode p95 increase <=1.0 ms
- median send-queue p95 increase <=1.0 ms
- render FPS must remain >=59.0 for the same 60 fps source
- any failed threshold blocks release until a focused profiler trace identifies and fixes the UI cause

- [ ] Run A/B three times.
- [ ] Record median difference.
- [ ] If any numeric gate fails, capture a profiler trace for that metric, add a regression measurement, apply the smallest fix, then rerun all three A/B pairs.
- [ ] Commit.

---

### Task 7: Playable Alpha release checklist

**Files:**
- Create: `docs/release/v0.1-alpha-checklist.md`

Checklist:
- [ ] Windows host starts from GUI
- [ ] Mac client connects from GUI
- [ ] 4K60 HEVC Main10 HDR path verified
- [ ] 1080p60 fallback profile works manually
- [ ] stereo audio stable
- [ ] controller works
- [ ] rumble works
- [ ] FEC works under injection
- [ ] IDR recovery works
- [ ] ABR responds without queue growth
- [ ] reconnect after one dropped session works
- [ ] first pairing shows matching six-digit SAS on both devices and requires explicit confirmation
- [ ] tampered/replayed INPUT and CONTROL packets are rejected
- [ ] 60-minute real-game session passes
- [ ] UI style checklist passes
- [ ] no known unbounded queue
- [ ] no software video fallback
- [ ] logs contain no credentials/secrets
- [ ] README documents runtime prerequisites

- [ ] Execute checklist from a clean build/install.
- [ ] Tag only after every required item passes.
- [ ] Commit:
  ```bash
  git add docs/release
  git commit -m "docs: add v0.1 playable alpha release gate"
  ```

---

## Final alpha acceptance

MiniStream v0.1 is complete only when the target Windows->Wi‑Fi 6->Mac->TV setup can play a real game for at least 60 minutes with:
- 4K60 HEVC Main10 HDR
- hardware encode/decode
- stereo audio
- one controller + rumble
- bounded queue behavior
- FEC and IDR recovery
- queue-aware adaptive bitrate
- modern GUI
- no crash, deadlock, sustained memory growth or sustained latency growth
