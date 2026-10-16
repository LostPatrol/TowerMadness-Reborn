<!-- Active original-x86 architecture, commands, current evidence, and immediate continuation work. -->
# 原 x86 开发基线

主线源码在 `tools/x86-reuse/`。Win32宿主映射原ELF32，CPU直接执行GAME/MENU/PGL；适配Windows C ABI、兼容OpenGL、输入、单线程GS和最小平台对象。原APK/ELF文件不改。原UI使用480×800逻辑坐标，Windows窗口可缩放，不依赖Android或CPU模拟器。完整移植尚未完成。

## 构建与验证

```powershell
& .\tools\x86-reuse\build.ps1 -SkipGlSignatures
& .\tools\x86-reuse\run.ps1 -Mode menu
```

默认构建在 `agent/codex/x86-reuse/current`；`play.cmd`进入同一原菜单。鼠标操作原轮盘，拖动平移，滚轮缩放，空格发波，Esc返回，F5/F9保存和读取。正常menu冷启动读取已有F5存档，停在原活动局菜单等待点击Resume；退出不自动保存，退出前按F5。测试continue仍读取独立的menu-save.bin。

构建先冻结源码、塔恢复及音频头文件，默认clean-first，记录stb_vorbis/JSON/CMake/EXE哈希。运行复制构建清单、输入和验收脚本，启动前后检查EXE。只有功能所需步骤真实发生才验收通过。交互menu/play默认有声；验收默认静音，`-Audio`开启，`-Silent`关闭。

```powershell
& .\tools\x86-reuse\run.ps1 -Mode menuauto -OutputDirectory "$PWD/agent/codex/verify/evidence"
& .\tools\x86-reuse\run.ps1 -Mode menuauto -Map continue -OutputDirectory "$PWD/agent/codex/verify/evidence"
& .\tools\x86-reuse\run.ps1 -Mode menuauto -Map restorestress -OutputDirectory "$PWD/agent/codex/stress/evidence"
```

前两条顺序执行；continue读真实前次保存。restorestress要求13次真实F9恢复。缺存档、缺步骤或恢复后时钟不前进均失败。`scene -Map <gamemode>`检查实际地图载入/绘制，`levels`仅枚举资源，`upgrade`是原API建升售。`campaign`由原菜单/账户决定地图；continue/restorestress都是测试场景名，不是地图选择。

其他验收入口：`scores`检查原成绩日期与replay三轮读写；`speed`实测原按钮1×/2×/4×/1×时钟；`audio -Audio`检查原source池、停止、音乐位置与失败隔离；`weapons -Map Peach`检查12组原API建造/升级/出售/绘制，核弹走完整Mortar前置链。`guiweapons -Map Peach`按`weapon-clicks.json`点击原四类别、12种基础塔的建造/首升/双点确认出售；两者都不证明全部武器战斗效果。`menuauto -Map options`点击原五项设置；`help`点击原Guide/Credits；`newgame`测试活动局从原菜单换新局；`dlc-Arcade`等通过原分类和拖动选择地图。

`menuauto -Map campaignfull`要求前两关自然胜利及继续链；`campaignall`还要求Hard、Madness和最终解锁。策略只能调用原合法建造/升级/发波，不写金币、生命或解锁；失败会使验收失败。`menuauto -Map latebattle`从独立F5 checkpoint构造晚局，检查真实旧GAME释放、活动敌人/弹体及玩家状态精确恢复、时钟推进和自然胜利。原恢复会重置倍速，driver通过原按钮恢复4×后再继续发波。

`weaponcombat -CombatCase <case>`使用`weapon-combat-cases.json`检查14类原伤害/效果触发，`verify-weapon-combat.ps1`汇总同一冻结EXE的全部case。Flak使用四个原合法位置、各扣50；不改出生点或敌人。该模式允许输局，仅证明原武器行为触发，不能替代胜利验收。

