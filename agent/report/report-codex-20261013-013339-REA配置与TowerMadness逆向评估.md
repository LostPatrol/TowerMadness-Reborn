<!-- Evidence-based setup and feasibility report; static findings are not runtime validation. -->
# REA 配置与 TowerMadness 逆向评估

执行者：codex；时间：2026-10-13 01:33:39，Asia/Shanghai。

## 结论与任务范围

已配置 REA 并完成实际 APK 分析、独立 MCP 调用和 Ghidra 原生函数抽样。资源复用条件好；修改数据驱动数值难度低；局部算法恢复难度中等；复用资源并重建 Windows 版本整体为中高难度。把原 ARM 二进制及 Apportable 框架直接搬到 Windows，难度高，当前不建议作为首选路线。

这次任务是配置与难度评估，未制作 Windows 可玩版本、未修改原 APK、未完成 APK 重打包/签名/安装。保留源码重建与运行对照的后续工作，不能把静态分析报告算作移植完成。

## 已配置并验证的工具

| 项目 | 当前状态 |
| --- | --- |
| REA | 6.4.0，安装于 agent/codex/tools/rea，依赖由 package-lock 固定 |
| Codex MCP | 已写入 C:/Users/LostPatrol_2/.codex/config.toml；保留其他配置并生成 config.toml.rea.backup |
| REA 技能 | 安装到 C:/Users/LostPatrol_2/.agents/skills/reverse-engineer-anything，identity aligned，版本 37 |
| JADX 引擎 | jadx-headless-mcp 0.7.1；下载 SHA-256 与 REA 审计值一致 |
| 普通 JADX | 1.5.6，产出 Java 壳层代码；报告 7 处错误，不能宣称全量无损反编译 |
| Ghidra | 12.1.4，直接 headless 分析 ARM ELF 成功；保存数据库 |
| 本机运行时 | Node 22.23.1、npm 10.9.8、Temurin JDK 21.0.11，均复用既有安装 |
| Windows 开发工具 | Visual Studio Community 2026、MSVC 14.51.36231、Windows SDK 10.0.26100.0 已发现；本次未验证游戏构建 |

REA scoped doctor 的 Codex 注册、Ghidra provider、技能检查通过。独立 MCP 客户端真实完成连接，服务器版本 6.4.0、工具数 170；调用 inspect_android_method 成功返回 GLSurfaceRenderer.onDrawFrame，其调用 native drawFrame。结果在 agent/codex/rea-mcp-verification.json。这证明本地服务器可工作；当前对话工具仍需客户端重新连接才能加载。

所有下载均使用现有系统代理 127.0.0.1:7897，没有切换 TUN 或改变代理配置。Ghidra ZIP SHA-256 为 ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db；JADX ZIP 为 545ea2be9c242511bc145755cf4bda2485ade42966e096f8b4d3da2a230e8974，均与 GitHub 官方 release asset digest 一致。JADX 引擎 JAR 为 6e5eacf500b64292bfb73c49797c1958f6ee44646e43e868039ae7feb573ff75。

## 分析对象与架构

唯一输入：TowerMadness+Zero_+3D+TD_1.22_APKPure.apk，44,479,763 字节。

SHA-256：0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b，分析前后保持一致。

REA 清单：包名 com.limbicsoftware.towermadnesszero，版本 1.22，versionCode 1444639614，minSdk 9，targetSdk 17；声明 OpenGL ES 1.0 与 accelerometer。JADX 引擎报告 2362 类、2159 资源。类检索 limbic 只匹配两个 R 资源类；结合原生绘制入口与原生导出函数，核心游戏逻辑位于 native，而不是 Java 层。

APK 包含 23 个 armeabi 原生库，没有 x86/x64/arm64 版本。主库 libverde.so 为 32 位小端 ARM ELF，4,291,592 字节，SHA-256 92c4663dfa4a8b6e7045516f6f5b31e90090de58b96394cd0e8efcee7e94edee。其 Objective-C 段、tmAppDelegate、Apportable Java 壳层及 Foundation/CoreFoundation/CoreGraphics/OpenAL 等依赖共同支持“由 iOS/Objective-C 项目经 Apportable 移植”的判断。未发现 Unity/IL2CPP 结构。

