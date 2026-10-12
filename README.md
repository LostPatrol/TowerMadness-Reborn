<!-- Native Windows game scope, controls, and reproducible developer setup. -->
# TowerMadness Reborn

TowerMadness Zero 1.22 的 Windows x64 原生重建原型。复用原游戏模型、纹理和关卡数据，以 C++20、SDL3 和 OpenGL 3.3 运行。

当前可游玩 PlainPlane 的完整 26 波：建造激光塔、升级、出售、自动攻击、飞行与分裂敌人、胜负、暂停、倍速和重开。暂为静音版本；其他武器、关卡选择、存档及完整原作界面尚未实现。

## 游玩

双击发布目录中的 `TowerMadness.exe`，保留同目录的 `SDL3.dll` 和 `assets`。支持 Windows 10/11 x64，需支持 OpenGL 3.3 的显卡驱动。

| 操作 | 功能 |
| --- | --- |
| 左键点击草地 | 建造 Kilowatt Laser，花费 10 金币、占 2×2 导航格 |
| 左键点击已有塔 | 选择塔、查看射程 |
| U / Upgrade 按钮 | 升级所选塔 |
| Delete / Sell 按钮 | 出售所选塔 |
| N / Send wave 按钮 | 开始或提前发送下一波；开局后也会自动发波 |
| 空格 | 暂停 / 继续 |
| F | 切换 1× / 2× / 4× |
| R | 重开 |
| WASD / 右键拖动 | 移动镜头 |
| 滚轮 / C | 缩放 / 重置镜头 |
| Esc | 退出 |

可在发波前布防。建造和升级需等待完成；堵住敌人通往羊圈的道路会被拒绝。

## 开发

安装 Visual Studio 的桌面 C++ 工具（含 Windows SDK、CMake）及 Node.js。项目依赖局部放在 `third_party`：

```powershell
& .\tools\setup-dependencies.ps1
& .\tools\build.ps1
```

构建输出在 `release`。仓库包含已准备的运行资源；从原 APK 重新生成资源时，先将其解包资源放在 `agent/codex/apk/assets`，再执行 `node tools/prepare-assets.mjs`。原 APK、逆向工具与数据库不进入 Git。

结构及修改入口见 [开发说明](docs/DEVELOPMENT.md)，资源格式见 [资源说明](docs/ASSETS.md)，恢复规则与保真边界见 [玩法规则](docs/RULES.md)。

原游戏及资源属于 Limbic Software。本项目用于个人学习与内部使用。
