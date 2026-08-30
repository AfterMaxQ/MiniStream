# MiniStream v0.2 Functional Reliability Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** 在不降低现有加密和输入控制能力的前提下，把 MiniStream 从“模块已存在”推进到一条可发现、可配对、可持续运行的局域网 1080p60 基础串流链路，并为后续 FEC、ABR 和高分辨率能力建立真实集成路径。

**Architecture:** 继续使用一个 UDP 会话端口和现有 C++/Asio/Qt 入口，不引入服务框架或云端中继。发现端改为可注入网卡提供器、定向广播加有限重试的非阻塞状态机；会话端锁定经握手确认的 peer，并在每次 UI tick 中批量清空非阻塞 socket，避免一个 10 ms 定时器把吞吐上限固定为 100 packet/s。媒体端按“先 1080p60，再 FEC/ABR”接通，平台后端只报告真实能力并真正执行协商出的尺寸、帧率、编解码器和码率。

**Tech Stack:** C++20, CMake, standalone Asio 1.38.2, Catch2 3.15.3, Qt 6.11.2/QML, Opus 1.5.2, libsodium 1.0.20, Leopard-RS, Windows DXGI/NVENC/Media Foundation/WASAPI/ViGEm, macOS CGDisplayStream/ScreenCaptureKit/VideoToolbox/CoreAudio/SDL3.

**Spec:** docs/superpowers/specs/2026-08-30-dynamic-device-discovery-design.md and docs/superpowers/specs/2026-08-29-ministream-v0.1-design.md. The user-provided MiniStream v0.1 issue summary is supplemental scope input; it is not treated as a runtime instruction or as evidence that a proposed fix already works.

## Global Constraints

- 用户要求跳过安全专属改造；本计划不持久化 DeviceIdentity、不把临时密钥签名认证接入现有 pairing、不改变 nonce/replay/AEAD 设计。
- 现有 ChaCha20-Poly1305、AAD、ReplayWindow、SAS 人工确认和对应安全测试必须保持通过；不得把 UDP 改成明文。
- Discovery port remains 47990 in production, and the existing versioned discovery wire format remains the compatibility boundary unless a test proves a new field is required.
- 1080p60 H.264 SDR is the first functional baseline. 1440p/4K/HDR may only be selected when both advertised and locally supported; unsupported requests must be rejected or downgraded before the encoder starts.
- Discovery, connect, retry, and error reporting must not synchronously block the Qt event loop.
- Do not add global input hooks, a cloud service, NAT traversal, permanent role assignments, or a new service framework.
- ViGEm remains optional metadata; keyboard and mouse control must not depend on the gamepad driver.
- Every behavioral change starts with a failing focused test and ends with a fresh focused test plus the relevant full-suite command.
- Hardware, permission, firewall, clean-machine, packet-loss, and 30–60 minute tests are acceptance gates. A passing unit test or build must never be reported as passing those gates.
- Generated build directories, packaged binaries, and ignored local assets are never committed.
- Each completed task gets a focused commit; no release tag is created until all mandatory platform gates are either passed with evidence or explicitly recorded as blocked.

## Current-code audit and scope decision

| Issue from the supplemental summary | Current-code finding | v0.2 treatment |
| --- | --- | --- |
| One limited broadcast and one query | Confirmed in core discovery | Fix with active IPv4 interface enumeration, directed broadcasts, fallback limited broadcast, bounded retries, and wire-compatible deduplication. |
| macOS Local Network declaration | No Info.plist or NSLocalNetworkUsageDescription in the tracked tree | Fix in the macOS bundle and verify the final app bundle. |
| Discovery blocks UI and errors collapse into an empty list | Confirmed: RoleController calls the synchronous discover_hosts path and exposes only bool/empty-list behavior | Fix with a non-blocking discovery state machine and actionable status mapping. |
| Windows firewall handling | CPack config installs ViGEm but no MiniStream inbound rule | Add a Private-profile, program-scoped UDP rule and uninstall cleanup; never disable the firewall or open the Public profile. |
| Discovery diagnostics | No shared discovery logger exists | Add bounded debug logging for interface, send, receive, and failure events without keys or SAS secrets. |
| One UDP packet per 10 ms tick | Confirmed in both runtimes | Fix with a bounded non-blocking receive batch and a regression that drains thousands of loopback datagrams. |
| Negotiated bitrate not applied to scheduler | Confirmed: ControlledRuntime constructs the scheduler at its default 20 Mbps | Fix by applying the negotiated rate at scheduler creation and on later ABR decisions. |
| last_sender_ changes the streaming destination | Confirmed in UdpEndpoint | Fix the functional peer-destination bug by locking the endpoint after valid Hello; cryptographic identity persistence remains out of scope. |
| FEC class not connected to media | Confirmed: PacketType::VideoFec exists but MediaSender/MediaReceiver never use it | Integrate a bounded per-frame FEC wire path and test recovery through MediaSender/MediaReceiver. |
| RateController not connected to runtime | Confirmed: only the standalone class and rumble Feedback path exist | Add encrypted feedback telemetry and connect bitrate/FEC decisions to encoder, scheduler, and sender. |
| macOS controlled audio is microphone input | Confirmed: CoreAudioCapture selects kAudioUnitSubType_DefaultInput and reports microphone input | Replace it with real system/application audio capture; do not continue advertising microphone input as game audio. |
| Advertisement and request disagree | Confirmed: the host advertises Debug1080 while remote unconditionally requests Quality4K | Add common-capability selection and make the selected configuration constrain the capture and encoder. |
| VideoToolbox CompleteFrames on every frame | Confirmed | Move to submit/callback/latest-frame flow; CompleteFrames is only used during controlled teardown. |
| CodecConfig sent on every frame | Confirmed | Send only on first usable config and when codec/profile/resolution changes. |
| Video decode ignores codec_configured_ | Confirmed | Drop boundedly or defer video until a valid CodecConfig is installed. |
| Runtime and FEC/ABR integration coverage | Confirmed as missing | Add loopback protocol/media integration and FEC/feedback tests; do not substitute them with hardware claims. |
| Platform tests, class naming, and DMG dependency closure | Platform checks are shallow; class name says ScreenCaptureKit while implementation is CGDisplayStream; macOS libsodium packaging is not self-contained | Fix the name/documentation mismatch without an unrelated capture rewrite, add platform probes, and verify/bundle non-Qt dependencies. |
| DeviceIdentity persistence, transcript signatures, CSPRNG migration | Security-only | Explicitly skipped; preserve current security checks and document the exclusion in the plan and final handoff, not in README. |

## File map

The following is the intended change surface. Existing files are listed as Modify; new files are listed as Create only where a focused responsibility does not fit the current file without making it harder to test.

- Discovery and networking: Modify src/core/session/discovery.hpp, src/core/session/discovery.cpp, src/core/net/udp_endpoint.hpp, src/core/net/udp_endpoint.cpp; add tests to tests/session/discovery_test.cpp and tests/net/udp_endpoint_test.cpp.
- Runtime state and handshake: Modify src/app/remote/remote_runtime.hpp/.cpp, src/app/controlled/controlled_runtime.hpp/.cpp, src/app/ui/role_controller.hpp/.cpp, src/core/session/role.hpp/.cpp, and src/core/session/handshake.hpp/.cpp.
- Negotiation and media: Modify src/core/config/stream_profile.hpp/.cpp, src/core/media/media_pipeline.hpp/.cpp, src/core/transport/packetizer.hpp/.cpp, src/core/transport/reassembler.hpp/.cpp, src/core/transport/packet_scheduler.hpp/.cpp, and the two runtime files above.
- FEC and feedback: Create src/core/fec/video_fec.hpp/.cpp and src/core/telemetry/feedback_wire.hpp/.cpp; modify src/core/fec/fec_codec.* and src/core/telemetry/stream_aggregator.* only for the counters and wire integration they own.
- Audio and platform media: Modify src/core/audio/jitter_buffer.*; create the macOS system-audio capture pair under src/macos/audio/; rename the misleading CGDisplayStream implementation to src/macos/video/cgdisplaystream_capture.*; modify src/macos/platform/controlled_backend.* and src/macos/CMakeLists.txt.
- Windows platform and packaging: Modify src/windows/platform/controlled_backend.*, src/windows/video/dxgi_capture.*, src/windows/video/nvenc_encoder.*, cmake/Dependencies.cmake, packaging/Packaging.cmake; create packaging/windows/ministream_firewall_install.nsh and packaging/windows/ministream_firewall_uninstall.nsh.
- macOS packaging and release docs: Create packaging/macos/Info.plist.in; modify src/macos/CMakeLists.txt, README.md, and create docs/release/v0.2-functional-reliability-checklist.md only after the implementation gates are known.
- Tests and build registration: Modify tests/CMakeLists.txt and add tests/integration/remote_control_integration_test.cpp, tests/transport/video_fec_pipeline_test.cpp, tests/telemetry/feedback_wire_test.cpp, tests/packaging/packaging_contract_test.py, and tests/packaging/version_contract_test.py, plus focused platform or pure-helper tests where the existing target already supports them.