`& .\tools\x86-reuse\verify-upgrade-gui.ps1`默认执行157步非Mortar原GUI升级脚本；`verify-nuke-gui.ps1`复用同一验证器，执行19步Mortar→Nuke→H Bomb链。两者默认绑定current及其构建清单，支持显式EXE、输出目录和预期哈希；只派发鼠标操作并读取原状态，不直接调用建升售API。

```powershell
& .\tools\x86-reuse\package.ps1
```

便携目录包含原库/资产、标准化纹理、GS/GL元数据、EXE和`Play.cmd`。生成到新目录，不复制开发或用户存档；其`data`保存本地状态，`logs`保存诊断。`Play.cmd silent`静音。包使用静态MSVC runtime和Windows系统XAudio2，无Android依赖。

默认60 FPS，F6依次切换60/120/不限帧；标题显示实际循环帧率。`TOWERMADNESS_FPS`可指定非负整数，0为不限帧；`TOWERMADNESS_WINDOW=648x1080`指定初始客户区。便携启动器可组合`silent 1080 120`或`unlimited`参数。任意窗口比例采用居中黑边，3D按实际视口重绘，原贴图不会增加细节。

## 必需目录

| 路径 | 用途 |
| --- | --- |
| 根目录两份APK | 原始输入；身份和资源等价见[APK_BASELINES.md](APK_BASELINES.md) |
| `agent/codex/x86-reuse/package` | 唯一原x86解包，23库和原资产 |
| 同级GS/GL JSON、libverde-elf.json | 运行适配与原符号 |
| `assets`、`third_party` | 标准化资源、JSON与GL头文件；资源准备脚本共享上述package/assets |
| `agent/codex/x86-reuse/current` | 当前可执行文件、冻结输入和构建清单 |
| `agent/codex/x86-reuse/analysis`、`ghidra` | 原MENU/生命周期静态导出及x86数据库 |
| `tools/x86-reuse/ghidra` | 可复用的只读导出脚本 |
| `agent/codex/x86-reuse/verification` | 精简验收事实与历史Victory图片 |
| `agent/codex/tools` | 已注册REA/Ghidra/JADX运行时，保持路径不变 |

原APK、工具、数据库和运行输入未入Git；仅clone仓库不足以运行。旧重建与规则规格只在需要时查[reference](reference/README.md)。不要依赖已清理的旧过程目录或会话。

`CorrectionProbe.java`导出恢复/分配函数，参数为输出目录、libverde-elf.json；`NativeMenuProbe.java`导出菜单与调用关系，参数为输出目录、符号JSON、0x10000。两者使用现有NativeX86数据库和本地Ghidra headless的`-readOnly -noanalysis`，输出到新目录。`inspect-elf.mjs`接收原库路径，向stdout输出符号JSON，替代已删除的过程目录版本。

## 核心修复与接口合同

D2原因是原TOWERS_fromIBS只为104字节同步前缀分配104字节，随后写+0x68/+0x6c；正常TOWER_alloc需要112字节。`tower-restore.hpp`只修正塔读取分配，保留原数据和校验。原指令在analysis/restore.txt及analysis/menu，禁止改成全局malloc扩容或跳过GAME_free。

恢复请求统一排队到帧边界：先释放旧GAME，再走原GAME_fromIBS安装替代对象。原IBS读者复制数据，OBS/IBS包装对象正常释放。同步菜单操作后重新取得GAME，终局不绘制已释放对象。

`MENU_setResolution`接收float，`GAME_setResolution`接收int。首关导航22×32，不能折半。`GAME_upgradeTower(game,instanceId,targetTechId)`；`TOWERS_getIdForCell`接收TOWERS*（GAME+0x10）。Ghidra地址=ELF VA+0x10000，ARM规格中的地址不能直接套用。

installHook改私有映射导出入口，之后findExport可能已指向宿主。objc汇编dispatcher可能为STT_NOTYPE/size0，不能过滤掉fpret/stret。未初始化完整Objective-C运行库，不盲调objc_getClass或所有构造器；未知导入/selector仍明确失败。

