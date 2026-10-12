<!-- Native game architecture, editing entry points, and focused verification. -->
# 开发说明

工程采用单窗口、40Hz 固定步长玩法及独立渲染。`Game`读取原配置，`Renderer`只读取场景视图；关卡切换释放旧地图 GPU 缓存。音频是独立可选模块，默认两项音量为零，不初始化音频设备。

| 要修改什么 | 修改入口 |
| --- | --- |
| 资源转换 | `tools/prepare-assets.mjs`，格式见 ASSETS.md |
| 地图、距离场、放置／出售、敌人移动、UFO、计分 | `src/game.cpp` |
| 原武器分支、整数护甲、击杀和分裂 | `src/combat.cpp`；规则见 PROJECTILE_RULES.md、EFFECT_RULES.md |
| 炮口、弹道预测、移动投射物 | `src/projectile.cpp` |
| 减速、增幅及 Tesla 拉动 | `src/effects.cpp` |
| 关卡目录、无限公式及地图种子 | `src/catalog.cpp`；规则见 MODES.md |
| 菜单、HUD、建造栏、输入、桌面倍速 | `src/main.cpp` |
| PNG、纹理别名、网格、地形过渡、森林、相机 | `src/renderer.cpp`；矩阵运算在 math.hpp |
| 进度及设置／完整局内状态 | `src/profile.cpp`／`src/session.cpp` |
| 原 Ogg、音量和播放队列 | `src/audio.cpp`；见 AUDIO.md |
| 构建和发布 | CMakeLists.txt、tools/build.ps1、package-release.ps1 |

运行资源相对 EXE 定位，支持中文安装路径。MSVC 使用静态 C++ 运行库；发布不依赖 REA、Java、Node、CMake 或 Android SDK。依赖固定 SDL3 3.4.18、nlohmann/json 3.12.0、stb_vorbis 1.22，版本与摘要在 setup-dependencies.ps1。原解包目录不存在时，构建使用已提交的准备资源。

build.ps1默认重建Release，避免工具保留的旧文件时间戳导致增量编译复用旧对象；确认时间戳正常时可加`-Incremental`。准备资源未变时可加`-SkipAssets`。

## 验证

过程产物写入 `agent/codex`；运行 GUI EXE 时用 `Start-Process -PassThru`并等待进程。示例：

```powershell
$run = Start-Process -FilePath .\release\TowerMadness.exe -ArgumentList '--coverage-test','--output','E:/TowerMadness-Reborn/agent/codex/p7' -WindowStyle Hidden -PassThru
$run.WaitForExit()
$run.ExitCode
```

| 参数 | 验证范围 |
| --- | --- |
| `--self-test` | 原50金币普通操作完成26波胜利、自然失败、堵路／出售／暂停、15／144FPS调度一致 |
| `--coverage-test` | 116个自然开局、四科技、12武器及状态、计分、无限、保存恢复，边界见 COVERAGE.md |
| `--smoke-test` | 本机真实 GL、窗口缩放及坐标往返、场景截图 |
| `--input-test` | 将 self-test 合法操作经 SDL 鼠标／键盘及生产输入处理重放，完成胜利、重开、失败 |
| `--desktop-test` | 生产菜单选择、保存／继续、116地图实际绘制；只使用输出目录的测试 profile |
| `--resource-test` | 420 PNG 和321模型及其材质／实际纹理文件加载 |
| `--audio-test` | 36 Ogg 离线解码，绝不初始化声音设备 |
| `--benchmark-test` | 依赖 self-test 操作记录；完整自然局及最繁忙场景的12000帧绘制、内存样本 |

`input-test`和`benchmark-test`需先在同一输出目录运行`self-test`。验证入口不会读取真实用户 profile；交互模式才使用 LocalAppData。数值夹具不等于自然通关；116个模式的开局／绘制通过不等于全部自然通关。测试通过也不构成安卓动态等价或另一台 Windows 的兼容证明。

保存格式带版本，临时文件关闭后用 Windows 替换现有 profile。局内记录实体、活动波计数、UFO、投射物、状态、时钟及 MT19937；视觉 effect 重新进入后可省略。坏的局内保存由候选 Game 校验，失败不会覆盖当前局。