---

### Task 1: Freeze the baseline and add deterministic test seams

**Files:**
- Inspect: CMakeLists.txt, tests/CMakeLists.txt, src/core/session/discovery.*, src/app/remote/remote_runtime.*, src/app/controlled/controlled_runtime.*
- Modify: tests/CMakeLists.txt
- Modify: tests/net/udp_endpoint_test.cpp
- Create: tests/support/loopback_test_helpers.hpp

**Interfaces:**
- Produces a reusable loopback helper that can send, receive, and pump non-blocking UDP packets without depending on a GUI or hardware encoder.
- Keeps production ports and runtime construction unchanged until a later task explicitly adds an injectable DiscoveryConfig.

- [ ] Step 1: Capture the clean baseline.

    Run:

    powershell -NoProfile -Command "git status --short --branch"
    powershell -NoProfile -Command "cmake --build build-clean --config Debug --parallel 4 --target ministream_tests"
    powershell -NoProfile -Command "& '.\build-clean\tests\Debug\ministream_tests.exe'"
    powershell -NoProfile -Command "python tests/ui/role_shell_copy_test.py"
    powershell -NoProfile -Command "python tests/ui/shortcut_policy_test.py"

    Expected: the worktree is clean, the existing C++ suite passes, and the two existing UI policy scripts pass. Record exact assertion/test counts in the execution notes only; do not place them in README.

- [ ] Step 2: Write the failing receive-loop seam test.

    Add a helper-level test named loopback helper preserves packet order that sends 32 numbered datagrams through two loopback endpoints and asserts all payloads are returned in receive order. The helper must use explicit endpoint objects and a bounded pump timeout; it must not sleep for a fixed one-second interval.

- [ ] Step 3: Run the new focused test before implementation.

    Run:

    cmake --build build-clean --config Debug --parallel 4 --target ministream_tests
    .\build-clean\tests\Debug\ministream_tests.exe "loopback helper preserves packet order"

    Expected: the test target does not yet contain the helper or the named test, so the command fails to compile or reports the missing test. This is the required Red state.

- [ ] Step 4: Add only the helper registration.

    Register the helper test source in tests/CMakeLists.txt and keep all runtime behavior unchanged. Do not add fake benchmark output or a platform-specific pass condition.

- [ ] Step 5: Run the helper test and commit the seam.

    Run the same focused command. Expected: PASS after the helper-only implementation.

    Commit:

    git add tests/CMakeLists.txt tests/support/loopback_test_helpers.hpp tests/net/udp_endpoint_test.cpp
    git commit -m "test: add deterministic loopback transport helpers"

### Task 2: Make discovery cover real IPv4 interfaces and retry without blocking

**Files:**
- Modify: src/core/session/discovery.hpp
- Modify: src/core/session/discovery.cpp
- Modify: tests/session/discovery_test.cpp
- Modify: src/core/CMakeLists.txt only if a platform source file is split out

**Interfaces:**
- Add DiscoveryError::NoUsableInterface and DiscoveryError::PermissionDenied while retaining Bind, Send, and Receive.
- Add DiscoveryInterface with name, four-byte IPv4 address, four-byte netmask, up, and loopback fields.
- Add directed_broadcast(address, netmask) as a pure helper returning no address for a /32 or invalid mask.
- Add DiscoveryConfig with port defaulting to kDiscoveryPort and retry interval defaulting to 150 ms.
- Add DiscoveryState { Idle, Searching, Complete, Failed } and DiscoveryPollResult { state, hosts, error }.
- Add DiscoveryClient::start(Microseconds timeout), DiscoveryClient::poll(SteadyClock::time_point now), DiscoveryClient::state(), DiscoveryClient::active(), and DiscoveryClient::stop(). discover_hosts(timeout) remains as a blocking compatibility wrapper for non-UI callers and delegates to this state machine.
- The production interface provider uses GetAdaptersAddresses on Windows and getifaddrs on macOS/Linux. It excludes down and loopback interfaces, does not hard-code Wi-Fi/VPN names, and deduplicates identical broadcast addresses.

- [ ] Step 1: Add failing pure broadcast tests.

    Add named cases:

    - directed broadcast for 192.168.1.20/255.255.255.0 is 192.168.1.255;
    - directed broadcast for 10.0.0.7/255.255.0.0 is 10.0.255.255;
    - directed broadcast rejects a /32;
    - interface enumeration excludes down and loopback entries;
    - discovery targets include directed broadcasts and one limited-broadcast fallback.

    Inject a provider returning:

    en0: 192.168.1.20/255.255.255.0, up=true, loopback=false
    utun0: 100.64.0.2/255.255.255.255, up=true, loopback=false
    lo0: 127.0.0.1/255.0.0.0, up=true, loopback=true
    down0: 172.16.0.2/255.255.0.0, up=false, loopback=false

    Assert that only 192.168.1.255 and 255.255.255.255 are send targets. The provider remains test-injected so tests do not depend on the Windows machine's VPN state.

- [ ] Step 2: Run the focused discovery tests and verify Red.

    Run:

    cmake --build build-clean --config Debug --parallel 4 --target ministream_tests
    .\build-clean\tests\Debug\ministream_tests.exe "directed broadcast*"

    Expected: missing interfaces/configuration or failed assertions before production implementation.

- [ ] Step 3: Implement platform enumeration and broadcast calculation.

    For each valid non-loopback IPv4 interface calculate:

    broadcast = (address & netmask) | (~netmask)

    On Windows obtain the prefix/netmask from GetAdaptersAddresses and link iphlpapi in the Asio/core target. On macOS/Linux use ifa_addr, ifa_netmask, ifa_flags, and ifa_broadaddr when present. Skip invalid zero addresses and /32 interfaces. Preserve VPN or virtual interfaces when they are up and have a usable broadcast; do not filter by interface name.

- [ ] Step 4: Implement the non-blocking retry state machine.

    At start, bind one IPv4 socket to an ephemeral port, enable broadcast, and schedule an immediate send. Every 150 ms until the deadline, send the encoded query to every directed broadcast and once to 255.255.255.255. A partial send succeeds if at least one target was sent; all-target failure maps to PermissionDenied for access-denied errors and Send otherwise. Each poll drains up to 256 replies, rejects invalid advertisements, filters controllable hosts, and deduplicates by address plus session_port.

    The default UI timeout is 750 ms. The blocking compatibility wrapper may sleep in 1 ms increments outside the UI thread, but RoleController must never call it.

- [ ] Step 5: Add host-side drain and diagnostic logging.

    Change DiscoveryHost::poll to drain all currently pending query packets up to a fixed 64-query safety limit and reply to each query only when the advertisement is controllable. Log interface, target, received sender, device name, and session port to std::clog at debug level. Never log keys, SAS values, ciphertext, or raw input.

