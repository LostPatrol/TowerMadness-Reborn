<!-- Native game architecture, editing entry points, and focused verification. -->
# 开发说明

工程采用单窗口、40Hz 固定步长玩法及独立渲染。`Game`读取原配置，`Renderer`只读取场景视图；关卡切换释放旧地图 GPU 缓存。音频是独立可选模块，默认两项音量为零，不初始化音频设备。

`Desktop`在现有main.cpp中维护浏览、建造、详情、暂停/重开确认与结算状态。按钮和快捷键共用`desktopInput`，镜头事件统一经`desktopEvent`经过UI后处理。`Game::*BlockReason`返回空字符串表示可执行，实际命令与预览使用同一规则；试放只复制导航格，不能修改资金、状态或随机数。`sellRefund`提供真正的排期退款，不能用投资额替代。

战场矩形由`paletteTop`和详情栏宽度确定，`Renderer::setBoardViewport`使绘制、投影和拾取共用该区域。`SceneView`提供面板、文字、图标、图片、原模型及世界精灵。交互循环经`Game::advanceEffects`提供单调realtime，短暂Effect/Beam独立于暂停/倍速到期；离线检查缺省使用确定性Game毫秒。移动投射物、塔建造和敌人动作保留仿真时间。Beam保存发射炮口、姿态、长度及塔类型，Effect保存出生元数据；短暂事件不进入存档。公式及适配差异见 COMBAT_VISUALS.md。

相机、材质与塔表现见[CAMERA](CAMERA.md)、[TOWER_VISUALS](TOWER_VISUALS.md)。原视角为地图中心、eyeY+5/height50/FOV45；`resetCamera(false)`用于原默认姿态，普通桌面入口仍自动fit。原材质缺省unlit texture×RGBA，point颜色按byte截断。塔build/sell及升级前后半模型、directionalshadow、Tesla附件、boost圈由`makeScene`组装；原selection及选中range已接入，预览range与升级range反馈仍有适配差异。完整nativeAimSmooth战斗gate、初始随机yaw/idle、诱饵屏幕aura、条件核爆闪光、羊完整动作与Menu world仍未闭合。

外围流程见 FRONTEND.md。关卡浏览使用独立Game；Renderer::mapPreview以同一场景路径离屏绘制原地图，恢复当前地图、镜头和GL状态，最多缓存16张缩略图。生产退出统一经requestExit，保存失败不能直接丢弃当前局。

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
| `--coverage-test` | 24项CPU规则/保存检查及116个自然开局、四科技、12武器，含realtime不改变战斗/RNG与暂停到期；边界见 COVERAGE.md |
| `--smoke-test` | 本机真实GL、原默认相机投影、unlit/point颜色/塔表现、暂停realtime、窗口缩放与坐标往返及截图 |
| `--input-test` | 将 self-test 合法操作经 SDL 鼠标／键盘及生产输入处理重放，完成胜利、重开、失败 |
| `--desktop-test` | 生产菜单选择、保存／继续、116地图实际绘制；只使用输出目录的测试 profile |
| `--visual-test` | 12类基础武器及Nuke、H Bomb、Mine III、Tesla、Rail VI、Energy Pulse的18个原沙盒真实事件和GL截图；炮弹/导弹等待真实命中 |
| `--resource-test` | 420 PNG 和321模型及其材质／实际纹理文件加载 |
| `--audio-test` | 36 Ogg 离线解码，绝不初始化声音设备 |
| `--benchmark-test` | 依赖 self-test 操作记录；完整自然局及最繁忙场景的12000帧绘制、内存样本 |

`input-test`和`benchmark-test`需先在同一输出目录运行`self-test`。验证入口不会读取真实用户 profile；交互模式才使用 LocalAppData。数值夹具不等于自然通关；116个模式的开局／绘制通过不等于全部自然通关。测试通过也不构成安卓动态等价或另一台 Windows 的兼容证明。

`visual-test`通过公开Game API在原10000金币沙盒合法建塔、发波，等待实际发射/命中；不改资金、HP、波参数或战斗RNG。高级塔直接由API建造，不能将结果描述为UI升级路径验收。截图使用生产desktopScene和Renderer，逐类JSON明确记录时刻与范围。

保存格式带版本，临时文件关闭后用 Windows 替换现有 profile。局内记录实体、活动波计数、UFO、投射物、状态、时钟及 MT19937；视觉 effect 重新进入后可省略。可选`towers[].appearance`保留建造/出售起点及升级旧类型，旧存档或坏视觉字段降级，保留可用核心状态。坏的局内保存由候选 Game 校验，失败不会覆盖当前局。

投射物视觉规格见PROJECTILE_VISUALS.md。Projectile的trailSample/trailThreshold由真实移动更新推进；Effect保存A/B，SceneView.quads只绘制原世界几何，不能用相机朝向重新生成trail。可选session.projectiles[].trail保存采样点/阈值；旧或损坏视觉字段从当前位置重新开始，不拒绝有效核心局。影像随机来自math.hpp的独立hash，绝不能消费战斗MT。

开发期原作对照见[Android环境](ANDROID_REFERENCE.md)与[双APK基线](APK_BASELINES.md)。模拟器和下载包只放在agent/codex，发布EXE不依赖它们。

敌人动画与死亡规格见[ANIMATION](ANIMATION.md)：Game维护原时间帧、平滑heading、独立slot及冻结corpse；main.cpp的deathTransform和enemyShadow负责原矩阵组装。可选session.animation保存这些状态，不能让纯表现使用战斗RNG或加入活动敌人/胜负计数。炮塔俯仰必须复用Game::towerElevation，避免模型与攻击炮口分离。
