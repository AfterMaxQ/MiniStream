# Windows 同步、构建、打包和上传提示词

将以下内容交给 Windows 上的代码助手执行。

```text
请直接完成 MiniStream 的 Windows 源码同步、Release 构建、NSIS 打包和 GitHub Release 上传。不要只给我操作说明，不使用 subagent，不运行单元测试、CTest、UI 测试或实机串流测试；构建、依赖部署、安装包生成和产物核对需要完成。

仓库：https://github.com/AfterMaxQ/MiniStream
本轮目标版本：v0.2.5
修复 PR：https://github.com/AfterMaxQ/MiniStream/pull/2
发布页面：https://github.com/AfterMaxQ/MiniStream/releases/tag/v0.2.5
Mac 端已经处理串流黑屏、关键帧恢复、自动发现、键鼠与手柄逻辑、声音起播和队列恢复，并使用共同的 CMakePresets.json。Windows 包必须对应 v0.2.5 tag 的同一个提交，不能使用旧平台分支的不同源码冒充同一版本。

1. 找到本机 MiniStream 仓库，先检查 git status --short、git remote -v、git branch -vv、git worktree list，再执行 git fetch origin --tags。核对 PR #2、origin/main 和 v0.2.5 的当前状态，不凭旧上下文判断是否同步。
2. 保留本地未提交文件及独有提交，不使用 reset --hard、git clean、强制 checkout 或 force push。干净且可快进的 main 使用 git pull --ff-only origin main；存在独有工作时保留原目录，在独立 worktree 中从 v0.2.5 开始发布构建。比较旧 Windows 维护分支的独有修改，已经包含的修复不要重复 cherry-pick。不要将旧 feat/ministream-v0.1 当作最新基线。
3. 检查 VS 2022 x64 C++ 工具、CMake 3.30+、Qt 6.11.2 msvc2022_64、NVENC SDK 的 Interface/nvEncodeAPI.h、NSIS，以及 gh 登录状态。查找现有 SDK 并设置真实 QTDIR、MINISTREAM_NVENC_SDK_ROOT；缺少可直接安装的构建工具就补齐。不要降级 Qt，不要关闭 NVENC 来绕过配置错误。
4. 阅读 docs/development.md 和 CMakePresets.json，在确认工作目录是目标 tag 且干净后执行：
   cmake --preset windows
   cmake --build --preset windows
   cpack --preset windows
   保持 MINISTREAM_BUILD_TESTS=OFF、MINISTREAM_BUILD_TOOLS=OFF、MINISTREAM_BUILD_UI=ON、MINISTREAM_ENABLE_PACKAGING=ON。不运行任何测试。
5. 构建和打包失败时，直接定位并修复必要的错误，不做无关重构。机器路径放 CMakeUserPresets.json，不提交 SDK、日志、缓存或安装包。如果必须修改已发布 tag 的源码，保留 v0.2.5 不动：在 codex/ 分支完成修复、合并最新 main，再准备下一个未使用补丁版本；新包必须使用新的 tag 和 Release，不能把不同源码的包上传成 v0.2.5。发布说明明确该新版本的 macOS 包是否仍待构建。涉及新提交或合并时用 [skip ci] 跳过测试 CI，不能删除现有 CI 工作流。
6. 产物应为 out/packages/0.2.5/MiniStream-0.2.5-Windows-x64-Setup.exe 及 CPack 生成的 SHA256 文件。核对版本、x64 架构、Qt/QML 与 libsodium 运行库部署、ViGEmBus 安装器及防火墙配置已包含。核对实际 SHA256；不要启动应用来做功能测试。
7. 将 Windows 安装包和 SHA256 上传到上述已存在的 v0.2.5 GitHub Release。保留该 Release 的 macOS DMG、SHA256、原有说明和预发布状态，不覆盖或删除任何既有附件。如果存在同名附件，先比较 hash；相同则跳过，不同则调查来源，不使用 --clobber 直接覆盖。可使用 gh release upload v0.2.5 <exe绝对路径> <sha256绝对路径> --repo AfterMaxQ/MiniStream。
8. 更新 Release 中 Windows 的待构建状态并列出下载文件，保留 Mac 的签名与未测试说明。最后报告实际提交 SHA、tag、构建和打包结果、完整本地产物路径、SHA256、GitHub 下载链接，以及实际遇到的限制。明确写出“未运行测试”，不要宣称黑屏或远控已经通过实机验证。本次不需要安装或启动 Windows 应用。
```