- [ ] Step 6: Run the focused and existing discovery tests.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "directed broadcast*"
    .\build-clean\tests\Debug\ministream_tests.exe "*LAN discovery*"
    .\build-clean\tests\Debug\ministream_tests.exe "discovery*"

    Expected: PASS, including round-trip wire validation and deduplication. Commit:

    git add src/core/session/discovery.hpp src/core/session/discovery.cpp tests/session/discovery_test.cpp src/core/CMakeLists.txt
    git commit -m "fix(discovery): probe active IPv4 broadcasts with bounded retries"

### Task 3: Move discovery off the Qt blocking path and expose actionable states

**Files:**
- Modify: src/app/remote/remote_runtime.hpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/app/ui/role_controller.hpp
- Modify: src/app/ui/role_controller.cpp
- Modify: ui/pages/RemotePage.qml
- Modify: tests/session/remote_runtime_test.cpp
- Modify: tests/ui/role_shell_copy_test.py if copy expectations change

**Interfaces:**
- Add RemoteRuntime discovery states: Idle, Searching, Complete, Failed.
- Add begin_discovery(Microseconds timeout), discovery_state(), and last_discovery_error(). Keep refresh as a compatibility wrapper that starts discovery and returns whether it was accepted, not whether the final list is non-empty.
- Add QML properties searching and discoveryStatus backed by runtime state; retain hosts as the completed result.
- RoleController::findDevices() returns immediately, starts the state machine, and clears old results only when a new search starts. RoleController::tick() advances discovery and emits stateChanged when it completes.

- [ ] Step 1: Add failing runtime state tests.

    Add cases:

    - remote discovery enters Searching without blocking;
    - remote discovery exposes Complete for an empty result;
    - remote discovery preserves Failed and error kind;
    - role controller searching mirrors runtime state;
    - macOS controlled Streaming counts as connected using the pure role-state helper extracted from RoleController.

    The tests must assert that findDevices() does not sleep for the discovery timeout and that an empty result is not represented as a socket failure.

- [ ] Step 2: Run the focused test and verify Red.

    Run:

    cmake --build build-ui --config Debug --parallel 4 --target ministream_tests
    .\build-ui\Debug\ministream_tests.exe "*remote discovery*"
    .\build-ui\Debug\ministream_tests.exe "*macOS controlled Streaming*"

    Expected: the new states and helper are absent or the current synchronous behavior fails the assertions.

- [ ] Step 3: Connect DiscoveryClient to RemoteRuntime.

    RemoteRuntime owns a DiscoveryClient while browsing. Its tick first advances an active discovery search, then handles session packets. On completion it replaces hosts and on failure clears hosts while retaining the typed error. No call from RoleController may invoke the blocking discover_hosts wrapper.

- [ ] Step 4: Map failures without guessing.

    Use these exact user-facing mappings:

    - PermissionDenied: Local network access is blocked. Allow MiniStream in system settings.
    - NoUsableInterface: No usable network interface is available.
    - Bind/Send/Receive: Local network discovery is unavailable. Check firewall settings.
    - Complete with zero hosts: No devices found on the local network.
    - Searching: Searching local network

    Do not report “firewall blocked” as a fact when the socket only reports no replies.

- [ ] Step 5: Fix macOS controlled state and keep the list bounded.

    In RoleController::connected(), branch on mode for both Windows and macOS so controlled Pairing/Streaming and remote Pairing/Streaming are represented consistently. Keep the existing QML content bounds, bind every card text width to the delegate width, and make the Find devices button disabled only while Searching.

- [ ] Step 6: Run focused UI/runtime checks and commit.

    Run:

    .\build-ui\Debug\ministream_tests.exe "*remote discovery*"
    .\build-ui\Debug\ministream_tests.exe "*macOS controlled Streaming*"
    python tests/ui/role_shell_copy_test.py
    python tests/ui/shortcut_policy_test.py

    Commit:

    git add src/app/remote/remote_runtime.hpp src/app/remote/remote_runtime.cpp src/app/ui/role_controller.hpp src/app/ui/role_controller.cpp ui/pages/RemotePage.qml tests/session/remote_runtime_test.cpp tests/ui/role_shell_copy_test.py
    git commit -m "fix(ui): make discovery asynchronous and report network state"

### Task 4: Add macOS Local Network metadata and Windows Private-profile firewall setup

**Files:**
- Create: packaging/macos/Info.plist.in
- Modify: src/macos/CMakeLists.txt
- Create: packaging/windows/ministream_firewall_install.nsh
- Create: packaging/windows/ministream_firewall_uninstall.nsh
- Modify: packaging/Packaging.cmake
- Create: tests/packaging/packaging_contract_test.py
- Modify: tests/ui/role_shell_copy_test.py only if a permission copy assertion is added

**Interfaces:**
- The final macOS bundle must contain NSLocalNetworkUsageDescription with the stable text: MiniStream uses your local network to discover and connect to nearby devices for game streaming.
- The Windows installer rule is scoped to $INSTDIR\bin\ministream.exe, UDP, inbound, enabled, and profile=private. It must be removed by the uninstaller.
- Installer commands must leave Public-profile traffic untouched and must not disable Windows Defender Firewall.

- [ ] Step 1: Add a failing packaging assertion.

    Create tests/packaging/packaging_contract_test.py and add a script-level assertion that the generated macOS Info.plist template contains the exact key and that Packaging.cmake loads both NSIS firewall command files. On Windows, assert the generated CPack configuration contains profile=private, protocol=UDP, the installed executable path, and an uninstall delete command.

    Expected before implementation: the files or strings are absent.

- [ ] Step 2: Configure the macOS bundle.

    Set MACOSX_BUNDLE_INFO_PLIST to the configured Info.plist for the ministream target. Keep the existing bundle identifier/version/icon properties. Do not place a local filesystem path or a session note in the plist.

- [ ] Step 3: Add installer and uninstall rules.

    Use netsh through the elevated NSIS installer:

    advfirewall firewall add rule name="MiniStream (Private UDP)" dir=in action=allow program="$INSTDIR\bin\ministream.exe" enable=yes profile=private protocol=UDP

    The uninstall command deletes the exact rule name. Capture and log the netsh exit code in the installer log; a failed rule must not be silently described as configured.

- [ ] Step 4: Verify generated artifacts rather than templates only.

    On macOS run:

    cmake -S . -B build-release -DMINISTREAM_BUILD_UI=ON -DMINISTREAM_ENABLE_PACKAGING=ON
    cmake --build build-release --config Release
    cpack --config build-release/CPackConfig.cmake -C Release
    plutil -p build-release/MiniStream.app/Contents/Info.plist

    Expected: NSLocalNetworkUsageDescription is present in the final bundle. On Windows build the NSIS package, inspect the generated installer script, install in a disposable VM, query the exact rule with netsh advfirewall firewall show rule name="MiniStream (Private UDP)", uninstall, and query again. Do not mark this task complete from a template grep alone.

- [ ] Step 5: Run the static packaging check and commit.

    Run the focused packaging assertion and git diff --check, then commit:

    git add packaging/macos/Info.plist.in src/macos/CMakeLists.txt packaging/windows/ministream_firewall_install.nsh packaging/windows/ministream_firewall_uninstall.nsh packaging/Packaging.cmake tests/ui/role_shell_copy_test.py
    git commit -m "fix(packaging): declare LAN access and configure private UDP access"

### Task 5: Lock the session peer and remove the one-packet receive ceiling

**Files:**
- Modify: src/core/net/udp_endpoint.hpp
- Modify: src/core/net/udp_endpoint.cpp
- Modify: src/app/remote/remote_runtime.hpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/app/controlled/controlled_runtime.hpp
- Modify: src/app/controlled/controlled_runtime.cpp
- Modify: tests/net/udp_endpoint_test.cpp
- Modify: tests/session/controlled_runtime_test.cpp
- Modify: tests/session/remote_runtime_test.cpp

**Interfaces:**
- Add UdpEndpoint::try_receive_batch(std::size_t max_packets).
- Add UdpEndpoint::lock_peer(const ReceivedDatagram&), clear_peer(), and peer_locked().
- Once locked, receive drops datagrams whose address or port differs from the peer and reply always uses the locked endpoint. Remove last_sender_ as a reply target.
- set_remote() establishes the expected remote address and session port for the controller side; the controlled side locks the sender only after a valid, accepted Hello.
- Both runtimes process up to 512 ready datagrams per tick and stop early on would_block. The limit is a starvation guard, not a throughput target.

