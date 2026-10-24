<!-- Project purpose, player guide, and local development for the Windows port. -->
# TowerMadness Reborn

[English](README.md) | [简体中文](README.zh-CN.md)

这是一个出于个人情怀的非官方怀旧项目，希望能在 **Windows 上重玩童年时期的 TowerMadness Zero**。项目通过原生 Windows 宿主复用原游戏代码、资源与玩法，运行时不需要 Android 或 CPU 模拟器。

**请尊重原作版权。** TowerMadness 及其原始代码、美术、音乐等内容的版权属于 Limbic Software 及相应权利人。仓库所公开的原作内容仅供方便学习、分享交流编程知识，**请在下载后 24 小时内删除下载的原作内容**。本项目与原作者无隶属关系，也未获得其背书。

## 玩家指南

### 开始游玩

在 Windows 10/11 上，将便携包完整解压到可写文件夹，双击 **TowerMadness.exe**。保留同目录的 `runtime` 文件夹，不要在压缩包预览中直接运行。游玩不需要开发工具。

点击 **New Game**，选择地图后按 **Enter** 开始。鼠标点击空地，在轮盘中选择炮塔；点击已有炮塔可升级或出售。保护羊群，抵挡敌人进攻。

选关时用左右方向键、A/D 或滚轮切换地图：滚轮向上选上一张，向下选下一张。**Map overview** 可查看地图缩略图并筛选。

### 默认操作

| 操作 | 功能 |
| --- | --- |
| 鼠标左键 | 操作菜单，通过炮塔轮盘建造、升级或出售 |
| 鼠标拖动 / 滚轮 | 平移镜头 / 缩放 |
| 右键 | 收起炮塔轮盘 |
| 空格 / Esc | 暂停或继续 / 返回菜单 |
| N | 提前发下一波 |
| W | 升级所选炮塔；有分支时需在轮盘中选择 |
| Q、E / 中键拖动 | 旋转镜头 |
| R | 重置取景 |
| F5 / F9 | 保存 / 读取检查点；读取会替换当前对局 |
| F1 / F10 | 打开图鉴 / PC 设置 |
| F11 或 Alt+Enter | 切换无边框全屏 |
| F6 | 切换 60 / 120 / 不限帧 |

**PC SETTINGS** 可调整按键、镜头灵敏度和界面大小，窗口与 PC 设置会自动记住。音乐和音效在原 **Options** 中调整，默认帧率上限为 60 FPS。

### 存档与问题反馈

- 中途离开前按 **F5** 保存，下次启动点击 **Resume** 继续。手动检查点保留五份旧备份。
- 战斗中关闭窗口会询问：**Yes** 保存退出，**No** 不保存退出，**Cancel** 返回游戏。
- 自动存档默认关闭，可在 **PC SETTINGS** 开启，每两分钟保存一次；用 **Restore separate autosave** 恢复，不覆盖手动存档。
- `data` 文件夹保存进度和设置。更换版本时，先关闭两份游戏并备份，再将旧版 `data` 复制到新版目录，避免覆盖想保留的新进度。
- 启动失败时，确认已完整解压且文件夹可写。反馈问题请附地图名称、操作步骤和截图；重启前保留 `logs/game.log`，也可用 **PC SETTINGS → Export diagnostics** 导出日志。存档请另行备份。

在线排行和 Game Center 不可用；成就页仅显示本地战役进度。首次加载或读档可能短暂停顿。

<!-- player-guide:end -->

## 开发者指南

### 本地准备与构建

当前维护的代码位于 `tools/x86-reuse/`：C++20 Win32 宿主加载原 Android x86 ELF 库，适配平台调用、OpenGL、输入、音频和存储。即使使用 64 位 Windows，也须构建为 **Win32/x86**。

需要 Visual Studio 的“使用 C++ 的桌面开发”组件、Windows SDK、CMake 和 Node.js。换一台电脑使用前，先检查 `build.ps1`、`run.ps1` 中的本机 CMake 和 Node 路径。

原 APK、解包库、本地依赖和生成的运行元数据不进入 Git，**仅克隆仓库不足以构建并运行游戏**。请自行准备原始 x86 APK（`Madness_Zero-1.22.apk`）。首次生成 GS 元数据还需要本地 Capstone 5 x64 DLL：`prepare-gs.ps1` 优先使用 `agent/codex/x86-reuse/capstone.dll`，否则从脚本配置路径中的 Ghidra wheel 提取。常规构建不需要 REA 或 Ghidra 分析数据库。

确认上述本机路径后，在项目根目录使用 PowerShell：

```powershell
# Download the pinned development headers and audio decoder.
& .\tools\setup-dependencies.ps1
# Extract your x86 APK and generate GS metadata using the local Capstone DLL.
& .\tools\x86-reuse\prepare.ps1 -Apk .\Madness_Zero-1.22.apk
# Prepare textures, then generate OpenGL metadata and build the Win32 host.
node .\tools\prepare-assets.mjs
& .\tools\x86-reuse\build.ps1
& .\tools\x86-reuse\run.ps1 -Mode menu
```

已有 OpenGL 元数据时，可用 `build.ps1 -SkipGlSignatures` 跳过重新生成。默认构建目录为 `agent/codex/x86-reuse/current`；`run.ps1 -Mode menu -Silent` 可静音运行。已有构建也可通过 `tools/x86-reuse/play.cmd` 启动。

### 开发入口与打包

`host.cpp` 负责原生加载器与平台适配；`desktop-*.hpp` 负责桌面操作、菜单、设置、存档和图鉴；`native-audio.hpp`、`original-audio.hpp` 负责 Windows 音频。`assets/textures/` 和共享资源准备脚本仍属于当前路线。新增构建依赖时，同步加入构建清单及相关验证脚本。

用以下命令生成新的本地便携目录：

```powershell
& .\tools\x86-reuse\package.ps1
```

产物位于 `release/`，包含可执行文件、运行文件和玩家说明。本地研究文档、agent 文件及已停止维护的重建路线不纳入仓库。
