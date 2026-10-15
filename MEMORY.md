<!-- Current development baseline, essential local inputs, and unresolved limits only. -->
# Project memory

## 当前真实基线

任务 03-correction 已审计并整顿主线。当前唯一开发入口为 tools/x86-reuse；不再恢复历史 DSH 自动目标，不从 agent/dsh/changes 或 agent/claude 复制整份 WIP。接续先读 docs/X86_CORE_REUSE.md 与 docs/BASELINE_AUDIT.md，旧 docs/CLAUDE_HANDOFF.md 仅为跳转页。未继续推进完整移植功能。

原 x86 核心在 Win32 宿主直接执行 ELF 机器码，复用原 GAME/MENU/PGL 和资源；Windows 适配 C ABI、OpenGL、输入和最小平台对象。固定480×800、静音，不初始化扬声器。最终不得依赖模拟器。原 APK/ELF 不改；内存私有映射允许明确的平台适配和直接缺陷修复。

旧 src/release 是已停止常规推进的 x64/SDL3 重建，参考标签 reconstruction-reference-20261014/f4e8e08。旧重建的完整保存、12武器等不能算作 x86 已完成。参考 docs/TECHNICAL_ROUTE_REVIEW.md、DEVELOPMENT.md 及各规则/表现文档。

任务03的关键修复：TOWERS_fromIBS 从104字节同步前缀分配104字节，却写+0x68/+0x6c两个运行字段；原TOWER_alloc实际分配112字节。独立内存围栏已捕获这一越界。tower-restore.hpp只修正该塔恢复分配，原读取/校验/存档字节保持不变，禁止改成通用malloc加大或跳过GAME_free。F9及脚本恢复统一排队到帧边界，旧GAME先释放再重建；OBS/IBS包装对象正常释放。旧“IBS别名/共享动物地址/先后释放顺序即根因”推断不作结论。

当前host.cpp SHA256：731eac007f466d993aeb0d60fcb54a58fb8248f8759bd27706b950a28a797a1a。
tower-restore.hpp SHA256：2b0705e5529fa85c5df1a01c59167b8d894e14a707307298b45cce9bf50a6327。
验证EXE SHA256：77d58c7c52f998fd3398da026e0d88f63729efb3f0d3274f32b5387475274eb1。
构建和后续运行均须看实际build-manifest，不靠以上哈希代替新验证。

默认build/run目录agent/codex/x86-reuse/current；tools/x86-reuse/play.cmd和agent/codex/user-test/launch-current-x86.cmd进入同一原菜单。build.ps1先冻结输入、默认clean-first，可显式-Incremental；run.ps1绑定源码/EXE/脚本并检查实际验收步骤。native0不等于验收成功；原map空操作已删除。menuauto -Map continue/restorestress是测试旋钮，不是地图选择；campaign也由原菜单/账户决定地图。

F5/F9用menu-save-hotkey.bin，测试跨进程继续用menu-save.bin；不手工互相替代。输出根/evidence对应同级state，不读取旧重建真实用户profile。旧profile使用SHGetKnownFolderPath，改LOCALAPPDATA环境变量不能隔离，后续勿碰。

## 证据边界和未解决风险

任务03实际验证含塔活动局两次menuauto、13次F9恢复及14次旧GAME释放、暂停冻结和恢复后时钟推进、跨进程继续、四图scene、原激光50→40→20→50。诊断仅用于定位，交付宿主走普通CRT。此门槛不证明任意存档/地图/长时恢复均稳定。

历史首关campaign-20261015-130009确有原Victory（8羊、1417分），随后preferredLanguages缺失退出，不是完整战役通过。历史116图的CSV/JSONL/232 PNG已独立复核，集合完全一致、116开局图各不相同；但旧每次运行没有源码/EXE哈希绑定，只能作历史载入绘制证据，不能当当前版本116图验收，更不是116关通关。

尚未验收：完整胜利继续/四战役与解锁链、原全部GUI与12武器、真实倍速、长时无限、多地图恢复、DPI/跨机器、最终独立包。原SCORES读1970但用2001纪元构造日期，日期漂移不应由通用NSDate语义造假修复。robosteal_walk0..19模型缺失仍未闭合。

## 本地输入与工具

两个原APK均不进Git；x86 Madness_Zero-1.22.apk SHA256为7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd；ARM包SHA256为0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b。两包配置/游戏资产基线见docs/APK_BASELINES.md。

运行必需：agent/codex/x86-reuse/package（23库及原资产）、其上级GS/GL JSON、assets/textures、third_party/json.hpp。原x86 libverde SHA256为adc78904cc5e89145fb9f4eca720d22cc48a89b634da1f95f4f053477c0ee7f6；任务03对原包1967条库/资产内容逐项校验一致。仅clone Git不能重现本地运行环境。

本机VisualStudio18 Community/MSVC14.51、Win32 CMake、Windows SDK；Node为C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。REA Windows provider不接受ELF；使用已存在的Ghidra12.1.4直接headless（JDK C:/java/jdk/21）可行。数据库agent/codex/x86-reuse/ghidra/NativeX86.gpr；顺序-readOnly -noanalysis，导出到新目录，禁止覆盖关键旧证据。原Android对照环境和工具保留，详见docs/ANDROID_REFERENCE.md。

审计现场agent/codex/correction；清理清单cleanup-files.json，争议原文retired-audit-inputs.zip；旧源码和交接before/。保留agent/dsh/menu-evidence、ghidra-scripts、evidence、关键恢复快照；其余状态/报告不作为当前指令。桥凭据和服务stdout不提交、不输出。另有本机默认DSH服务，不属于本任务；勿按历史PID终止。

Windows PowerShell；不使用gh；默认main，不新建分支/worktree。代理通常127.0.0.1:7897；Git私有仓库LostPatrol/TowerMadness-Reborn。勿关其他服务、动系统ACL/硬件、改根TODO。过程在agent/codex，报告在agent/report。当前用户AGENTS修改和既有20261013报告迁移不是本任务产生，提交时隔离。