- [ ] Step 1: Add failing endpoint tests.

    Add:

    - batch receive drains all currently queued loopback packets;
    - reply remains bound after a stray sender datagram;
    - locked endpoint rejects a different source port;
    - set_remote establishes the expected source endpoint.

    Use three loopback endpoints for the stray-sender case. Send a valid request from A, lock A, send noise from B, call reply, and assert only A receives the reply.

- [ ] Step 2: Run the focused tests and verify Red.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "*batch receive*"
    .\build-clean\tests\Debug\ministream_tests.exe "*stray sender*"

    Expected: the batch/peer API is absent and the current last_sender_ behavior fails the destination assertion.

- [ ] Step 3: Implement peer-aware batch receive.

    Keep all socket operations non-blocking. In try_receive_batch, loop until max_packets or would_block, discard oversize/error datagrams, and never update a reply target from an unvalidated sender. When peer_locked is true, filter both address and port before returning a datagram.

- [ ] Step 4: Integrate the batch into both runtimes.

    ControlledRuntime::tick drains discovery first, then processes the session batch. On a valid Hello that passes role, capability, and negotiated-profile checks, call lock_peer before sending Accept. RemoteRuntime uses the selected host endpoint from set_remote and drains the same batch during Connecting, Pairing, and Streaming.

- [ ] Step 5: Add the packet-rate regression.

    Send 5000 small encrypted-looking loopback datagrams into the endpoint, call try_receive_batch repeatedly with a 512 limit, and assert all 5000 are observed without a one-per-tick helper call. This is a correctness regression for the old ceiling, not a hardware benchmark.

- [ ] Step 6: Run focused transport/runtime tests and commit.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "*batch receive*"
    .\build-clean\tests\Debug\ministream_tests.exe "*peer*"
    .\build-clean\tests\Debug\ministream_tests.exe "*runtime*"

    Commit:

    git add src/core/net/udp_endpoint.hpp src/core/net/udp_endpoint.cpp src/app/remote/remote_runtime.hpp src/app/remote/remote_runtime.cpp src/app/controlled/controlled_runtime.hpp src/app/controlled/controlled_runtime.cpp tests/net/udp_endpoint_test.cpp tests/session/controlled_runtime_test.cpp tests/session/remote_runtime_test.cpp
    git commit -m "fix(transport): drain UDP batches and lock the session peer"

### Task 6: Make Hello, pairing offer, and confirmation finite and retryable

**Files:**
- Modify: src/core/session/role.hpp
- Modify: src/core/session/role.cpp
- Modify: src/core/session/handshake.hpp
- Modify: src/core/session/handshake.cpp
- Modify: src/app/remote/remote_runtime.hpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/app/controlled/controlled_runtime.hpp
- Modify: src/app/controlled/controlled_runtime.cpp
- Modify: tests/session/role_test.cpp
- Modify: tests/session/handshake_test.cpp
- Modify: tests/session/controlled_runtime_test.cpp
- Modify: tests/session/remote_runtime_test.cpp

**Interfaces:**
- Add a transient RemoteConnecting state between RemoteBrowsing and Pairing; transitions to RemoteBrowsing on a bounded timeout and to Pairing only after a valid Accept and peer offer.
- Keep HandshakeRetrier for Hello and add a small PairingMessageRetrier for offer/confirmation with interval 250 ms and maximum four sends per phase.
- Runtime connect becomes a non-blocking start operation. The tick drives retry, receive, and timeout; no one-second receive call runs on the UI thread.
- Duplicate Hello, offer, or confirmation from the locked peer is idempotent: resend the existing response and do not regenerate the identity, ephemeral key, local offer, or session keys.

- [ ] Step 1: Add failing retry/state tests.

    Add:

    - RemoteBrowsing enters RemoteConnecting without a one-second pause;
    - Hello is sent at t=0, 250, 500, and 750 ms only;
    - pairing offer is resent after a dropped first offer;
    - confirmation is resent until peer confirmation arrives;
    - controlled duplicate Hello returns the same Accept nonce;
    - controlled duplicate offer returns the same peer offer;
    - all retry phases return to browsing after their deadline.

- [ ] Step 2: Run the focused tests and verify Red.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "*retries*"
    .\build-clean\tests\Debug\ministream_tests.exe "*RemoteConnecting*"
    .\build-clean\tests\Debug\ministream_tests.exe "*duplicate Hello*"

    Expected: the current synchronous connect and one-shot pairing paths fail the new cases.

- [ ] Step 3: Implement the state machine.

    Store the selected host, Hello, retrier, phase deadline, and existing pairing objects in RemoteRuntime. On Accept, validate nonce, role, codec, dimensions, fps, bitrate bounds, and peer endpoint before entering the offer phase. On a valid peer offer, compute the SAS and expose Pairing. Keep the current manual confirmation requirement.

- [ ] Step 4: Make controlled responses idempotent.

    Cache the accepted Hello nonce and current response objects during the active session. Re-send Accept or the existing PairingOffer for matching retries; reject a different Hello after peer lock. A failed or cancelled phase clears the cache and returns to the existing browsing/broadcasting state.

- [ ] Step 5: Update UI status and run tests.

    Show Connecting to <device> while RemoteConnecting, disable duplicate Connect actions, and keep Disconnect/close cleanup paths calling release_input before teardown.

    Run the focused tests, the full handshake test group, and both UI scripts. Commit:

    git add src/core/session/role.hpp src/core/session/role.cpp src/core/session/handshake.hpp src/core/session/handshake.cpp src/app/remote/remote_runtime.hpp src/app/remote/remote_runtime.cpp src/app/controlled/controlled_runtime.hpp src/app/controlled/controlled_runtime.cpp tests/session/role_test.cpp tests/session/handshake_test.cpp tests/session/controlled_runtime_test.cpp tests/session/remote_runtime_test.cpp ui/pages/RemotePage.qml
    git commit -m "fix(session): retry handshake phases without blocking the UI"

### Task 7: Negotiate a common stream profile and constrain the platform encoder

**Files:**
- Modify: src/platform/capabilities.hpp
- Modify: src/windows/platform/host_capabilities.cpp
- Modify: src/windows/platform/controlled_backend.cpp
- Modify: src/macos/platform/controlled_backend.mm
- Modify: src/app/ui/role_controller.cpp
- Modify: src/core/config/stream_profile.hpp
- Modify: src/core/config/stream_profile.cpp
- Modify: src/core/session/handshake.hpp
- Modify: src/core/session/handshake.cpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: tests/config/stream_profile_test.cpp
- Modify: tests/session/handshake_test.cpp
- Modify: tests/windows/host_capabilities_test.cpp
- Modify: tests/macos/controlled_backend_test.mm

**Interfaces:**
- Extend ControlledCapabilities and RemoteCapabilities with explicit h264, hevc, hdr10, max_width, max_height, and max_fps fields while preserving the existing ready/detail fields.
- Add a pure select_common_stream_profile(const DiscoveredHost&, const RemoteCapabilities&) function. It considers Quality4K, Balanced1440, and Debug1080 in preference order, but accepts only a profile whose codec, HDR flag, dimensions, and frame rate are supported by both peers. It clamps bitrate to the selected profile range.
- Make the controlled advertisement use the backend's real codec flags and limits. Never infer HEVC/H.264 support merely from one generic video-ready boolean.
- Extend the Hello and Accept wire payloads with one explicit HDR10 flag and increase their fixed lengths by one byte. The decoder must reject an HDR request unless both the advertisement and the backend capability report HDR10; current controlled backends therefore select SDR until their HDR path is implemented. No silent HDR claim is allowed.

- [ ] Step 1: Add failing profile-selection tests.

    Add cases:

    - a host advertising only H.264 1920×1080@60 selects Debug1080 and never Quality4K;
    - a host advertising HEVC 2560×1440@60 selects Balanced1440 only when the remote decoder advertises HEVC;
    - a host with max 1280×720 rejects all current profiles;
    - HDR is selected only when both sides report HDR10 and the backend accepts it;
    - the selected bitrate lies between the profile minimum and maximum.