libverde.so 保留 4324 个已定义动态函数符号；按 GAME/ENEMIES/TOWERS/WAVES/PATH/MAP/WEAPONS/REPLAY 前缀筛出 233 个函数。没有 .symtab 或内嵌 DWARF 调试节，Ghidra 也未找到外置 DWARF。因此函数名字丰富，但原始结构体、变量类型和源码仍未恢复。

## 原生逻辑实证

REA 调用 inspect --provider ghidra 对主库明确返回 target_format_unsupported：Windows Ghidra P0 accepts PE targets only。这是 REA Windows 的限制；没有伪造成功、改扩展名或修改 REA 的限制检查。官方边界见 [REA Windows Ghidra 文档](https://github.com/morluto/rea/blob/main/docs/windows-ghidra-p0.md)。

直接 Ghidra headless 导入成功，识别 10,390 个函数，数据库为 agent/codex/ghidra-project/TowerMadness.gpr。12 个函数抽样全部 decompileCompleted=true，并导出指令：ENEMIES_canTowerDamageEnemy、ENEMIES_damageEnemy、ENEMIES_damageThroughArmor、GAME_buildTower、GAME_sellTower、GAME_sendNextWave、TECH_load、TOWERS_getTowerForCell、TOWERS_haveMoneyToBuild、TOWERS_sell、WAVES_load、WAVES_getNumWaves。

护甲函数样本在 gameplay-pseudocode.c 中显示：先查询敌人护甲类型与塔对象；塔不存在则返回 0；护甲 1/2/3 读取不同倍率，执行整数转 float、乘法及转整数；护甲 4 单独分支，其余返回原伤害。伤害主流程包含扣血、击杀金币/分数、生成子敌人等路径。资金检查明确比较玩家金币与塔参数。这些已超出只读字符串，证明局部规则可以追踪。

结构字段的语义尚须结合 TECH_load 等加载器和运行结果验证；浮点软运算助手及 undefined 类型影响伪代码可读性。decompileCompleted=true 仅表示生成伪代码完成，不证明代码等价。依赖库未导入，首次分析有 762 个未解析外部符号。Ghidra 地址在本次导入中比原始 ELF 符号值多 0x10000，例如护甲函数原始符号/本文件 .text 偏移 0x28a560，Ghidra 地址 0x29a560；禁止把后者直接当文件偏移。

## 资源复用与数值修改

| 资源 | 数量及证据 | 主要处理 |
| --- | --- | --- |
| 图像 | 422 PNG，签名全部标准 | 可复用；渲染仍需核对 UV、混合与缩放 |
| 音频 | 36 个 .caf，实际为 Ogg/Vorbis | 按真实格式加载 |
| 模型 | 321 .model、202 .vbo | 需资源转换/加载器 |
| 纹理/材质/图集 | 314 .texture、122 .material、148 .atlas | 多数 pvr 引用实际对应 .pvr.png；另有真实 Grape.pvr |
| 地图与玩法 | 118 .map、116 .gamemode | terrain 长度均等于 width×height；所有 gamemode 的 map/tech/waves 引用存在 |
| 波次与科技 | 89 .waves、4 .tech | JSON 数据可提取，支持大批参数修改 |

.model/.texture/.gamemode/.waves/.tech 都可严格 JSON 解析。29 地图、11 图集、1 材质含尾逗号；font.atlas 另缺闭合数组，转换时需单独处理。地图入口坐标可能超过 terrain 格尺寸，表明存在另一套游戏坐标映射，不能直接按网格数组解释。

202 VBO 的长度和全部索引范围通过检查。布局满足 63 字节头 + vertexBytes（头偏移43）+ indexBytes（偏移55）+ 24 字节尾。201 个使用 float32 XYZ+UV、20 字节步长和 uint16 索引；rect.vbo 使用 XY+UV、16 字节步长和 uint8 索引。实际顶点数据起于63；尾部包围盒含异常极值，转换时需重算。布局转 OBJ/glTF 可行，但本次未制作转换器或运行渲染验证。

每个 tech 有 63 个塔与 63 个武器定义；Easy/Medium/Hard/Insane 启用塔分别 26/43/61/63。塔字段有 cost/reload/range/buildtime/upgrades/disabled；武器字段有 damage/splash/速度/效果，涉及12类武器。86个普通波次数组合计8873条波次；另3个 Endless 文件为生成器配置，不能按普通数组处理。

因此调整塔价、射程、伤害、升级链、敌人血量/速度/奖励、波次安排及初始金币/生命的障碍较小；添加新武器类型、寻路规则或新效果需改原生处理代码或重建规则。已确认数据位置，不代表某一修改后的 APK 已验证运行。

## Windows 路线与难度

| 目标 | 判断 | 原因 |
| --- | --- | --- |
| 复用图像/音频/关卡/数值 | 低至中 | 资源透明；少数格式与坐标规则要修复/确认 |
| 在原 APK 修改已有数值 | 低至中 | 文本易改；仍要重打包、签名、兼容运行验证 |
| 恢复/改写局部规则 | 中 | 保留函数名，关键函数可反编译；缺类型与对照运行 |
| 复用资源重建 Windows 可玩版本 | 中高，具备推进条件 | 需要重建渲染、输入、时间步、寻路、12类武器、存档等 |
| 原二进制连同兼容框架直接移植 | 高 | ARM32、Android ABI、Objective-C runtime、旧平台接口与大量兼容库耦合 |

建议按“一个关卡+一种塔+一种敌人”完成资源加载、规则恢复和原作对照，再扩大到武器/效果/地图系统。这是后续推荐顺序，本次未开始游戏实现或擅自选择新引擎。通过模拟器在 Windows 运行可提供对照基准，但不等于原生 Windows 移植。

## 本机还需准备什么

静态分析阶段无需 Android Studio；现有 Node/JDK 加本次工具已足够。Windows C++ 编译环境已发现，暂无需再装一套 Visual Studio。

当前在 PATH、标准 SDK 路径和已安装软件清单范围内未发现 Android Studio、Android SDK/adb 或 Android 模拟器；这不是全盘文件不存在的证明。下一步最需要 Android 运行对照：有兼容32位ARM的旧安卓设备最好，或实际确认能运行本APK的模拟器。不能假设任意x86_64镜像都能运行armeabi库。

如修改 APK：准备 Android SDK Platform-Tools（adb）与 Build-Tools（apksigner、zipalign），另准备 Apktool 与自有测试签名。完整 Android Studio 是可选的管理界面，SDK 可独立使用。仅在确实重编译 Android 原生模块时考虑 NDK；安装 NDK 不能把反编译伪代码自动变成原工程。见 [Android 命令行工具](https://developer.android.com/tools)。

本 APK targetSdk=17；Android 14 默认阻止低于23的全新安装，因此不能用最新镜像的安装失败判断游戏无法恢复。官方提供低target测试用ADB选项，但它不解决ARM32或旧API运行兼容性。见 [Android 14 安装行为](https://developer.android.com/about/versions/14/behavior-changes-all#minimum-installable-target-api-level)。

## 验证、产物与未解决项

已验证：工具下载哈希；REA Codex/技能/Ghidra scoped doctor；REA APK 清单和类查询；真实 MCP 握手与 Java 方法反编译；Ghidra 数据库导入、保存、重新打开；12个函数输出；TSV列结构；原APK哈希未变。无Python、无新分支、无worktree；原目录不是已有源码仓库，没有建立远程或推送。

报告与 MEMORY.md 已创建；复现入口及完整限制见 agent/codex/README.md。REA Evidence：

- APK inventory：ev_fd90863483f2e897407a01d7b6b6003ac9db6c2b1919622a8da635f7a761bfb3，rea-artifact.json。
- Android package：ev_c9b730a2e5d29ee3f5390daa28ef820550f49adf46742c51f01633d0e31fe97e，rea-android.json。
- MCP method：ev_762b8cf73427ecec1b2630cddb36e1963759920561784e377a6a2ac664abc375，rea-mcp-verification.json。
- 原生 ELF：native-evidence/elf.json；Ghidra 伪代码/指令/清单分别为 gameplay-pseudocode.c、gameplay-instructions.tsv、functions.tsv。这些是直接Ghidra/自编只读脚本证据，不冒充REA原生Evidence。

未完成指标：尚无可运行Windows游戏、修改后APK或动态行为等价验证；未完整恢复寻路/无限波次公式、坐标转换、结构体、序列化、动画插值、全部武器算法和旧在线服务行为。恢复原始源码及一键完整移植的预期不成立；当前证据支持分模块重建。
