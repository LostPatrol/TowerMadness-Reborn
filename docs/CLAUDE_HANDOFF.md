<!-- Developer handoff: original-x86 port baseline, local artifacts, failures, and continuation steps. -->
# TowerMadness 原 x86 移植：Claude 开发交接

交接日期：2026-10-15。工作目录：`E:\TowerMadness-Reborn`，Windows / PowerShell，Git `main`。

用户因成本原因暂停 Codex 目标，拟由 Claude 接续。**本次交接只整理文档，没有恢复开发、构建、测试或 DSH 自动续跑。** Claude 应在用户指示继续后开展开发；不需要为了接续而恢复 Codex/DSH 服务。

## 1. 目标与路线

先读根目录 `AGENTS.md`、`agent/task/02-x86.md`，再按需查阅 `MEMORY.md`。任务要求：在 Windows 10/11 上相对完整地移植 TowerMadness，包含原菜单、GUI、HUD、地图关卡、战斗、敌人与炮塔、金币升级及战役通关。尽量复用原代码、资源和界面，不降低需求来宣称完成。

当前主线为 **Zero 1.22 原 x86 核心复用**：Win32 C++ 宿主映射原 ELF32，CPU 直接执行原 `libverde.so` 及包内依赖；复用原 GAME/MENU/PGL 等机器码，适配 Windows C ABI、OpenGL、触摸、语言与存储边界。运行时不依赖 Android 或 CPU 模拟器，也不是将反编译伪代码重新编译。

另一条旧路线是 `src/` 中的 Windows x64 C++/SDL3 重建版，`release/TowerMadness.exe` 属于它。已停止常规推进，保留作参考，标签 `reconstruction-reference-20261014` → `f4e8e08`。**README 和 MEMORY 中旧重建版的12武器、倍速、完整保存等能力，不可算作原 x86 主线已完成。** 当前路线说明为 `docs/X86_CORE_REUSE.md`，路线比较为 `docs/TECHNICAL_ROUTE_REVIEW.md`。

边界：个人学习与怀旧，不分发；全程静音，不初始化扬声器；忽略广告、购买验证和联网排名。允许模拟器辅助对照，最终交付必须 Windows 原生运行。原 APK/ELF 文件不改；进程私有映射内的平台入口适配与原文件修改不同。

## 2. 三个版本必须分清

以下 SHA256 已在此次交接时重新读取核对；后续修改后应重新记录，不能只凭 build 编号。

| 对象 | 路径 / 状态 | SHA256 |
| --- | --- | --- |
| Git 中的产品宿主 | `tools/x86-reuse/host.cpp`；尚未提升后继失败候选 | `61ad5b431bbd8a832664ea62f78aa9cd0fcb403e3d7238ff88429c8189caac2c` |
| 手工测试 exe | `agent/codex/x86-promotion-20261015-0550/product-build/Release/TowerMadnessX86Probe.exe`；既有原型 | `fa11b98620b702e600cfb077f4c6ede351a3e40be2a4f624d214c0a05e3b7337` |
| 最新开发源码 | `agent/dsh/changes/tools/x86-reuse/host.cpp`；**未编译、未通过验收** | `c3bee89600d1c338dcd8f30669ecfd26bd6695653dc6a9be8e8114df8f973e48` |
| 最后已构建 exe | `agent/dsh/build/Release/TowerMadnessX86Probe.exe`；候选129，关闭诊断仍崩溃 | `c4c24a8e4155ec88d5394d842e4099aeee38f1cf5ea73197a497f0a8988a2467` |

最后已构建 exe 对应源码为 `cf24d2babcbde3ab67bb41fe88edf05be793b9f1e75c0316784e3034c9cba3e6`，快照 `agent/dsh/bridge/snapshots/snap-mv97dq9t-ee2c6438/`。它与最新开发源码不是同一版本。

最新源码快照为 `agent/dsh/bridge/snapshots/snap-mv97geg7-eeb49ce9/`，明确 `unbuiltChanges=true`。停止时恢复副本为 `agent/dsh/recovery-20261015-152127/`，含源码、状态及哈希。**不要整份覆盖产品源码，也不要将快照 metadata 的 `verified=false` 或构建成功当作运行验收。**

交接前 `main` 最新提交为 `4126bc5`；此次交接文档另作提交。工作区原本存在用户修改的 `AGENTS.md`、旧报告删除及 `agent/report/history/`、未跟踪的任务文件；不要清理、恢复或顺手提交这些变动。不新建分支/worktree，默认在 main 工作。