- [ ] Step 2: Run focused profile tests and verify Red.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "*profile selection*"
    .\build-clean\tests\Debug\ministream_tests.exe "*host capabilities*"

    Expected: the new capability fields/helper are absent and the current remote behavior would request 4K regardless of advertisement.

- [ ] Step 3: Populate real platform capabilities.

    Windows derives H.264/HEVC support from the NVENC/decoder probes already used by the platform; macOS derives it from VideoToolbox. The maximum dimensions and fps are the largest dimensions the capture/encoder path can actually configure, not a label copied from the remote preference.

- [ ] Step 4: Select and validate the profile on both sides.

    RemoteRuntime computes the common profile before creating Hello. ControlledRuntime validates Hello against its advertisement and backend limits, then configures the exact requested codec, width, height, fps, HDR flag, and bitrate. If no common profile exists, return a typed incompatibility status instead of entering Pairing.

- [ ] Step 5: Make capture dimensions match the negotiation.

    Add explicit output-size configuration to the Windows DXGI/NVENC path and to the macOS display capture path. A captured native 5K frame must not be handed to an encoder configured for 1920×1080 without a real scaler or an encoder-supported input/output conversion. The emitted CodecConfig must report the actual negotiated output size.

- [ ] Step 6: Run unit/platform compile checks and commit.

    Run the profile and handshake groups plus platform tests on their native hosts. Commit:

    git add src/platform/capabilities.hpp src/windows/platform/host_capabilities.cpp src/windows/platform/controlled_backend.cpp src/macos/platform/controlled_backend.mm src/app/ui/role_controller.cpp src/core/config/stream_profile.hpp src/core/config/stream_profile.cpp src/core/session/handshake.hpp src/core/session/handshake.cpp src/app/remote/remote_runtime.cpp tests/config/stream_profile_test.cpp tests/session/handshake_test.cpp tests/windows/host_capabilities_test.cpp tests/macos/controlled_backend_test.mm
    git commit -m "fix(video): negotiate a common profile before encoder startup"

### Task 8: Apply negotiated bitrate and make CodecConfig delivery edge-triggered

**Files:**
- Modify: src/core/transport/packet_scheduler.hpp
- Modify: src/core/transport/packet_scheduler.cpp
- Modify: src/app/controlled/controlled_runtime.hpp
- Modify: src/app/controlled/controlled_runtime.cpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/platform/controlled_backend.hpp
- Modify: src/windows/platform/controlled_backend.hpp
- Modify: src/windows/platform/controlled_backend.cpp
- Modify: src/macos/platform/controlled_backend.hpp
- Modify: src/macos/platform/controlled_backend.mm
- Modify: tests/transport/packet_scheduler_test.cpp
- Modify: tests/session/controlled_runtime_test.cpp
- Modify: tests/session/remote_runtime_test.cpp

**Interfaces:**
- Add PacketScheduler::video_rate_bps() for observability and test assertions.
- Add ControlledBackend::reconfigure_bitrate(std::uint32_t) with a default failure result or bool that preserves source compatibility for simple test backends.
- Track the last CodecConfig sent by ControlledRuntime. Send it only when parameter sets first become available or any codec/dimension/fps/HDR/parameter-set value changes.
- RemoteRuntime must gate Video processing on codec_configured_. It may discard video received before config, but it must not grow an unbounded pre-config queue.

- [ ] Step 1: Add failing scheduler/config tests.

    Add:

    - controlled media scheduler starts at the negotiated bitrate;
    - scheduler delay reflects a 50 Mbps negotiated rate rather than 20 Mbps;
    - codec config is emitted once for an unchanged parameter set;
    - video before codec config is not sent to the decoder;
    - new parameter sets reopen the decoder configuration gate.

- [ ] Step 2: Run the focused tests and verify Red.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "*negotiated bitrate*"
    .\build-clean\tests\Debug\ministream_tests.exe "*codec config*"

    Expected: the scheduler has no public rate inspection, the runtime never applies negotiated_bitrate_, and codec_configured_ is not used as a video gate.

- [ ] Step 3: Apply the rate at media-sender creation.

    Immediately after constructing PacketScheduler, call set_video_rate(negotiated_bitrate_) after clamping to the selected profile. When ABR changes the rate, call the backend encoder reconfiguration and scheduler update as one runtime operation; if encoder reconfiguration fails, retain the previous scheduler rate and emit a diagnostic.

- [ ] Step 4: Edge-trigger CodecConfig and gate the decoder.

    Compare the complete CodecConfig, including parameter_sets. Send a Control packet only after a non-empty config is available and only when it differs from the cached value. On the remote, configure the decoder from a valid config, set codec_configured_ only on success, and ignore subsequent Video packets until the gate is true.

- [ ] Step 5: Run focused media tests and commit.

    Run the scheduler, media pipeline, codec wire, and runtime groups. Commit:

    git add src/core/transport/packet_scheduler.hpp src/core/transport/packet_scheduler.cpp src/app/controlled/controlled_runtime.hpp src/app/controlled/controlled_runtime.cpp src/app/remote/remote_runtime.cpp src/platform/controlled_backend.hpp src/windows/platform/controlled_backend.hpp src/windows/platform/controlled_backend.cpp src/macos/platform/controlled_backend.hpp src/macos/platform/controlled_backend.mm tests/transport/packet_scheduler_test.cpp tests/session/controlled_runtime_test.cpp tests/session/remote_runtime_test.cpp
    git commit -m "fix(media): honor negotiated bitrate and codec readiness"

### Task 9: Connect FEC to the real video media path

**Files:**
- Create: src/core/fec/video_fec.hpp
- Create: src/core/fec/video_fec.cpp
- Modify: src/core/fec/fec_codec.hpp
- Modify: src/core/fec/fec_codec.cpp
- Modify: src/core/transport/packetizer.hpp
- Modify: src/core/transport/packetizer.cpp
- Modify: src/core/transport/reassembler.hpp
- Modify: src/core/transport/reassembler.cpp
- Modify: src/core/media/media_pipeline.hpp
- Modify: src/core/media/media_pipeline.cpp
- Modify: src/app/controlled/controlled_runtime.cpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/core/CMakeLists.txt
- Modify: tests/fec/fec_codec_test.cpp
- Create: tests/transport/video_fec_pipeline_test.cpp

**Interfaces:**
- Use the existing PacketType::VideoFec. Do not change the encryption envelope.
- Add a fixed, versioned VideoFecHeader carrying frame_id, capture_timestamp_us, shard_index, data_shards, parity_shards, shard_bytes, frame_bytes, keyframe flag, and reserved bits. Data shards are padded to a fixed 64-byte-aligned size small enough for the FEC header plus parity and the existing AEAD overhead to stay under kMaxDatagramBytes.
- Add VideoFecEncoder::encode_frame(frame, parity_ratio) returning normal Video datagrams plus VideoFec parity datagrams.
- Add VideoFecReassembler::push_data, push_parity, expire, and counters for recovered and unrecoverable frames. Recovered data shards must be converted into the same deterministic video shard representation consumed by FrameReassembler.
- MediaReceiver exposes recovered/unrecoverable counters without exposing Leopard internals to the runtime.

- [ ] Step 1: Add failing pipeline tests.

    Create a synthetic encoded frame larger than three shards. Encode with a non-zero parity ratio, remove one normal Video datagram, feed the rest plus parity through MediaReceiver, and assert the original EncodedFrame is returned. Add a second case removing more shards than parity and assert no frame is returned and exactly one unrecoverable frame is counted.

    Add a size assertion for every encrypted normal and FEC datagram: bytes.size() <= kMaxDatagramBytes.

- [ ] Step 2: Run the focused FEC pipeline test and verify Red.

    Run:

    cmake --build build-clean --config Debug --parallel 4 --target ministream_tests
    .\build-clean\tests\Debug\ministream_tests.exe "*FEC pipeline*"

    Expected: PacketType::VideoFec has no media producer/consumer and the test cannot recover the removed shard.

