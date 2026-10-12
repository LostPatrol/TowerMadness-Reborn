<!-- Native sample architecture, editing entry points, and focused verification. -->
# 开发说明

工程采用单窗口、固定步长玩法、独立渲染的结构。`Game` 读取原配置，在40Hz更新状态；`Renderer` 只读取实体视图，不修改金币、路径或伤害。SDL负责桌面输入与窗口，不初始化音频。

| 要修改什么 | 修改入口 |
| --- | --- |
| 地图 / 模型 / 配置转换 | `tools/prepare-assets.mjs`；格式见ASSETS.md |
| 寻路、摆塔、升级、售塔、波次、beam伤害 | `src/game.cpp`，证据与差异见RULES.md |
| 输入、按钮、HUD、仿真倍速 | `src/main.cpp` 的gameInput、makeScene及主循环 |
| PNG / 网格 / 材质、相机、拾取 | `src/renderer.cpp`；投影运算在math.hpp |
| 编译 / 发布依赖 | `CMakeLists.txt`、`tools/build.ps1`、setup-dependencies.ps1 |

运行资源相对EXE定位。MSVC使用静态C++运行库，发布只需EXE、SDL3.dll和assets。SDL3固定3.4.18，nlohmann/json固定3.12.0，下载摘要在setup-dependencies.ps1；无需Android SDK或模拟器。若找不到原解包目录，构建直接使用已提交的准备资源。

## 验证

验证文件属于过程产物，统一输出至agent/codex/task01，不混入运行资源。可运行：

```powershell
& .\release\TowerMadness.exe --self-test --output E:/TowerMadness-Reborn/agent/codex/task01
& .\release\TowerMadness.exe --smoke-test --output E:/TowerMadness-Reborn/agent/codex/task01
& .\release\TowerMadness.exe --input-test --output E:/TowerMadness-Reborn/agent/codex/task01
```

Windows GUI程序由PowerShell启动时可能不阻塞；自动验证应使用Start-Process -PassThru再WaitForExit。self-test使用普通建塔/升级指令验证金币、堵路、暂停、售塔、自然失败及26波胜利，对比15/144FPS调度。smoke-test记录真实GL驱动、截图和窗口缩放后坐标往返。input-test依赖同目录simulation-checks.json，把记录的合法操作通过SDL鼠标/键盘事件及实际桌面输入处理重放，再验证重开和自然失败。

这些结果证明本机Windows原型可运行。它们不是Android真机等价测试；随机序列、相机、动画、阴影和界面仍有已知差异。需要扩展武器时，先恢复该武器算法，不复用beam算法冒充其他类型。
