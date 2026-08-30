# MiniStream

MiniStream 是局域网游戏串流程序。Windows 和 macOS 使用同一个应用入口，
启动后按当前会话选择“Allow control”或“Remote control”。

## 架构

```text
MiniStream (Qt Quick / QML)
          │
          ▼
RoleController ── 顶部角色切换、发现、配对、输入和清理
     ┌───────────────┴───────────────┐
     ▼                               ▼
ControlledRuntime                 RemoteRuntime
     │                               │
     ├─ Windows: DXGI/D3D11/NVENC   ├─ Windows: MF/D3D11
     │  WASAPI loopback/ViGEm        │  WASAPI output
     │                               │
     └─ macOS: CGDisplayStream      └─ macOS: VideoToolbox/Metal
       VideoToolbox/ScreenCaptureKit  CoreAudio/SDL3
       Accessibility input
                    │
                    ▼
MiniStream Core: versioned UDP, pairing, authenticated encryption,
FEC, bounded packet scheduling, Opus audio, input and telemetry
```

视频、音频和输入都通过同一条加密 UDP 会话传输。控制端只接收主动广播的
设备；被控制端未点击 **Allow control** 前不会出现在发现列表中。视频帧在
Windows 保持 D3D11 纹理，在 macOS 保持 `CVPixelBuffer`/Metal 纹理，QML
控件与视频画面位于同一个窗口。

## 使用

1. 在准备共享画面的设备上打开 **Allow control**，确认 Video、Audio、Input
   和 Network 状态正常，然后点击 **Allow control**。
2. 在另一台设备切换到 **Remote control**，点击 **Find devices**，从列表中
   选择设备。列表显示系统类型、设备名和视频/音频参数。
3. 点击 **Connect**。两台设备会显示同一个六位配对码；只有确认两边代码
   一致后才会开始串流。
4. 串流页面点击 **Control remote** 将键盘、鼠标和可用手柄发送到远端；
   点击 **Use this device** 立即恢复本机输入。断开连接、切换顶部角色、
   关闭窗口和配对取消也会释放输入。

输入捕获只在 MiniStream 窗口内生效，不安装全局键盘或鼠标钩子。为避免和
游戏菜单快捷键冲突，远程输入开启时 Esc 和 F11 会发送到远端；退出控制请
点击 **Use this device**。本机模式下：

- Windows/Linux：`Ctrl+Alt+R` 切换远程输入；
- macOS：`⌘+Option+R` 切换远程输入；
- `F11` 切换全屏，非远程输入模式下 `Esc` 退出全屏。

## 从源码构建

### Windows

开发构建需要 Visual Studio 2022 C++ 工具、CMake 3.30 或更高版本、Qt
6.11.2 `msvc2022_64`、NVIDIA 驱动和 NVIDIA Video Codec SDK 头文件。
将 Qt 安装目录设为 `QTDIR`，将包含 `Interface/nvEncodeAPI.h` 的 SDK 目录设
为 `MINISTREAM_NVENC_SDK_ROOT`：

```powershell
$env:QTDIR = "<Qt 6.11.2>/msvc2022_64"
$env:MINISTREAM_NVENC_SDK_ROOT = "<Video Codec SDK 13.1>"

cmake -S . -B build-ui -G "Visual Studio 17 2022" -A x64 `
  -DMINISTREAM_BUILD_UI=ON `
  -DCMAKE_PREFIX_PATH="$env:QTDIR" `
  -DMINISTREAM_NVENC_SDK_ROOT="$env:MINISTREAM_NVENC_SDK_ROOT"
cmake --build build-ui --config Debug --parallel 4
ctest --test-dir build-ui -C Debug --output-on-failure
```

没有 ViGEmBus 时仍可使用键盘和鼠标。需要手柄时安装 ViGEmBus；发布安装
器会携带官方安装程序并在驱动缺失时通过 Windows UAC 提示安装。安装器还会
询问是否允许 `ministream.exe` 在 Private Network 接收 UDP；该规则覆盖
47990 discovery 和动态 session 端口，不会开放 Public Network。拒绝后需在
Windows Defender Firewall 中为程序允许 Private Network 入站流量，才能发现
或接受连接。

### macOS

需要 Xcode、CMake 3.30 或更高版本、Qt 6.11.2 macOS 套件和 `libsodium`。
例如先执行 `brew install libsodium`，然后在 Mac 上构建：

```sh
cmake -S . -B build-macos -G Xcode \
  -DMINISTREAM_BUILD_UI=ON \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.2/macos"
cmake --build build-macos --config Release
ctest --test-dir build-macos -C Release --output-on-failure
```

首次使用“Allow control”时，macOS 可能要求授予本地网络、屏幕录制和辅助功能
权限。系统音频通过 ScreenCaptureKit 的屏幕录制授权获取，不会把麦克风当作
游戏音频。MiniStream 会在页面显示对应状态，可通过 **Open System Settings**
打开系统设置。拒绝权限不会启用软件视频或麦克风回退。

依赖库（Asio、SDL3、Opus、Leopard-RS 等）由 CMake 按
`cmake/Dependencies.cmake` 中的版本获取；SDK、驱动和构建目录不属于仓库。

## 发布包

发布包包含统一的 `ministream` 应用和 Qt/QML 运行时，最终用户不需要安装
Qt、CMake、SDL、Opus、libsodium、Leopard-RS 或编译器。

### Windows 安装器

在已配置 Qt 和 NVIDIA SDK 的 Windows 环境中：

```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 `
  -DMINISTREAM_BUILD_TESTS=OFF `
  -DMINISTREAM_BUILD_TOOLS=OFF `
  -DMINISTREAM_BUILD_UI=ON `
  -DMINISTREAM_ENABLE_PACKAGING=ON `
  -DCMAKE_PREFIX_PATH="$env:QTDIR" `
  -DMINISTREAM_NVENC_SDK_ROOT="$env:MINISTREAM_NVENC_SDK_ROOT"
cmake --build build-release --config Release
cpack --config build-release/CPackConfig.cmake -C Release
```

输出文件名为 `MiniStream-Setup.exe`。安装器包含 Qt/QML、MSVC runtime、
libsodium 和 ViGEmBus 安装程序；NVIDIA 显卡驱动仍由系统提供，不随包安装。

### macOS DMG

在 Mac 上执行：

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

输出 `MiniStream.dmg`。打开 DMG 后将 `MiniStream.app` 拖到
`Applications`，再从 Applications 启动。

## 当前版本边界

- 仅支持同一局域网内的发现和连接，不包含账号、云服务、NAT 穿透或多控制器。
- 编解码器和 HDR 状态由本机硬件能力决定；4K60、HEVC Main10/HDR10 和长时间
  串流需要在目标 Windows/macOS 设备及显示器上单独验收。
- macOS 的屏幕录制、辅助功能和音频权限由系统控制；Windows 手柄输入需要
  ViGEmBus，键盘鼠标不依赖该驱动。

发布检查项见 [`docs/release/v0.2-functional-reliability-checklist.md`](docs/release/v0.2-functional-reliability-checklist.md)。