- [ ] Step 3: Define and validate the FEC wire header.

    Use big-endian integers and reject zero counts, shard indexes outside total count, inconsistent frame_bytes, unsupported version, and a payload exceeding the sealed datagram budget. Keep the normal Video media header unchanged for data packets.

- [ ] Step 4: Integrate FEC in MediaSender.

    Packetize the frame data with the FEC-aware shard size, calculate parity count as ceil(data_shards × fec_ratio) with a minimum of one parity shard for a non-empty frame and a maximum that keeps the total bounded, encode parity through FecCodec, authenticate every parity packet as PacketType::VideoFec, and enqueue both data and parity at the existing Video priority and deadline.

- [ ] Step 5: Integrate FEC in MediaReceiver.

    Store bounded per-frame FEC state, accept normal and parity shards in any order, recover when at least data_shards are present, pass recovered data through the existing frame reassembler, and expire incomplete state at the existing frame deadline. A frame that cannot recover is counted once and removed.

- [ ] Step 6: Run all FEC/media tests and commit.

    Run the existing FecCodec tests, the new pipeline tests, media pipeline tests, and the full C++ target. Commit:

    git add src/core/fec/video_fec.hpp src/core/fec/video_fec.cpp src/core/fec/fec_codec.hpp src/core/fec/fec_codec.cpp src/core/transport/packetizer.hpp src/core/transport/packetizer.cpp src/core/transport/reassembler.hpp src/core/transport/reassembler.cpp src/core/media/media_pipeline.hpp src/core/media/media_pipeline.cpp src/app/controlled/controlled_runtime.cpp src/app/remote/remote_runtime.cpp src/core/CMakeLists.txt tests/fec/fec_codec_test.cpp tests/transport/video_fec_pipeline_test.cpp
    git commit -m "feat(media): connect FEC to encrypted video packets"

### Task 10: Connect feedback, RateController, encoder, scheduler, and FEC ratio

**Files:**
- Create: src/core/telemetry/feedback_wire.hpp
- Create: src/core/telemetry/feedback_wire.cpp
- Modify: src/core/telemetry/stream_aggregator.hpp
- Modify: src/core/telemetry/stream_aggregator.cpp
- Modify: src/core/telemetry/stream_snapshot.hpp
- Modify: src/app/remote/remote_runtime.hpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/app/controlled/controlled_runtime.hpp
- Modify: src/app/controlled/controlled_runtime.cpp
- Modify: src/platform/controlled_backend.hpp
- Modify: src/windows/video/nvenc_encoder.cpp
- Modify: src/windows/platform/controlled_backend.cpp
- Modify: src/macos/video/videotoolbox_encoder.hpp
- Modify: src/macos/video/videotoolbox_encoder.mm
- Modify: src/macos/platform/controlled_backend.mm
- Modify: tests/telemetry/feedback_wire_test.cpp
- Modify: tests/adaptation/rate_controller_test.cpp
- Modify: tests/session/controlled_runtime_test.cpp
- Modify: tests/session/remote_runtime_test.cpp

**Interfaces:**
- Add an authenticated Feedback payload kind distinct from the existing six-byte rumble payload. The network report contains a monotonically increasing report sequence, received/lost video packet counts, jitter, FEC recovered count, FEC unrecoverable count, and receiver timestamp fields with bounded integer ranges.
- RemoteRuntime sends a report at most every 100 ms while Streaming. ControlledRuntime combines it with local scheduler queue delay and applies RateController decisions.
- A decision updates encoder bitrate, PacketScheduler video rate, and the next-frame FEC ratio. Encoder failure does not silently update only the scheduler.
- Add runtime-visible counters for TX/RX packets per second, Mbps, captured/encoded/received/decoded/rendered frame counts, dropped packets/frames, FEC recovery, queue delay, and audio underruns. Console/debug logging is sufficient; no dashboard is required.

- [ ] Step 1: Add failing feedback wire tests.

    Encode/decode a report with known counters, reject unknown kind, truncated fields, oversized counts, and a sequence mismatch. Assert a six-byte rumble packet still decodes as rumble and is not treated as a report.

- [ ] Step 2: Run the focused feedback tests and verify Red.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "*feedback wire*"
    .\build-clean\tests\Debug\ministream_tests.exe "*rate controller*"

    Expected: there is no network feedback wire or runtime consumer.

- [ ] Step 3: Add the receiver report source.

    Track video packet sequence gaps and FEC outcomes in MediaReceiver/RemoteRuntime. Send the encrypted report through the existing session peer. Reset counters on disconnect and do not include addresses, keys, input payloads, or SAS values.

- [ ] Step 4: Apply decisions atomically at the controlled side.

    Build NetworkFeedback from the report plus scheduler estimated_video_queue_delay. Call RateController::update at the report cadence. Apply the returned bitrate and ratio to all three consumers before sending the next video frame. Use the selected profile's minimum and maximum as the controller bounds.

- [ ] Step 5: Add feedback behavior tests.

    Assert that a loss report reduces bitrate and increases the FEC ratio, that two stable seconds increase bitrate slowly, that encoder reconfiguration failure leaves the previous rate intact, and that the scheduler getter, encoder configuration, and next-frame FEC ratio agree after a successful decision.

- [ ] Step 6: Run tests and commit.

    Run the feedback wire, RateController, media, and runtime groups. Commit:

    git add src/core/telemetry/feedback_wire.hpp src/core/telemetry/feedback_wire.cpp src/core/telemetry/stream_aggregator.hpp src/core/telemetry/stream_aggregator.cpp src/core/telemetry/stream_snapshot.hpp src/app/remote/remote_runtime.hpp src/app/remote/remote_runtime.cpp src/app/controlled/controlled_runtime.hpp src/app/controlled/controlled_runtime.cpp src/platform/controlled_backend.hpp src/windows/video/nvenc_encoder.cpp src/windows/platform/controlled_backend.cpp src/macos/video/videotoolbox_encoder.hpp src/macos/video/videotoolbox_encoder.mm src/macos/platform/controlled_backend.mm tests/telemetry/feedback_wire_test.cpp tests/adaptation/rate_controller_test.cpp tests/session/controlled_runtime_test.cpp tests/session/remote_runtime_test.cpp
    git commit -m "feat(abr): close the feedback to encoder and FEC loop"

### Task 11: Fix audio loss recovery and replace macOS microphone capture

**Files:**
- Modify: src/core/audio/jitter_buffer.hpp
- Modify: src/core/audio/jitter_buffer.cpp
- Modify: src/app/remote/remote_runtime.cpp
- Create: src/macos/audio/system_audio_capture.hpp
- Create: src/macos/audio/system_audio_capture.mm
- Modify: src/macos/platform/controlled_backend.hpp
- Modify: src/macos/platform/controlled_backend.mm
- Modify: src/macos/CMakeLists.txt
- Modify: tests/audio/jitter_buffer_test.cpp
- Modify: tests/macos/controlled_backend_test.mm
- Create: tests/macos/system_audio_capture_test.mm

**Interfaces:**
- AudioJitterBuffer keeps bounded duration and returns one playout decision per expected sequence. A PLC decision must advance the runtime's expected sequence exactly once; receiving a later packet must not leave the stream pinned to the missing sequence.
- SystemAudioCapture exposes start, read, stop, and a capability/error detail. It captures system/application output through the supported ScreenCaptureKit audio stream, converts buffers to 48 kHz stereo float PCM, and uses a bounded latest/queue policy so capture cannot grow memory without limit.
- MacControlledBackend reports system audio only when the system-audio path is available; it no longer reports CoreAudio microphone input as game audio.

- [ ] Step 1: Add the failing PLC regression.

    Push packets 1 and 2 into the jitter path, request sequence 0, assert PLC and expected sequence advances to 1, then request/play packet 1 and 2 successfully. A repeated pop(0) must not be used as the runtime's clock.

