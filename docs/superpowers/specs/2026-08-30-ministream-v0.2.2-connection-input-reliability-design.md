# MiniStream v0.2.2 Connection & Input Reliability Design

## Goal

v0.2.2 closes the connection-lifecycle and cross-platform input gaps that can leave two peers in different states, permanently lock a host, or interpret the same key differently on Windows and macOS. It preserves the existing UDP, pairing, crypto, media, FEC, ABR, and single-controller architecture.

The release target is a reliable 1080p60 H.264 SDR baseline with optional HEVC only when the active encoder proves support. It does not claim raw relative mouse capture, HDR, or hybrid-GPU support.

## Verified findings

Each finding below was verified against the tracked source before it was included in the design.

| Finding | Decision |
| --- | --- |
| Key packets carry sender-native Windows VK or macOS CGKeyCode values | Replace with USB HID keyboard usage IDs on the wire. |
| Key/button edges are one-shot UDP and peer cleanup only clears gamepad state | Add ordered reliable edges, acknowledgements, ReleaseAll, and backend-held-state cleanup. |
| A valid Hello locks the host endpoint without an expiry | Add a two-second handshake peer lease. |
| Pairing confirmation retries expire after about one second, which is shorter than normal human confirmation time | Let the 60-second pairing lease govern human confirmation; bounded retries are not a disconnect condition. |
| Losing the final confirmation can leave one side Streaming and the other Pairing | Retain a one-second post-pair confirmation grace and answer confirmations during it. |
| Streaming has no authenticated liveness timeout | Use existing feedback as controller-to-host activity, add a small host heartbeat, and expire either peer after three seconds of authenticated silence. |
| Pairing/Streaming hosts keep answering discovery | Answer discovery only while Broadcasting and unlocked. |
| A receive batch can contain another sender that was prefetched before the first Hello locked the peer | Recheck every prefetched datagram against the current locked peer before processing it. |
| Windows advertises HEVC from NVENC runtime presence alone | Preflight H.264/HEVC on the active capture device and advertise only proven codecs. |
| Audio playout advances only one 10 ms frame after a delayed UI tick | Add a bounded clock-driven catch-up loop. |
| Controlled Streaming opens the Remote video/control page | Give the controlled role a separate active-session page. |

## Compatibility boundary

Keyboard wire semantics change in this release. Silent interoperation with older builds is unsafe, so v0.2.2 increments every pre-session compatibility boundary:

- common encrypted packet protocol: version 2;
- Hello/Accept handshake: explicit protocol version 2 plus message kind;
- pairing offer/confirmation magic and SAS transcript: version 2;
- LAN discovery protocol: version 3.

An older v0.1.x or v0.2.1 peer therefore does not appear compatible and must be upgraded. The v0.1.0 Mac build must not be used for v0.2.2 acceptance.

## Platform-neutral keyboard path

`DesktopKey` uses USB HID Keyboard/Keypad usage IDs. The path is:

```text
Qt key -> DesktopKey (HID) -> encrypted UDP -> native sink
                                      Windows -> scan code SendInput
                                      macOS   -> CGKeyCode CGEvent
```

The shared Qt mapper covers letters, digits, Space, punctuation used by normal keyboards, Escape, Tab, Backspace, Enter, navigation keys, F1-F12, Caps Lock, and the generic left-side modifier usages. Unsupported keys are rejected instead of sending an ambiguous native number.

Key release uses a protocol-owned flag. Windows sink flags and macOS flags never cross the network.

## Reliable input ownership

Mouse movement, wheel movement, and full gamepad state remain lossy. Key, mouse-button, and ReleaseAll events use a small sequence envelope:

```text
Remote                                  Controlled
edge(seq, input) ----------------------> buffer by sequence
retry with fresh AEAD packet ----------> inject once, in order
                         <-------------- authenticated ack(seq)
```

The sender reuses the existing 20/40/80 ms retry scheduler. The receiver buffers at most 64 out-of-order edges, applies only contiguous sequences, acknowledges only applied events, and acknowledges duplicates without reinjecting them. A retry exhaustion or full pending window ends the session so the host's liveness cleanup releases all held input instead of continuing with unknown state.

Native sinks track successfully pressed keys and buttons. `clear_input()` sends best-effort releases and runs on ReleaseAll, clean disconnect, mode switch, process stop, pairing cancellation, and liveness expiry. Gamepad clearing remains separate.

## Session convergence

Default timings are centralized in `SessionTiming` and can be shortened in integration tests:

- abandoned Hello peer lease: 2 s;
- human pairing lease: 60 s;
- post-pair confirmation grace: 1 s, repeated every 100 ms;
- authenticated heartbeat: 500 ms;
- authenticated silence timeout: 3 s.

Only successfully authenticated streaming packets refresh liveness. Plaintext packets cannot keep a streaming session alive.

The host is discoverable only in `Broadcasting` with no locked peer. A timed-out or disconnected peer returns the host to this exact state.

## Targeted platform and UI fixes

Windows codec preflight uses the same D3D11 device selected by desktop capture. A failed profile is rejected before Accept; discovery capabilities are refreshed from the successful preflight. Hybrid-GPU capture/encode selection is not rewritten in this release.

Audio catch-up is bounded to four 10 ms playout frames per runtime tick and resynchronizes after a larger stall. It is driven by the playout clock, not packet arrival.

The QML shell shows a receiver/video page only for the Remote role. Controlled Streaming shows a compact active-session page with Disconnect. A reserved local shortcut exits remote input, and losing application focus sends ReleaseAll before returning input locally.

## Deliberate non-goals

- raw relative mouse capture and cursor lock;
- 4K SDR/HDR profile redesign;
- cross-adapter capture/encode on hybrid-GPU laptops;
- persistent device identity or a pairing security redesign;
- multi-controller or busy-response negotiation;
- replacing UDP with TCP/QUIC.

These items need platform-specific design or real hardware evidence and are not approximated with unverified behavior.

## Release evidence

Release requires fresh unit/integration tests, protocol fault tests, a Release Windows build, installer install/uninstall smoke checks, application launch and role-page Computer Use checks, SHA-256 publication, and a tagged GitHub release. macOS compilation, permissions, Windows/macOS key contracts, four-direction device tests, raw-mouse testing, and DMG upload remain explicit Mac/hardware handoff gates.
