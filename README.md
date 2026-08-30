# MiniStream

MiniStream is a LAN game-streaming application. The Windows program is the host and the macOS program is the client. Shared C++20 code handles the wire protocol, UDP transport, FEC, congestion response, pairing, session encryption, Opus audio, controller packets, and telemetry. Platform code handles capture, audio devices, virtual input, and the desktop shell.

## Architecture

```text
Windows Host
  DXGI Desktop Duplication -> D3D11 texture -> NVENC -> authenticated UDP
  WASAPI loopback         -> Opus audio packets
  ViGEmBus                <- controller packets
  discovery / pairing / encrypted UDP session
                              |
                           LAN / Wi-Fi
                              |
macOS Client
  discovery / pairing / encrypted UDP session
  SDL3 controller         -> input packets
  video packets            -> VideoToolbox pixel buffers
  audio packets            -> CoreAudio output

Shared core
  versioned packets, bounded queues, reassembly, Leopard FEC,
  priority scheduling, adaptive bitrate policy, clock sync,
  session lifecycle, rolling telemetry, and libsodium security
```

The current `0.1.0` checkout contains the shared authenticated media transport, Windows DXGI/WASAPI backends, runtime NVENC integration, ViGEmClient integration, macOS VideoToolbox/CoreAudio backends, LAN discovery, six-digit pairing, and the Qt Quick host/client shell. The Qt shell is deliberately separate from media and network hot paths.

A successful build proves the protocol, capture, codec, audio, controller, pairing, and packaging contracts. The release checklist still requires target-machine validation of the complete 4K60 HDR session.

## Repository layout

```text
src/core/       shared protocol, transport, security, audio, input, session, telemetry
src/windows/    Windows host capabilities, DXGI capture, WASAPI loopback, ViGEmBus input
src/macos/      macOS client entry points and SDL3 controller backend
ui/             Qt Quick pages, controls, and theme tokens
cmake/          pinned dependencies, warnings, Qt deployment helpers
packaging/      CPack/NSIS and macOS DragNDrop configuration
tests/          Catch2 unit and loopback tests
tools/          netprobe and UI copy checker
docs/           design, implementation plans, and release criteria
```

## Runtime flow

1. Install the Windows host and the macOS client.
2. Start the host. The first page reports Video, Audio, Controller, and Network readiness.
3. On the client choose **Find PCs**, select the Windows PC, and choose **Connect**.
4. Both devices show the same six-digit pairing code. Confirm only when the codes match on both screens.
5. Choose **Control remote** when input should be sent to the PC. Choose **Use this Mac** to release keyboard, mouse, and controller routing back to the Mac. **Esc** always releases remote input; it also leaves fullscreen. `Ctrl+Shift+F12` toggles the same input mode.

MiniStream is LAN-only in this version. It does not provide Internet/NAT traversal, accounts, cloud services, or multi-controller support.

## Building from source

### Windows host

Requirements for a development build:

- Windows 10 or newer with Visual Studio 2022 C++ tools
- CMake 3.30 or newer
- a recent NVIDIA driver for the NVENC capability check
- ViGEmBus for virtual-controller operation

Configure and build the native host and tests:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Build the Qt shell by pointing CMake at an installed Qt 6.11.2 tree:

```powershell
cmake -S . -B build-ui -G "Visual Studio 17 2022" -A x64 `
  -DMINISTREAM_BUILD_UI=ON `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.2\msvc2022_64" `
  -DMINISTREAM_NVENC_SDK_ROOT="C:\SDKs\NVIDIA\Video_Codec_SDK_13.1"
cmake --build build-ui --config Release
```

The client target is enabled when the same project is configured on macOS. SDL3 is fetched by CMake for the macOS input backend. Qt is a build-time dependency; it is copied into the application bundle by the deployment step described below.

The CMake options are:

| Option | Default | Purpose |
| --- | --- | --- |
| `MINISTREAM_BUILD_TESTS` | `ON` | Build Catch2 tests |
| `MINISTREAM_BUILD_TOOLS` | `ON` | Build `netprobe` |
| `MINISTREAM_BUILD_UI` | `OFF` | Build the Qt Quick desktop shell |
| `MINISTREAM_ENABLE_PACKAGING` | `OFF` | Enable CPack installer generation |
| `MINISTREAM_NVENC_SDK_ROOT` | empty | NVIDIA Video Codec SDK headers used by the Windows NVENC integration |

Run the UI copy check before committing UI text:

```powershell
python tools/check_ui_copy.py ui
```

## Release packages

Release packaging is target-specific. Build the Windows installer on Windows and the DMG on macOS; the repository does not require end users to install CMake, Qt, SDL, Opus, libsodium, Leopard, or Visual Studio.

### Windows installer

Configure a clean release tree with Qt available to CMake:

```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 `
  -DMINISTREAM_BUILD_TESTS=OFF `
  -DMINISTREAM_BUILD_TOOLS=OFF `
  -DMINISTREAM_BUILD_UI=ON `
  -DMINISTREAM_ENABLE_PACKAGING=ON `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.2\msvc2022_64"
cmake --build build-release --config Release
cpack --config build-release/CPackConfig.cmake -C Release
```

The resulting `MiniStream-0.1.0-Windows-AMD64.exe` contains the host, libsodium, the MSVC runtime files, and the ViGEmBus installer. During installation it checks the `ViGEmBus` service. If the driver is absent, the installer asks whether to run the bundled driver installer; Windows then presents its normal UAC prompt. The MiniStream installer does not require the user to download a driver or DLL separately.

The NVIDIA driver remains a machine prerequisite. The installer reports whether the NVIDIA adapter and `nvEncodeAPI64.dll` are available; it does not bundle a graphics driver.

### macOS DMG

Run these commands on the Mac that will produce the application bundle:

```sh
cmake -S . -B build-release -G Xcode \
  -DMINISTREAM_BUILD_TESTS=OFF \
  -DMINISTREAM_BUILD_TOOLS=OFF \
  -DMINISTREAM_BUILD_UI=ON \
  -DMINISTREAM_ENABLE_PACKAGING=ON \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.2/macos"
cmake --build build-release --config Release
cpack --config build-release/CPackConfig.cmake -C Release
```

Open the generated `MiniStream-0.1.0-Darwin.dmg` and drag `MiniStream.app` to `Applications`. Qt deployment is generated from the target executable so the installed app carries its Qt/QML runtime.

## Pinned dependencies

The versions and source pins are kept in [`cmake/Dependencies.cmake`](cmake/Dependencies.cmake): Qt 6.11.2, Catch2 3.15.3, standalone Asio 1.38.2, SDL 3.4.14, Opus 1.5.2, libsodium 1.0.20, Leopard-RS at commit `6e5725ebdf9da4370b0bcc4f70fa8eb66f4e6198`, and ViGEmClient at commit `9e91a124d179bf26a878a952153042ac871da243`. The Windows installer downloads the signed ViGEmBus 1.22.0 setup during the packaging configure step and embeds it in the generated installer.

## Release status

The release gate is tracked in [`docs/release/v0.1-alpha-checklist.md`](docs/release/v0.1-alpha-checklist.md). The current Windows build and shared test suite are usable for development and integration work. A release claiming end-to-end 4K60 HDR streaming still requires target Windows and macOS hardware validation, including hardware encode/decode, Metal presentation, audio output, controller rumble, and long-session stability.