- [ ] Step 2: Run the focused audio test and verify Red.

    Run:

    .\build-clean\tests\Debug\ministream_tests.exe "*PLC*"

    Expected: the current jitter buffer returns PLC for a missing sequence but the runtime has no advancement assertion, so the integration regression fails or the new helper is absent.

- [ ] Step 3: Implement the bounded audio loss path.

    Advance expected_audio_sequence_ on PLC in RemoteRuntime, preserve Opus decoder state, and count the underrun. Keep the existing 20 ms jitter cap and do not turn missing audio into unbounded waiting.

- [ ] Step 4: Add the system-audio capture implementation.

    Use ScreenCaptureKit's audio output stream and its availability/permission result. Convert each native audio buffer to interleaved stereo float at 48 kHz before returning PcmBlock. Stop and release the stream before destroying the queue. If the OS reports that system-audio capture is unavailable, return a specific capability failure and prevent Allow control; do not silently fall back to the microphone.

- [ ] Step 5: Add native/pure tests.

    Test the PCM conversion and bounded queue with deterministic sample buffers. On macOS, add a probe that starts and stops the real system-audio capture and asserts its capability detail does not contain microphone. The probe may be skipped only when the host lacks the required native API, and the skip reason must be printed.

- [ ] Step 6: Run tests and commit.

    Run the jitter group everywhere and the macOS audio tests on macOS. Commit:

    git add src/core/audio/jitter_buffer.hpp src/core/audio/jitter_buffer.cpp src/app/remote/remote_runtime.cpp src/macos/audio/system_audio_capture.hpp src/macos/audio/system_audio_capture.mm src/macos/platform/controlled_backend.hpp src/macos/platform/controlled_backend.mm src/macos/CMakeLists.txt tests/audio/jitter_buffer_test.cpp tests/macos/controlled_backend_test.mm tests/macos/system_audio_capture_test.mm
    git commit -m "fix(audio): advance PLC and capture macOS system audio"

### Task 12: Make macOS video encoding asynchronous and name the capture implementation accurately

**Files:**
- Create: src/macos/video/cgdisplaystream_capture.hpp
- Create: src/macos/video/cgdisplaystream_capture.mm
- Delete only after replacement is compiled: src/macos/video/screencapturekit_capture.hpp
- Delete only after replacement is compiled: src/macos/video/screencapturekit_capture.mm
- Modify: src/macos/platform/controlled_backend.mm
- Modify: src/macos/CMakeLists.txt
- Modify: src/macos/video/videotoolbox_encoder.hpp
- Modify: src/macos/video/videotoolbox_encoder.mm
- Modify: tests/macos/controlled_backend_test.mm

**Interfaces:**
- Rename the existing CGDisplayStream-backed class to CGDisplayStreamCapture so class/file/README names describe the actual API. This is a behavior-preserving correction; it is not a migration to a new capture API.
- Add a capture output-size configuration that the profile negotiation task can call before stream start/restart.
- Change VideoToolboxEncoder to submit frames and expose take_latest() for callback-produced EncodedFrame values. Do not call VTCompressionSessionCompleteFrames from every encode call; call it during stop and before invalidation.
- Keep only one latest encoded frame under a mutex and release all Core Foundation objects on stop.

- [ ] Step 1: Add failing asynchronous-encode tests.

    Add a native test that submits two frames, waits for callback output through the bounded latest-frame accessor, and asserts the encoder does not call a per-frame flush path. Add a source-level assertion that the production encode method contains no CompleteFrames call.

- [ ] Step 2: Run the focused macOS test before implementation.

    Run on macOS:

    cmake --build build-release --config Debug --parallel 4 --target ministream_tests
    ./build-release/tests/Debug/ministream_tests "*VideoToolbox*"

    Expected: the current synchronous interface or source assertion fails.

- [ ] Step 3: Rename and register the actual display capture.

    Move the existing CGDisplayStream implementation into the new name, update includes and CMake sources, and update architecture/release wording only where it currently claims ScreenCaptureKit for this class. Preserve permission checks, latest-frame ownership, and display timestamps.

- [ ] Step 4: Implement submit/callback/take-latest.

    The callback writes the latest encoded frame and parameter sets. The controlled backend submits the current pixel buffer, takes the most recent completed output, and returns it to MediaSender. It may return no frame while the callback is pending; the runtime must keep polling without blocking.

- [ ] Step 5: Run native tests and commit.

    Run the macOS platform tests, full C++ tests, and git diff --check. Commit:

    git add src/macos/video/cgdisplaystream_capture.hpp src/macos/video/cgdisplaystream_capture.mm src/macos/video/screencapturekit_capture.hpp src/macos/video/screencapturekit_capture.mm src/macos/platform/controlled_backend.mm src/macos/CMakeLists.txt src/macos/video/videotoolbox_encoder.hpp src/macos/video/videotoolbox_encoder.mm tests/macos/controlled_backend_test.mm
    git commit -m "perf(macos): use callback-driven VideoToolbox output"

### Task 13: Add the complete loopback integration test and runtime telemetry

**Files:**
- Create: tests/integration/remote_control_integration_test.cpp
- Modify: tests/CMakeLists.txt
- Modify: src/app/controlled/controlled_runtime.hpp
- Modify: src/app/controlled/controlled_runtime.cpp
- Modify: src/app/remote/remote_runtime.hpp
- Modify: src/app/remote/remote_runtime.cpp
- Modify: src/core/media/media_pipeline.hpp
- Modify: src/core/media/media_pipeline.cpp
- Modify: src/core/telemetry/stream_snapshot.hpp
- Modify: src/core/telemetry/stream_aggregator.hpp
- Modify: src/core/telemetry/stream_aggregator.cpp
- Modify: tests/telemetry/stream_aggregator_test.cpp

**Interfaces:**
- Add a test-only runtime configuration that uses a loopback discovery port while production defaults remain 47990.
- Fake backends produce deterministic H.264-like EncodedFrame bytes, deterministic Opus-sized audio packets, and record injected input/decoded frames. They do not claim hardware support.
- The integration test drives both runtimes by calling ticks and uses actual UdpEndpoint/SessionCrypto/pairing/media classes.
- Telemetry records packet/frame counters and queue/audio/FEC metrics at 100 ms intervals; it is observable through a callback or snapshot but does not require a UI dashboard.

- [ ] Step 1: Add the failing end-to-end test skeleton.

    Create one test named loopback control session completes handshake pairing and media. It must drive:

    start controlled → asynchronous discovery → selected host → Hello/Accept → pairing offer → equal SAS → both confirmations → Streaming → encrypted input → encrypted video → encrypted audio → disconnect.

    Assert that a deliberately replayed encrypted input packet is not injected twice; this reuses existing security behavior and is not a new security feature.

- [ ] Step 2: Run it and verify Red.

    Run:

    cmake --build build-clean --config Debug --parallel 4 --target ministream_tests
    .\build-clean\tests\Debug\ministream_tests.exe "*loopback control session*"

    Expected: the current synchronous runtime, fixed discovery port, one-shot handshake, and absent media feedback seams prevent completion.

- [ ] Step 3: Add deterministic fake backends and runtime configuration.

    Expose only test configuration for discovery port and time source. Make fake backends report H.264/1080p60, audio, and input as ready, and make each generated frame/audio packet carry an incrementing sequence for assertions.

- [ ] Step 4: Drive and assert every phase.

    Pump both runtimes until each explicit deadline. Assert selected host address/session port, equal SAS, Streaming state on both sides, input receipt, decoded video bytes, played audio samples, FEC counters, and clean return to browsing/idle after disconnect. A timeout fails with the last runtime state and error kind.

- [ ] Step 5: Add a receive-load case.

    Push at least 5000 small packets through the test endpoint and assert the batch path processes all packets. Keep this as a deterministic count test; do not print a fabricated Mbps or fps claim.

