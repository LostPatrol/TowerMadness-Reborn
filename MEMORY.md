<!-- Essential continuation facts for the current original-x86 port. -->
# Project memory

## 接续入口

目标是在Windows 10/11上完整移植TowerMadness，尽量复用原代码、资源和GUI，最终不依赖模拟器。当前主线仅在tools/x86-reuse；src是保留的旧x64重建参考，其能力不能算入x86进度。

先读docs/X86_CORE_REUSE.md。主代理负责核心开发、根因定位和验收；不要恢复已清理的DSH/Claude会话或按旧任务书中的预算/分派说明自动启动DSH。用户后续指令优先。

下一阶段按个人离线产品收尾：用户负责各关完整游玩验收，自动化仅覆盖地图载入、塔链/战斗效果、存档、输入与性能等必要专项，不再要求所有关卡全流程模拟通关。所有因付费而受限的内容应离线直接可用，需复核现有五地图包/三特殊塔开放是否遗漏；正常金币消耗及通关解锁保留。无需实现联网验证、在线排行等服务；Wiki/Guide/Credits等简单链接可以保留，访问失败不应阻断离线游戏。下一goal重点为四种高价塔实际验收、付费内容开放和正式交付；缺失内容如实列明，最终全流程验收由用户完成。首关升级卡顿已按用户修订移出必做项，后续未再复现则不作为缺陷或交付阻碍，不再专门扩大排查。

交付必须支持双击TowerMadness.exe直接进入原菜单，无需参数、命令行、PowerShell或Play.cmd，普通启动不显示控制台。优先交付ZIP portable（用户也接受MSI），整理为清楚的EXE/运行资源/data/logs布局，自动按EXE所在目录定位资源与状态；常用设置通过菜单/窗口/快捷键完成并适当持久化，启动错误以可读界面提示。现有EXE仍要求PACKAGE/OPERATION/LOG参数，尚未满足此目标，不能把当前ZIP或Play.cmd称作已完成的双击EXE交付。

Task05最新入口为agent/task/05-x86-delivery.md：用户已同意在正式发布前先做16:9/16:10真实横屏和GUI合理重排，消除竖屏画布两侧大黑边。顺序为横屏技术原型→GUI/镜头→其他离线收尾→最终回归打包。优先复用原分辨率/相机接口，同步调整绘制与鼠标命中、轮盘边界、教程、选关及结算；不能用拉伸/裁切或装饰填充冒充适配。当前基线仍是竖屏逻辑视口，横屏尚未实施；不将旧测试坐标当作新横屏验收。

## 当前基线与缺口

当前主线直接执行原ELF机器码，适配Win32/OpenGL/输入、XAudio2音频与最小平台对象；480×800逻辑坐标配可缩放实际视口，不依赖Android或CPU模拟器。原APK/ELF不修改，原包1944个资产文件及23个库文件（合计1967项）哈希保持一致。

已修复D2：原TOWERS_fromIBS为104字节存档前缀只分配104字节，却写+0x68/+0x6c；tower-restore.hpp分配完整112字节运行对象，再走原读取/校验。F9及脚本恢复在帧边界先释放旧GAME再重建；不要用全局malloc扩容、跳过free或吞异常掩盖问题。

当前EXE SHA256为618E8CD65A49580F06224027A2FE12580E040F8F7A21AE88C149E7171A04FB48，host源码SHA256为6E05E21E47D83906A606A0954A21A1F938A9EFA3B1888546D42C83545E6C4184。current由fps-resize-20261016/candidate3原样提升，冻结对应关系见current/build-manifest.json，提升记录见agent/codex/fps-resize-20261016/current-promotion.json。

新增host-display.hpp集中glViewport/glScissor、letterbox和输入映射；frame-pacing.hpp默认60FPS，F6切换60/120/0，TOWERMADNESS_FPS支持任意非负上限，TOWERMADNESS_WINDOW支持初始客户区。原游戏时钟未改，禁止用更改speed/realtime提高帧率。648×1080下19+157步原GUI已通过；动态菜单、横竖窗口镜头与F5/F9、跨进程继续、60/120倍速及干净便携副本通过。证据根agent/codex/fps-resize-20261016；旧约31.82FPS，新稳定60/120；不限帧数字是宿主循环而非显示器刷新率。

新版regression/final-summary-20261016-180515.json包含5/5新回归：两档speed、menuauto、continue、完整campaignall，均native0/GL0。四自然胜利10/10/10/8羊、四unlock1、Madness4933分；59949帧全程59.9031FPS。新版交付/限制总报告为agent/report/report-codex-20261016-182300-帧率与窗口缩放.md。

上一稳定版677A091E…的统一证据仍为agent/codex/x86-continue-20261016/final26/summary.json：148次native运行全通过、同EXE、GL错误0。下述原玩法完整覆盖以该旧版本为基础，不得当成新版重新执行148次；新版专项另列。