`NSLocale preferredLanguages`返回Windows UI语言数组；稳定UIDevice token及`systemVersion=5.0`表示宿主支持的UIKit接口等级。NSString numeric/case比较使用Windows CompareStringEx。UITouch保留本帧和上一帧位置；拖动超过5px后tapCount为0，防止滑动地图列表被误判为启动地图。滚轮生成双指NSSet，走原GAME触摸缩放与镜头限位，不直接改camera矩阵。

原`SCORES_save`保存2001纪元，`SCORES_load`却调用1970 getter；`correctScoreDateReader`验证原指令后，仅修正私有映射内该调用的selector引用。不要改NSDate全局语义或原文件。原`WAVES_init(int)`必须传0；无参会随机预载custom boss。两个原包均缺RoboSteal模型，但普通89个wave资源不引用该特殊派遣单位，不伪造替代资源。

`point-sprite-correction.hpp`修复原Flak出生帧提交size=0而触发桌面OpenGL错误的问题。仅排除不可见精灵；正尺寸参数经验证的入口trampoline回到原PGL batch/cache，未夹断尺寸或吞glGetError。

`host-display.hpp`统一原glViewport/glScissor与鼠标、拖动、滚轮的物理/逻辑坐标映射，原MENU/GAME分辨率合同保持不变。`frame-pacing.hpp`用进程内QPC截止时间和waitable timer等待剩余帧时间，代替每帧固定Sleep；不改原游戏时钟或系统计时器设置。驱动交换间隔设为0，宿主负责帧率上限；实际显示仍受显示器与驱动限制。`TOWERMADNESS_FRAME_TRACE`可选输出逐帧CSV，常规日志每5秒记录FRAME_STATS。

## 音频与平台界面

`native-audio.hpp`用已有stb_vorbis解码原CAF中的Ogg/Vorbis，XAudio2负责设备/voice；`original-audio.hpp`适配OpenAL边界，保留原SOUND registry和3/3/10/5池及分配器。PCM归host所有，原decode hook返回null防止原shutdown误释放。单音乐文件或设备失败仅使对应音频失效，游戏继续。

MusicManager宿主保存每曲位置和音量、静音恢复，切曲取消旧pending恢复，淡入淡出为一秒线性近似；空间音频仅距离衰减，没有声像/Doppler/HRTF。原Options设置由原账户保存。购买验证按项目范围忽略，仅五个地图包及Flamethrower/Flashbait/ProximityMine三个StoreManager entitlement开放；四战役解锁仍由原胜利逻辑写入。

Guide/Credits展示原离线HTML，使用Windows默认浏览器，未恢复UIKit嵌入视图。在线排行/GameCenter/News没有服务适配，不伪造在线结果。原Achievements/g+点击可返回并继续正常离线操作；强制进入News组件仍缺NewsFetchDelegate。普通原菜单没有News或Scores入口；四个本地成绩页的组件测试不能扩大为玩家菜单入口。原Reset Stats为清空成绩列表，没有发现独立Stats页面。

## 当前证据与下一步

当前`current`和新版便携包`release/TowerMadness-x86-20261016-181000`使用EXE `618E8CD65A49580F06224027A2FE12580E040F8F7A21AE88C149E7171A04FB48`，host源码`6E05E21E47D83906A606A0954A21A1F938A9EFA3B1888546D42C83545E6C4184`。冻结清单见`current/build-manifest.json`；`agent/codex/fps-resize-20261016/current-promotion.json`确认由candidate3原样提升。该证据根的`nuke1080-candidate3`与`upgrade1080-candidate3`分别记录真实648×1080下19/157步GUI、native0、GL0；`validation`记录动态菜单与横竖镜头/F5/F9，`regression/core-summary-20261016-180515.json`记录两档倍速及跨进程恢复4/4通过。