- [ ] Step 6: Run integration, full suite, and commit.

    Run the new integration test, all existing tests, and the telemetry tests. Commit:

    git add tests/integration/remote_control_integration_test.cpp tests/CMakeLists.txt src/app/controlled/controlled_runtime.hpp src/app/controlled/controlled_runtime.cpp src/app/remote/remote_runtime.hpp src/app/remote/remote_runtime.cpp src/core/media/media_pipeline.hpp src/core/media/media_pipeline.cpp src/core/telemetry/stream_snapshot.hpp src/core/telemetry/stream_aggregator.hpp src/core/telemetry/stream_aggregator.cpp tests/telemetry/stream_aggregator_test.cpp
    git commit -m "test(integration): exercise the complete loopback control path"

### Task 14: Validate platform behavior, package dependencies, and release documentation

**Files:**
- Modify: cmake/Dependencies.cmake
- Modify: packaging/Packaging.cmake
- Modify: README.md
- Create: docs/release/v0.2-functional-reliability-checklist.md
- Create: tests/packaging/version_contract_test.py
- Inspect: docs/release/v0.1-alpha-checklist.md
- Inspect: docs/superpowers/specs/2026-08-30-dynamic-device-discovery-design.md

**Interfaces:**
- macOS packaging must bundle or otherwise resolve the imported libsodium library in the final app. README must not claim end users need Homebrew libsodium if the package is self-contained; if the package remains development-only, the build section must say so accurately.
- The v0.2 checklist contains only durable acceptance requirements and evidence fields; it must not contain agent session commentary, private paths, or fabricated results.
- Platform tests are recorded separately from unit results: Windows↔macOS discovery, permission transitions, firewall on/off, 20 repeated searches, 1080p60 media, audio/input, packet-load, loss/FEC, ABR, and 30–60 minute stability.

- [ ] Step 1: Add failing package-dependency checks.

    On macOS build the app and run:

    otool -L MiniStream.app/Contents/MacOS/ministream

    Assert that the libsodium dependency resolves inside the bundle or to a deliberately documented system path. On a clean Mac without Homebrew libsodium, launching the packaged app must be the decisive test.

- [ ] Step 2: Implement dependency closure.

    Extend the macOS install/deploy path to copy the imported libsodium dylib into the bundle, set its install name/rpath consistently with the Qt deployment output, and preserve the existing Windows DLL installation. Do not commit copied binaries; verify them only in the build/package directory.

- [ ] Step 3: Build the durable v0.2 checklist.

    Include these checkboxes:

    - Allow control on Windows and macOS;
    - Windows↔macOS and reverse discovery over Wi-Fi;
    - 20 consecutive Find devices runs with no intermittent missing host;
    - local-network permission first request, denial, and re-enable;
    - Private-profile firewall install/uninstall behavior;
    - Hello/Accept/pairing and disconnect cleanup;
    - baseline 1920×1080@60 H.264 SDR;
    - system audio, keyboard, mouse, gamepad optionality, and rumble;
    - receive-load count, FEC recovery at configured loss, and ABR bitrate decrease/recovery;
    - tamper/replay regression tests remain green;
    - 30–60 minute real session with crash, deadlock, memory, latency, audio drift, and input checks.

    Leave every result unchecked until the corresponding command or real-device evidence exists.

- [ ] Step 4: Update README only for stable contracts.

    Replace the v0.1 release-checklist link with the v0.2 checklist only when that checklist is the current release contract. Document the Private-profile firewall prerequisite and macOS Local Network/Screen Recording/Accessibility permission behavior. Do not add a “fixed in this commit” paragraph or private build paths.

- [ ] Step 5: Run documentation/package checks and commit.

    Run:

    git diff --name-status
    git diff --check
    python tools/check_ui_copy.py ui
    cmake --build build-clean --config Debug --parallel 4 --target ministream_tests
    .\build-clean\tests\Debug\ministream_tests.exe

    Inspect the complete diff of README.md and the new release checklist. Commit:

    git add cmake/Dependencies.cmake packaging/Packaging.cmake README.md docs/release/v0.2-functional-reliability-checklist.md
    git commit -m "docs(release): define MiniStream v0.2 functional acceptance"

### Task 15: Version, verify, push v0.2, and merge main

**Files:**
- Modify: CMakeLists.txt
- Inspect: all files changed by Tasks 1–14

**Interfaces:**
- Project and package version becomes 0.2.0.
- The release tag is v0.2.0.
- The current feature branch remains available; do not delete or rename it as part of this plan.
- Integration is a fast-forward merge into main only after the source branch is pushed and the final review is clean.

- [ ] Step 1: Add the failing version assertion.

    Add or update tests/packaging/version_contract_test.py so it reads the project declaration and Packaging.cmake and asserts that both derive their package name from PROJECT_VERSION. Before changing CMakeLists.txt, the assertion must observe 0.1.0 and fail the required 0.2.0 expectation.

- [ ] Step 2: Bump the version.

    Change only the project version in CMakeLists.txt from 0.1.0 to 0.2.0. CPack and both native bundle version properties consume PROJECT_VERSION; verify that no unrelated version strings are invented.

- [ ] Step 3: Run the complete local verification.

    Run:

    cmake --build build-clean --config Debug --parallel 4 --target ministream_tests
    .\build-clean\tests\Debug\ministream_tests.exe
    python tests/ui/role_shell_copy_test.py
    python tests/ui/shortcut_policy_test.py
    python tools/check_ui_copy.py ui
    git diff --check
    git status --short
    git diff --name-status

    On native platforms also run the hardware probes and package checks from Task 14. Report unverified Windows/macOS gates as unverified; do not infer them from the Windows build.

- [ ] Step 4: Request read-only code review before merge.

    The reviewer must inspect the complete diff against the current main base and report only Critical/Important/Minor findings, with file and line references. Resolve every Critical/Important finding and rerun affected tests. No reviewer may edit the worktree.

- [ ] Step 5: Commit the version boundary and push the source branch.

    Use a focused release commit:

    git add CMakeLists.txt
    git commit -m "release: prepare MiniStream v0.2.0"
    git push origin feat/ministream-v0.1

    Confirm local HEAD equals origin/feat/ministream-v0.1 before proceeding.

- [ ] Step 6: Fast-forward main and push it.

    First verify the remote main has not moved:

    git fetch origin main feat/ministream-v0.1 --tags
    git switch main
    git merge --ff-only origin/feat/ministream-v0.1
    git push origin main

    If fast-forward is not possible, stop and report the divergence; do not force-push or create an unreviewed merge commit.

- [ ] Step 7: Create and push the release tag after main points at the reviewed commit.

    git tag -a v0.2.0 -m "MiniStream v0.2.0"
    git push origin v0.2.0
    git status --short --branch
    git log -2 --oneline --decorate

    Final evidence must show origin/main and v0.2.0 refer to the reviewed 0.2.0 commit. Do not state that hardware or permission gates passed unless their native evidence was actually collected.

## Acceptance gates and explicit exclusions

The plan is complete only when the following functional gates have evidence: discovery is reliable in both directions; the Qt path stays responsive while searching; peer destination remains stable; Hello/pairing survive bounded packet loss; a complete loopback session streams video/audio/input; receive processing is no longer capped at one packet per 10 ms; negotiated bitrate controls both encoder and scheduler; FEC recovers within parity capacity; ABR changes bitrate and FEC; macOS controlled audio is system audio; codec configuration gates decoding; packaged macOS/Windows network behavior is verified; and the release documentation matches the shipped contract.

The following are intentionally excluded because the user permitted skipping security-only repairs: persistent device identity, signed ephemeral transcript integration, trust-store policy, and CSPRNG migration. Existing security tests remain mandatory regression checks, and no task may remove their validation.

## Plan self-review

- Every functional item in the supplemental issue summary maps to Tasks 2–14; the security-only section maps to the explicit exclusion above.
- Every task specifies its interface filtering, error mapping, retry counts, packet budgets, FEC header validation, codec gating, and package rule scope concretely.
- Later tasks consume interfaces defined earlier: DiscoveryClient precedes asynchronous RemoteRuntime; peer locking precedes runtime retries; profile selection precedes scheduler/encoder bitrate; FEC counters precede feedback; feedback precedes ABR acceptance; native package checks precede version/tagging.
- No README sentence is reserved for session status or agent reasoning.
