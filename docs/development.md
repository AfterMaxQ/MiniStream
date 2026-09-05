# 开发目录与跨平台发布

Windows 和 macOS 使用同一个 GitHub 仓库、同一份共享代码和同一个版本号。
日常只在 `MiniStream` 主目录工作；平台维护不再各自复制一份业务逻辑。

## 目录职责

```text
MiniStream/
  src/core/          协议、加密、传输、音视频数据和输入格式
  src/app/           共享会话、角色控制和 Qt 显示适配
  src/platform/      平台接口
  src/windows/       Windows 采集、编解码、音频、设备输入
  src/macos/         macOS 采集、编解码、音频、设备输入
  ui/                两个平台共用的 QML 页面
  cmake/             固定依赖和 Qt 部署
  packaging/         Windows 安装器与 macOS DMG 配置
  docs/              使用、开发和发布说明
  tests/             已有测试（桌面构建 preset 默认不执行或构建测试）
  CMakeLists.txt     唯一版本号来源
  CMakePresets.json  两个平台统一的构建入口
  out/               本机生成的内容，不进入 Git
    build/macos/
    build/windows/
    packages/<版本>/
```

本机主目录为 `/Users/chilinlei/Code/MiniStream`。旁边已有的
`MiniStream-v0.2.4-*-maintenance` 是 Git worktree，`*-release` 是历史源码快照，
`*-build*` 是旧构建缓存，`*-release-assets` 是旧安装包，`*-build-tools` 是工具环境。
它们不是需要长期分别维护的平台项目。旧目录和提交保留供回溯，后续改代码使用主目录。
不要直接移动旧 CMake 构建目录：缓存里记录了绝对源码和依赖路径。

机器专属路径放到不提交的 `CMakeUserPresets.json`，或者在配置命令后使用 `-D` 覆盖。
SDK、编译器、依赖缓存、安装包和日志均不提交到源码仓库。

## 分支规则

- `main`：Windows/macOS 共同的集成基线。
- `codex/<修复主题>`：从最新 `main` 创建的短期修改分支，两端的相关修改放在一起。
- 历史 `feat/ministream-v0.1` 和平台维护分支用于追溯，不再作为新功能的起点。
- 合并后用同一个 tag/commit 在两个平台构建，禁止把不同提交的安装包放进同一个版本。

跨机器开始工作前先看 `git status --short`，再 `git fetch origin`。
有本地修改时先保留；不要通过 reset/clean 或强制 checkout 覆盖。

本次串流修复基于 `main` 的 v0.2.4（`9f4ac37`），工作分支为
`codex/streaming-recovery`。旧 v0.1 本地提交和两条平台维护分支仍可追溯。
源码修复最初仅修改代码；后续 v0.2.5 发布工作按用户要求进行构建、打包和安装，
继续跳过测试。具体构建和发布状态以对应 GitHub Release 说明为准。

## 后续需要构建时

以下命令是两个平台的构建入口。

macOS 需要 CMake 3.30+、Ninja、Apple Command Line Tools 或 Xcode、libsodium、
Qt 6.11.2。默认 Qt 路径是 `$HOME/Qt/6.11.2/macos`。
当前 macOS 发布 preset 面向 macOS 15.0+；本机打包使用的 Homebrew 原生依赖也要求 15.0。

```sh
cmake --preset macos
cmake --build --preset macos
cpack --preset macos
```

使用其他 Qt 路径时执行 `cmake --preset macos -DCMAKE_PREFIX_PATH=/你的Qt目录`。
生成的应用是 `out/build/macos/src/macos/MiniStream.app`。
这里按运行构建的 Mac 架构打包，不将 arm64 包称为 universal 包。

Windows 需要 VS 2022 C++ 工具、CMake 3.30+、Qt 6.11.2 `msvc2022_64`、
NVENC SDK 和 NSIS。先设置路径，再运行同样的三步：

```powershell
$env:QTDIR = "C:/Qt/6.11.2/msvc2022_64"
$env:MINISTREAM_NVENC_SDK_ROOT = "C:/SDKs/Video_Codec_SDK"
cmake --preset windows
cmake --build --preset windows
cpack --preset windows
```

Windows 安装包配置会拒绝缺少 NVENC 头文件的环境，避免生成无法共享画面的包。
桌面 presets 设置 `MINISTREAM_BUILD_TESTS=OFF`，不会夹带测试执行。
已有 GitHub CI 的测试流程独立保留，不表示这些修改已经通过 CI。

## GitHub Releases

安装包统一输出到 `out/packages/<版本>/`：

- `MiniStream-<版本>-Windows-x64-Setup.exe`
- `MiniStream-<版本>-macOS-<架构>.dmg`
- CPack 生成的对应 SHA256 文件

准备正式新版本时，只修改根 `CMakeLists.txt` 的 `project(... VERSION ...)`。
合并目标提交后打 `v<版本>` tag；两个平台都从该 tag 构建，再将安装包和 SHA256
上传到同一个 GitHub Release。源码由 GitHub 从 tag 自动提供，无需上传工作文件夹。
不要覆盖 v0.2.4 的现有下载来冒充新的修复版本。

macOS 和 Windows 的产物可以分批补充到同一个 Release；尚未上传的平台应明确标为待构建。
不要把编译或打包成功写成功能测试通过。