`frame-analysis/candidate3-result-20261016-175521.json`记录隐藏菜单/首局循环60/120/0档累计59.65/118.71/9615.29FPS，稳定尾段60.01/120.01/9800.77，原模拟时间五秒均推进五秒。不限帧不等于显示器呈现帧率，也不是晚局性能保证。`portable-smoke/summary.json`记录2390项及EXE清单、干净release未运行无data/空logs、含空格副本真实648×1080/120启动/F6切换与正常退出；物理尺寸使用PerMonitorV2线程核验。短启动样本曾有58.4ms帧，不声称全部场景无长帧。用户首关资金不足升级卡顿尚需根因复现，不能把帧率改善等同已修复。

`regression/final-summary-20261016-180515.json`汇总新版上述4次核心验证加完整campaignall共5/5通过、native0/GL0。四战役自然胜利10/10/10/8羊，最终4933分、四unlock1；默认60档整个四战役59949帧累计59.9031FPS，尾窗60.0044。新版交付与未完成项见`agent/report/report-codex-20261016-182300-帧率与窗口缩放.md`。

以下为上一稳定版本证据：EXE `677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`，host源码`4C52456CECFDCDC66DC4D4F4B2201605119938FF1C1F79194A0F9F925B4EA870`，证据根`agent/codex/x86-continue-20261016/`。旧148次通过不算新版重新执行148次。

`final26/summary.json`汇总148次native运行：全部验收通过、同EXE、GL错误0。范围包括116张地图载入/绘制，原MENU四战役自然胜利和结算继续/解锁，五DLC原菜单，New game、Options、Guide/Credits，12基础塔97步GUI和12组API建升售，14类真实武器触发，真实1×/2×/4×/1×，原成绩日期/replay三轮持久化，13次F9/14次旧GAME释放及新进程继续，真实音频与音乐隔离/pending取消。四战役依次10/10/10/8羊，四个解锁值均1；Madness模拟1246925ms、战斗分4582、最终4941分。116个scene不是116关通关，14类武器触发测试以输局结束。

同EXE外部专项不混入148次计数：`latebattle-final2`在11塔/7敌人/2弹体及原玩家状态精确恢复后继续自然胜利，5羊/4809分；`nuke-gui-final26`用19步原鼠标GUI完成Mortar I至V→Nuke→H Bomb及双点出售，未直接调用建升售API。镜头13个样本、普通menu冷启动Resume及便携运行也有独立验证。四本地成绩组件与Game Center离线返回的范围说明见`menu-analysis/offline-pages`，不要据此声称新增了普通菜单入口。

`gui-upgrades-final26/runs/20261016-141854-518/acceptance.json`另记录同EXE非Mortar升级GUI157/157步通过、nativeExit0、GL错误0，脚本SHA256为`566B71221315A5AEC34515D2FDF879D3BB2069092DF3C8B076EE1A6CDDBA555E`。11条链实际构造52种不同type、7条链到末级；合并核弹链共59/63种原塔type。Fusion、Railgun VI、Tesla末级、Flamethrower III因原2500资金不足仅验证拒绝，未构造。共享验证器的19步核弹wrapper复验也通过，证据在`gui-upgrades-final26/nuke-wrapper/runs/20261016-142114-826`。

干净交付目录为`release/TowerMadness-x86-20261016-135600`；`final26/clean-release.json`确认2390项清单哈希一致、从未启动、无data且logs为空。`final26/portable-smoke.json`验证独立实验副本从`C:/Windows/Temp`和含空格路径运行，F5/F9、冷启动Resume及本地data隔离通过。`portable-campaign-final26/acceptance.json`另验证便携副本完整四战役10/10/10/9羊，Madness战斗4580/最终4975分；终局F5存档重启被原入口忽略，普通New game可用，全部earned进度和scores全文保持一致，没有重复结算。实验副本含测试存档，不作为干净交付物。

保留限制：普通89波不使用的RoboSteal特殊派遣资源在两包均缺失；在线News/Game Center未接服务；音频淡变与空间效果存在上述近似；GUI尚缺上述四种高价type实际构造；14类战斗观测不能代表全部升级组合或地图通关。原APK/ELF不修改；原包1944个资产及23个库（合计1967文件）保持不变。接续先核对上述最终清单，再按未覆盖需求扩展。
