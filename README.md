<!-- Project scope, current native route, and local developer startup. -->
# TowerMadness Reborn

在 Windows 10/11 上移植 TowerMadness Zero，供个人学习和怀旧，不进行分发。

当前路线为 **原 x86 核心复用**：Win32 C++ 宿主直接执行原 APK 的 x86 机器码，适配 Windows、OpenGL、输入和本地存储，保留原游戏逻辑、资源及菜单。运行时无需 Android 或 CPU 模拟器。默认静音。

目前是可继续开发的原型，完整移植尚未完成。接续开发只需先读 [原核心开发说明](docs/X86_CORE_REUSE.md)；修复要点见 [基线修正摘要](docs/BASELINE_AUDIT.md)。

## 本地运行

需要 Visual Studio 桌面 C++ 工具（含 Windows SDK、CMake）、Node.js，以及本机已有的原包、标准化纹理、GS/GL 元数据和 `third_party/json.hpp`。原游戏文件和开发工具不进入 Git；仅克隆仓库不足以运行。

```powershell
& .\tools\x86-reuse\build.ps1 -SkipGlSignatures
& .\tools\x86-reuse\run.ps1 -Mode menu
```

已构建后也可双击 `tools/x86-reuse/play.cmd`。鼠标操作原菜单和建塔轮盘；空格发波，Esc 返回菜单，F5/F9 保存和读取宿主测试存档。固定 480×800 竖屏。开发数据与旧重建版用户存档隔离。

## 旧重建版

`src/` 是已停止常规推进的 x64/SDL3 重建参考；其旧构建和发布包已清理，可按需重新构建。该路线的功能和验收不计入 x86 移植。保留标签 `reconstruction-reference-20261014` 及 [静态规格参考](docs/reference/README.md)。

原游戏及资源属于 Limbic Software。本项目仅供个人学习与内部使用。
