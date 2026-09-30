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

## 画质与网络

连接前通过 **Quality** 选择档位，默认使用 Smooth。两端同时支持 HEVC 时，
1080p 优先使用 HEVC；否则使用 H.264。1440p 和 4K 需要两端支持 HEVC。

| 档位 | 分辨率与目标帧率 | 起始码率 | 自适应范围 |
|---|---|---|---|
| Smooth | 1920×1080，60 fps | 8 Mbps | 2–20 Mbps |
| Sharp | 2560×1440，60 fps | 16 Mbps | 4–40 Mbps |
| Ultra | 3840×2160，60 fps | 24 Mbps | 8–60 Mbps |

码率根据队列积压、丢包和恢复情况调整，FEC 与协议开销额外占用带宽。
Wi-Fi 串流先使用 Smooth；网络稳定后可选择更高分辨率。实际帧率取决于
桌面更新、设备性能和网络吞吐。静止桌面只发送变化的画面。

HDR 使用 HEVC Main10，并要求两端及被共享显示器支持 HDR。

## 使用

1. 在准备共享画面的设备上打开 **Allow control**，确认 Video、Audio、Input
   和 Network 状态正常，然后点击 **Allow control**。
2. 在另一台设备切换到 **Remote control**，设备列表每 3 秒自动刷新，也可点击 **Find devices** 立即查找，从列表中
   选择设备。列表显示系统类型、设备名和视频/音频参数。
3. 点击 **Connect**。两台设备会显示同一个六位配对码；只有确认两边代码
   一致后才会开始串流。
4. 串流页面点击 **Control remote** 将键盘、鼠标和可用手柄发送到远端；
   点击 **Use this device** 立即恢复本机输入。窗口失去焦点、断开连接、切换
   顶部角色、关闭窗口和配对取消也会释放输入。

输入捕获只在 MiniStream 窗口内生效，不安装全局键盘或鼠标钩子。为避免和
游戏菜单快捷键冲突，远程输入开启时 Esc 和 F11 会发送到远端；退出控制请
点击 **Use this device**，也可以使用始终由本机处理的保留退出组合键：

- Windows：`Ctrl+Alt+R` 进入远程输入，`Ctrl+Alt+Shift+R` 退出；
- macOS：`⌘+Option+R` 进入远程输入，`⌘+Option+Shift+R` 退出；
- `F11` 切换全屏，非远程输入模式下 `Esc` 退出全屏。

两端必须使用兼容的协议版本。升级时请同时更新控制端和被控制端。

## 从源码构建

Windows 和 macOS 使用 [CMake presets](CMakePresets.json)，构建输出位于
`out/build/windows/` 和 `out/build/macos/`。

### Windows

开发构建需要 Visual Studio 2022 C++ 工具、CMake 3.30 或更高版本、Qt
6.11.2 `msvc2022_64`、NVIDIA 驱动和 NVIDIA Video Codec SDK 头文件。
将 Qt 安装目录设为 `QTDIR`，将包含 `Interface/nvEncodeAPI.h` 的 SDK 目录设
为 `MINISTREAM_NVENC_SDK_ROOT`：

```powershell
$env:QTDIR = "<Qt 6.11.2>/msvc2022_64"
$env:MINISTREAM_NVENC_SDK_ROOT = "<Video Codec SDK 13.1>"

cmake --preset windows
cmake --build --preset windows
cpack --preset windows
```

没有 ViGEmBus 时仍可使用键盘和鼠标。需要手柄时安装 ViGEmBus；发布安装
器会携带官方安装程序并在驱动缺失时通过 Windows UAC 提示安装。安装器还会
询问是否允许 `ministream.exe` 在 Private Network 接收 UDP；该规则覆盖
47990 discovery 和动态 session 端口，不会开放 Public Network。拒绝后需在
Windows Defender Firewall 中为程序允许 Private Network 入站流量，才能发现
或接受连接。

Windows 被控制端需要支持 NVENC 的 NVIDIA 显卡。SDR 与 HDR 桌面通过
D3D11 转换后交给硬件编码器；HDR 能力会在开始共享前进行检测。

### macOS

需要 macOS 15 或更高版本、Xcode Command Line Tools、CMake 3.30 或更高版本、
Ninja、Qt 6.11.2 macOS 套件和 `libsodium`。Qt 默认安装目录为
`$HOME/Qt/6.11.2/macos`，可用 `-DCMAKE_PREFIX_PATH` 覆盖。

```sh
brew install cmake ninja libsodium
cmake --preset macos
cmake --build --preset macos
cpack --preset macos
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

Windows 输出到 `out/packages/<版本>/`，文件名为 `MiniStream-<版本>-Windows-x64-Setup.exe`。安装器包含 Qt/QML、MSVC runtime、
libsodium 和 ViGEmBus 安装程序；NVIDIA 显卡驱动仍由系统提供，不随包安装。

macOS 输出到 `out/packages/<版本>/MiniStream-<版本>-macOS-<架构>.dmg`。打开 DMG 后将 `MiniStream.app` 拖到
`Applications`，再从 Applications 启动。

## 当前版本边界

- 仅支持同一局域网内的发现和连接，不包含账号、云服务、NAT 穿透或多控制器。
- 视频使用 H.264/HEVC 硬件编码与解码，不使用软件视频回退。
- 串流页面支持 Desktop 和 Game 鼠标模式；Game 模式锁定光标并发送相对移动。
- macOS 的屏幕录制、辅助功能和音频权限由系统控制；Windows 手柄输入需要
  ViGEmBus，键盘鼠标不依赖该驱动。

从终端启动应用时，每秒输出编码帧率、解码提交帧率、处理耗时、发送队列和
丢包统计。解码提交帧率表示交给解码器的帧数，屏幕显示帧率取决于解码和渲染。