## 3. 已有证据与实际缺口

| 项目 | 已观察到的结果 | 不能扩大的结论 |
| --- | --- | --- |
| 原 GUI 与战斗 | 原菜单、选关、教程、鼠标轮盘建塔、发波、击杀、暂停冻结/继续、失败结算、本地成绩、PLAY AGAIN 有成功记录 | Options/Guide/Towerpedia 等全部菜单尚未验证 |
| 首关自然胜利 | `agent/dsh/evidence/campaign-20261015-130009/`；原 GAME 判胜，sim433075ms、8羊、1417分，原 Victory 页已目视 | 随后点击继续因未实现 NSLocale 消息退出；不是完整胜利流程成功 |
| 战役推进 | 原代码写入 `unlocked_medium=1`；新进程读回并创建 DividedAttention，真实扣款建塔、发波及失败重开 | 第二关、Hard/Insane/EndOfGame 和全战役通关未证明 |
| 116地图 | `agent/dsh/evidence/map-matrix/full116-20261015-130003/`；116独立gamemode、0失败、232张PNG，CSV/JSONL各116行，与原资源集合一致 | 只证明载入/绘制，不证明116关战斗或通关，也不证明原选关菜单可访问全部地图 |
| 原存档 | 活动局6082字节 OBS 保存、IBS恢复、跨进程继续有单次成功记录 | 反复F9/替换活动GAME存在严重堆损坏，不能称保存系统稳定 |
| 金币与升级 | 原命令测试建塔50→40、升级→20、出售→50；使用原科技链 | 不等于12武器全部GUI升级链验证通过 |
| 倍速 | 原切换入口已调用；已分析原更新合同 | 采样仍约实时1:1，不能从按钮或日志推断已实现4倍速 |
| 打包 | 已有直接调用包内exe的自包含布局、逐文件校验、系统DLL依赖检查和包内data写入证据 | 最新包含已撤销候选，不能作为可靠新版本交付 |

其它未闭合：`robosteal_walk0..19` 模型缺失影响动画；跨机器/DPI、长时间稳定性、全武器、完整原GUI与动态保真未完整验收。当前无可信的“总体完成百分比”。

## 4. 首要阻塞：恢复活动局后的堆损坏

**已确认事实：** 候选121两次在F9恢复后切换暂停附近以 `0xC0000374 / STATUS_HEAP_CORRUPTION` 退出；后继候选129关闭分配器诊断仍在相同语义位置失败。原生错误码应看 `*-run.json`，外层脚本的exit1只是探针失败。

优先读取：

1. `agent/dsh/evidence/menuauto-20261015-151420/`：最后已构建候选129的失败；最后为 `ORIGINAL_MENU_PAUSE_REQUESTED`，没有对应PAUSE_STATE。
2. `agent/dsh/evidence/menuauto-20261015-145024/`、`menuauto-20261015-145122/`：早期两次堆损坏。
3. `agent/dsh/qa/game-free-ownership-findings.md`、`d2-second-free-findings.md`：原构造/析构及潜在损坏位置。
4. `agent/dsh/qa/review-final-fix-candidate125.md`、`promotion-review-build121.md`：独立源码审核。
5. `agent/dsh/handoff-codex-20261015.md` 顶部最终状态。其下保留过时历史段落，甚至有旧 CURRENT 标签；**以顶部哈希、实际文件和证据为准，不照抄历史结论。**

**已经做的修改但不足以解决问题：** 最新WIP将恢复请求排队，在循环缓存game指针前处理，脱离旧game → 原GAME_free → 原GAME_fromIBS → 安装新game；脚本往返也走该队列，避免帧内局部旧GAME指针继续使用。对原MENU_endGame和待释放对象的所有权增加检查。先前123/124的 `ENEMIES_checkWin` 访问异常属于这类旧指针问题，不能与尚存的堆损坏混为已全部解决。

**仍是假设的方向：** ENEMIES反序列化中的id索引、对象链或游戏对象池可能较早写坏堆，后续分配/释放才检出。当前没有证据确定精确写入点；审核文档对原因的高置信措辞也应当作假设核对。

可定向继续导出 `ENEMY_fromIBS`（ELF `0x25fecd`）、`ENEMIES_alloc`（`0x25c3f8`）、`ENEMIES_free`（`0x25ecb0`），以及池 `HEAP_alloc/pop/free`（`0x280924/0x280aae/0x28096b`）。先核对地址属于ELF还是Ghidra地址再使用。