同一最终EXE已实测原MENU四战役自然胜利、结算继续及原账户解锁，依次10/10/10/8羊，Madness最终4941分，四个解锁值均1。116地图全部载入/绘制通过，仅代表scene覆盖。五个DLC真实菜单选择、原New game/Resume、12基础塔97步GUI建造/首升/出售、12组API建升售与14类武器触发通过。19步核弹GUI加157步非Mortar升级GUI实际构造59/63原塔type；Fusion、Railgun VI、Tesla末级、Flamethrower III仅验证原2500资金不足拒绝，尚未构造。入口为tools/x86-reuse/verify-upgrade-gui.ps1及verify-nuke-gui.ps1。触发测试输局，不代表地图通关或全部升级组合覆盖。

已实测真实1×/2×/4×/1×、原成绩日期/replay读写、含塔恢复、暂停冻结、跨进程继续和13次F9/14次旧GAME释放。同EXE晚局11塔/7敌人/2弹体及原玩家状态精确恢复后自然胜利5羊；原恢复重置倍速，测试driver必须通过原按钮重按4×后再按原节奏发波，不写speed字段。

新版干净交付目录为release/TowerMadness-x86-20261016-181000及同名ZIP；2390项和EXE哈希一致、无data且logs为空。便携副本已验证Play.cmd silent 1080 120、真实648×1080和F6不限帧、正常退出。测物理尺寸需调用方线程PerMonitorV2，否则150%DPI下GetClientRect会虚拟化为432×720。

上一交付release/TowerMadness-x86-20261016-135600已被用户使用，原目录及data不得覆盖。其独立副本曾验证WinTemp/含空格路径F5/F9、冷启动Resume和四战役（10/10/10/9羊，Madness4975分）；终局F5冷启动忽略已结束GAME，保留earned/scores且不重复结算。实验副本含测试存档，不作为干净交付物。

剩余边界：两包均缺RoboSteal特殊派遣模型，普通89个wave资源不引用，不伪造资源。在线News/Game Center未接服务；普通原菜单无News或Scores入口，本地四成绩组件覆盖不等于新增入口。音频淡变为1秒线性近似，空间音频仅距离衰减，无声像/Doppler/HRTF。磁盘写满/拒写时保存stream最终状态提示不足，正常F5/恢复已验收；主要实测为本机，未完成另一台Win10验证。最终证据入口见docs/X86_CORE_REUSE.md。

首关资金不足连点卡顿曾被报告，但用户现已撤下专门排查要求，未再出现则视为非缺陷，不阻碍交付。隔离旧版PlainPlane未发波、发波3秒及20秒实际交战/音频三组各20次真实失败均未复现；证据agent/codex/insufficient-perf-20261016/plain-*及report-codex-plainplane-insufficient-perf-20261016-182725.md，仅在再次出现时定向参考，不宣称已经修复。禁止吞点击/防抖或改金币。系统音量已按用户睡觉要求调至3%，后续不要自行调高；验收默认静音。

## 运行和工具

默认build/run目录agent/codex/x86-reuse/current；tools/x86-reuse/play.cmd进入原菜单。build.ps1冻结输入并默认clean-first；run.ps1绑定EXE/源码/脚本，缺少实际恢复/时钟推进步骤时失败。native0不等于验收成功。

F5/F9用menu-save-hotkey.bin，menuauto跨进程继续用menu-save.bin，不能互相冒充。普通menu冷启动读取F5存档并暂停，点击原Resume才推进；退出不自动保存，退出前按F5。测试用显式OutputDirectory，其evidence同级state与真实用户存档隔离；修改LOCALAPPDATA环境变量不能隔离旧重建SHGetKnownFolderPath。

已修复原SCORES读日期的1970/2001纪元不一致，仅改私有映射selector引用；原文件不变。Flak出生帧size=0仅排除不可见精灵，正尺寸仍走原PGL batch/cache，不吞GL错误。镜头保留UITouch previousLocation，滚轮走原双指缩放；StoreManager仅开放五地图包和三特殊塔购买selector，战役仍由原胜利解锁。CAF内Vorbis经stb_vorbis/XAudio2播放，MusicManager切曲会取消旧pending恢复。

必须保留：根目录两APK；agent/codex/x86-reuse的package、GS/GL JSON、libverde-elf.json、current、ghidra；assets；third_party。prepare-assets.mjs现在共享x86 package/assets，不再需要第二份ARM解包。

x86静态导出集中在agent/codex/x86-reuse/analysis；导出脚本在tools/x86-reuse/ghidra。已有Ghidra数据库NativeX86.gpr，用-readOnly -noanalysis顺序导出；Ghidra地址=ELF VA+0x10000。REA Windows provider不接受ELF，可直接用本地Ghidra headless。不要盲调未初始化的原objc运行库；MENU_setResolution是float，GAME_setResolution是int。

本机VS18 Community/MSVC14.51、Win32 CMake、JDK C:/java/jdk/21；Node为C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。agent/codex/tools中的REA/Ghidra/JADX是已注册工具运行时，不是调试垃圾，不可直接删除。

Android只保留TM_Original_API17_ARM_clean与对应SDK/镜像/原签名依赖，详情docs/ANDROID_REFERENCE.md；未启动模拟器或改变系统设置。可按需参考docs/reference和agent/codex/reference/arm的旧规格，非启动必读。

Windows PowerShell，不用gh；main默认不新建分支/worktree，代理通常127.0.0.1:7897。勿碰真实用户profile、其他服务或根TODO。用户原AGENTS改动不随手提交；任务文件不自行生成。过程放agent/codex，报告放agent/report。
