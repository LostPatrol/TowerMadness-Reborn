<!-- Native Windows game features, controls, and reproducible developer setup. -->
# TowerMadness Reborn

TowerMadness Zero 1.22 的 Windows x64 原生重建开发版。复用原模型、纹理、地图与科技数据，使用 C++20、SDL3 和 OpenGL 3.3，运行时无需 Android、模拟器或开发工具。

支持 12 类武器、四套科技、地空／护甲／再生／分裂敌人、战役进度、112 个包内附加关卡，以及原无限波生成和沙盒。支持暂停、倍速、升级、出售与保存继续。原作精确动画、阴影、粒子及手机动态对照尚未完成；附加关卡通过单独的桌面资源库开放，其原访问门槛未复现。

## 游玩

解压发布包后双击 `TowerMadness.exe`，保留同目录的 `SDL3.dll` 和 `assets`。支持 Windows 10/11 x64，显卡驱动需支持 OpenGL 3.3。

主菜单选择战役或包内关卡。战役从 PlainPlane 开始，胜利后依次开放下一科技；完成 DividedAttention 后开放当前地图的无限与沙盒模式。包内关卡保留各自配置的科技。选中建造栏中的塔，再点击草地布防，点击已有塔可升级或出售。

| 操作 | 功能 |
| --- | --- |
| 左键 | 操作菜单、选择塔或建造 |
| U / Upgrade | 升级所选塔；有分支时可点具体升级按钮 |
| Delete / Sell | 出售所选塔 |
| N / Send wave | 开始或提前发波；开局后每隔约 20 秒自动尝试发波 |
| 空格 / F / R | 暂停、切换 1×／2×／4×、重开 |
| M / Levels | 打开／关闭关卡菜单，菜单期间暂停仿真 |
| F5 / F9 | 保存当前局／继续保存的局 |
| WASD / 右键拖动 | 移动镜头 |
| 滚轮 / C / Esc | 缩放、重置镜头、退出 |

建造和升级完成前不会攻击。开放地图禁止堵路，固定路径地图可在路径外草地建塔。提前发波奖励在该波最后一个计数对象被击杀后计入分数，不直接增加金币。

默认静音；可在主菜单分别调节音效和音乐音量。进度、设置及当前局保存在 `%LOCALAPPDATA%\TowerMadness-Reborn\profile.json`，每 30 秒及退出时自动保存。读取异常会显示提示，并保留损坏文件备份。

## 开发

安装 Visual Studio 桌面 C++ 工具（含 Windows SDK、CMake）及 Node.js，依赖局部放在 `third_party`：

```powershell
& .\tools\setup-dependencies.ps1
& .\tools\build.ps1
& .\tools\package-release.ps1
```

构建输出在 `release`。仓库包含准备后的运行资源；重新转换时将原 APK 解包资源放在 `agent/codex/apk/assets`，执行 `node tools/prepare-assets.mjs`。原 APK、分析工具和数据库不进入 Git。

修改入口与验证见 [开发说明](docs/DEVELOPMENT.md)，恢复依据见 [规则说明](docs/RULES.md)、[武器](docs/PROJECTILE_RULES.md)、[效果](docs/EFFECT_RULES.md)和[模式](docs/MODES.md)。

原游戏及资源属于 Limbic Software。本项目用于个人学习与内部使用；第三方依赖许可随发布包提供。