`TM_DIAG_ALLOC` 是WIP的可选分配器诊断；未设置时走普通CRT。启用诊断曾有两次menuauto成功，但continue仍失败；关闭诊断又崩，**这只能说明堆布局/复现条件变化，不能算修复**。未知分配来源不等于double-free，尤其strdup也分配内存；size0的分配语义须保持原样。最新源码另外含尚未编译的ENEMIES大小探针，先检查其实现再使用。

直接修复错误对象和ABI/所有权合同；不要以跳过free、泄漏旧游戏、吞异常或忽略未知API作为最终解决方案。临时诊断实验必须与产品修改分开记录。

## 5. 接续构建、运行与验收

本机工具：Visual Studio 18 Community（MSVC14.51、Windows SDK、CMake）、Node、JDK21与Ghidra已在本地。x86宿主必须用 **Win32** 构建；旧重建版才是x64。`third_party/json.hpp` 和原资源等本地输入须保留。脚本中部分工具路径为本机固定绝对路径，换机器时先适配。

现有手测入口：`E:\TowerMadness-Reborn\agent\codex\user-test\launch-current-x86.cmd`。进入原menu模式，固定480×800、静音；鼠标操作原界面，空格发波、Esc返回、F5/F9保存读取。读档稳定性未保证。exe依赖项目内原包和PNG，不能单独拷走；此入口不会运行最新WIP。

下面是 **用户授权Claude继续之后** 的示例，不是此次交接已执行的操作：先把WIP复制到Claude自己的过程目录，保留DSH冻结件不动，再显式指定编译源。也可先构建Git产品基线作对照，不要只依赖可能陈旧的CMake缓存。

```powershell
Set-Location 'E:\TowerMadness-Reborn'
New-Item -ItemType Directory -Force '.\agent\claude' | Out-Null
Copy-Item -LiteralPath '.\agent\dsh\changes\tools\x86-reuse\host.cpp' -Destination '.\agent\claude\host.cpp'
& .\tools\x86-reuse\build.ps1 -BuildDirectory 'E:\TowerMadness-Reborn\agent\claude\build' -HostSource 'E:\TowerMadness-Reborn\agent\claude\host.cpp' -SkipGlSignatures
& .\tools\x86-reuse\run.ps1 -Mode menuauto -BuildDirectory 'E:\TowerMadness-Reborn\agent\claude\build' -OutputDirectory 'E:\TowerMadness-Reborn\agent\claude\evidence'
```

`run.ps1` 读取固定 `agent/codex/x86-reuse/package`，每次创建独立证据子目录。`-Mode menuauto -Map continue` 是跨进程恢复探针；它需要该输出目录对应state中的 `menu-save.bin`。`F5/F9` 使用 `menu-save-hotkey.bin`，两个文件不同。`campaign`实际映射为 `menuauto:campaign`，选中地图来自原账户/菜单，**不是传入-Map就真的换了战役地图**。`mode=map`为空操作，禁止计入通过。

最小恢复验收：诊断关闭、有真实塔/发波且 `gameStarted=true` 的活动局，连续两次menuauto和一次新进程continue均native0；核对状态、暂停冻结、恢复后继续战斗、旧对象释放，再扩展手工反复F5/F9。空白教程局到FINAL不能代替活动局。构建后立即保存源码、exe哈希与快照，再运行；测试期间不要让另一个构建覆盖exe。此门槛只是当前故障回归，不能替代完整目标验收。

修复该阻塞后，建议顺序：原胜利页点击继续链/NSLocale → 完整四战役合法自然通关及解锁 → GUI升级出售/重开和遗漏菜单 → 武器与地图战斗覆盖 → 倍速实测 → 干净用户状态的独立包验证。首关五塔布防及逐秒合法升级参考旧 `src/checks.cpp`，仅参考策略；不要用重建核心替代原代码验收。

## 6. 容易重复踩坑的合同

- 原PlainPlane地形11×16，导航22×32，塔占2×2导航格；不要把布防坐标折半。后续地图不能沿用首关策略来推断不可通关。
- `GAME_upgradeTower(game, instanceId, targetTechId)`；`TOWERS_getIdForCell`第一个参数是TOWERS*（位于GAME+0x10），不是GAME*。用原科技配置确认合法链，不能盲试相邻ID。
- `MENU_setResolution`是float逻辑尺寸，`GAME_setResolution`是int。
- 原OBS/IBS字节保持不变；宿主文件外层是IBS1 magic及长度。不要将C++ vector的内存交给原IBS_free。缓冲区生命周期假设应核对原读取实现，不能把保留所有缓冲区当成已证实根因修复。
- `objc_msgSend_fpret`等汇编导出可能STT_NOTYPE/size0，hook筛选不能误排除；曾因此在结算处崩溃。
- `installHook`改导出入口，随后findExport得到的可能已经是宿主适配；不能把它当未改动的Foundation入口。不要盲目运行所有库构造函数或直接调用未初始化objc运行库。
- 原SCORES日期混用了1970/2001基准；宿主忠实NSDate语义会保留原日期漂移，不要篡改通用日期语义掩饰。
- fade=1不代表白屏，需看稳定帧。未知导入/selector应明确报错，不能给通用成功值。
- 只能以原胜负判定认定胜利；不抬钱、改HP、强制胜利或用不合法升级路径美化结果。

## 7. 逆向与本地资料入口

定向分析已有原作，不必从零导入：

| 用途 | 本地位置 |
| --- | --- |
| 原库/资源 | `agent/codex/x86-reuse/package/lib/x86/`、`package/assets/` |
| 图形与GS元数据 | `agent/codex/x86-reuse/gl-signatures.json`、`gs-instructions.json` |
| 标准PNG | `assets/textures/` |
| x86符号与Ghidra项目 | `agent/codex/x86-reuse/libverde-elf.json`、`ghidra/NativeX86.gpr` |
| Ghidra工具 | `agent/codex/tools/ghidra/ghidra_12.1.4_PUBLIC/`；JDK `C:\java\jdk\21` |
| 当前导出 | `agent/dsh/menu-evidence/` 下pseudocode/instructions/symbols等文件 |
| 只读导出脚本 | `agent/dsh/bridge/ghidra.ps1`、`agent/dsh/ghidra-scripts/NativeMenuProbe.java` |
| 地图全量审核 | `agent/dsh/qa/map-matrix-116-results.md` |
| 末轮完整记录 | `agent/report/report-codex-20261015-151700-x86一小时阶段收束.md` |

Ghidra显示地址通常为 **ELF VA + 0x10000**。原项目以 `-readOnly -noanalysis` 顺序导出，不并行争用同一项目锁。导出脚本会覆盖同名结果，先保留有参考价值的旧文件；伪代码行号随导出变化，不是稳定定位。宿主源码可用函数名搜索，不依赖旧审核文档行号。

## 8. 打包、数据保护与暂停状态

已有打包脚本 `agent/dsh/package/package.ps1`，布局为 `runtime/original/x86/{exe,package,GS/GL元数据}`、`assets/textures`、空 `data` 和直接启动exe的 `launch.cmd`。候选新鲜且通过验收后再使用。`staging-20261015-145216` 含已撤销exe；旧 `staging-20261015-043811` 更旧，均不能冒充最终交付。不要重新发明依赖仓库/PowerShell的“独立包”启动器。

旧重建用户存档在 `%LOCALAPPDATA%\TowerMadness-Reborn\profile.json`；勿触碰。原x86开发探针使用输出目录关联的state，独立包使用包内data。覆盖LOCALAPPDATA环境变量不等于隔离旧重建的SHGetKnownFolderPath，测试应使用明确的测试数据路径。

Codex总目标为paused。15:21收束时DSH为blocked/revision13、disarmed、running=false，会话cancel成功，无遗留原生测试进程；其pause接口因不接受blocked而拒绝转换，不等于还在运行。不应为了把标签改成paused而重新启动模型任务。历史PID可能被其他应用复用，勿按旧PID杀进程。若Claude自行开发，不需要启动DSH；若用户以后要求使用它，再核对live状态。

`agent/codex/`、`agent/dsh/`、`third_party/`、APK及构建输出被Git忽略。**只克隆Git无法恢复当前开发现场。** 换电脑至少迁移原包与纹理、GS/GL元数据、依赖、WIP和快照、所需失败证据/测试state；需要逆向时再迁移Ghidra项目和工具。DSH的cookie、服务stdout启动链接可能含凭据，不放交接文档、不提交或公开分享。

最后遵守AGENTS：最小改动、英文简短注释、优先apply_patch、必要验证、不改未经授权的根TODO、不删原作或用户文件、不改系统/硬件设置、不关闭其他服务、不使用gh。默认提交同步main，显式暂存本次文件；报告放 `agent/report`，MEMORY只维护真实基线和重要未解决事项。
