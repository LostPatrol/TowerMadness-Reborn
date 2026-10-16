<!-- Essential continuation facts for the current original-x86 port. -->
# Project memory

## 接续入口

目标是在Windows 10/11上完整移植TowerMadness，尽量复用原代码、资源和GUI，最终不依赖模拟器。当前主线仅在tools/x86-reuse；src是保留的旧x64重建参考，其能力不能算入x86进度。

先读docs/X86_CORE_REUSE.md。主代理负责核心开发、根因定位和验收；不要恢复已清理的DSH/Claude会话或按旧任务书中的预算/分派说明自动启动DSH。用户后续指令优先。

## 当前基线与缺口

当前主线直接执行原ELF机器码，适配Win32/OpenGL/输入、XAudio2音频与最小平台对象；固定480×800，不依赖Android或CPU模拟器。原APK/ELF不修改，原1967个资产文件哈希保持一致。

已修复D2：原TOWERS_fromIBS为104字节存档前缀只分配104字节，却写+0x68/+0x6c；tower-restore.hpp分配完整112字节运行对象，再走原读取/校验。F9及脚本恢复在帧边界先释放旧GAME再重建；不要用全局malloc扩容、跳过free或吞异常掩盖问题。

当前EXE SHA256为677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A，host源码SHA256为4C52456CECFDCDC66DC4D4F4B2201605119938FF1C1F79194A0F9F925B4EA870。current从冻结候选原样提升，源码/头文件/EXE对应关系见agent/codex/x86-reuse/current/build-manifest.json。最终统一证据为agent/codex/x86-continue-20261016/final26/summary.json：148次native运行全通过、同EXE、GL错误0；外部专项另列，不混入该计数。

同一最终EXE已实测原MENU四战役自然胜利、结算继续及原账户解锁，依次10/10/10/8羊，Madness最终4941分，四个解锁值均1。116地图全部载入/绘制通过，仅代表scene覆盖。五个DLC真实菜单选择、原New game/Resume、12基础塔97步GUI建造/首升/出售、12组API建升售与14类武器触发通过。19步核弹GUI加157步非Mortar升级GUI实际构造59/63原塔type；Fusion、Railgun VI、Tesla末级、Flamethrower III仅验证原2500资金不足拒绝，尚未构造。入口为tools/x86-reuse/verify-upgrade-gui.ps1及verify-nuke-gui.ps1。触发测试输局，不代表地图通关或全部升级组合覆盖。

已实测真实1×/2×/4×/1×、原成绩日期/replay读写、含塔恢复、暂停冻结、跨进程继续和13次F9/14次旧GAME释放。同EXE晚局11塔/7敌人/2弹体及原玩家状态精确恢复后自然胜利5羊；原恢复重置倍速，测试driver必须通过原按钮重按4×后再按原节奏发波，不写speed字段。

干净交付目录为release/TowerMadness-x86-20261016-135600，2390项清单哈希一致、未启动、不含用户data且logs为空。独立实验副本已从C:/Windows/Temp及含空格路径验证F5/F9、冷启动Resume、完整四战役（10/10/10/9羊，Madness4975分）；终局F5冷启动忽略已结束GAME，保留全部earned进度与成绩且不重复结算。实验副本含测试存档，不能替代干净交付目录。

剩余边界：两包均缺RoboSteal特殊派遣模型，普通89个wave资源不引用，不伪造资源。在线News/Game Center未接服务；普通原菜单无News或Scores入口，本地四成绩组件覆盖不等于新增入口。音频淡变为1秒线性近似，空间音频仅距离衰减，无声像/Doppler/HRTF。磁盘写满/拒写时保存stream最终状态提示不足，正常F5/恢复已验收；主要实测为本机，未完成另一台Win10验证。最终证据入口见docs/X86_CORE_REUSE.md。

## 运行和工具

默认build/run目录agent/codex/x86-reuse/current；tools/x86-reuse/play.cmd进入原菜单。build.ps1冻结输入并默认clean-first；run.ps1绑定EXE/源码/脚本，缺少实际恢复/时钟推进步骤时失败。native0不等于验收成功。

F5/F9用menu-save-hotkey.bin，menuauto跨进程继续用menu-save.bin，不能互相冒充。普通menu冷启动读取F5存档并暂停，点击原Resume才推进；退出不自动保存，退出前按F5。测试用显式OutputDirectory，其evidence同级state与真实用户存档隔离；修改LOCALAPPDATA环境变量不能隔离旧重建SHGetKnownFolderPath。

已修复原SCORES读日期的1970/2001纪元不一致，仅改私有映射selector引用；原文件不变。Flak出生帧size=0仅排除不可见精灵，正尺寸仍走原PGL batch/cache，不吞GL错误。镜头保留UITouch previousLocation，滚轮走原双指缩放；StoreManager仅开放五地图包和三特殊塔购买selector，战役仍由原胜利解锁。CAF内Vorbis经stb_vorbis/XAudio2播放，MusicManager切曲会取消旧pending恢复。

必须保留：根目录两APK；agent/codex/x86-reuse的package、GS/GL JSON、libverde-elf.json、current、ghidra；assets；third_party。prepare-assets.mjs现在共享x86 package/assets，不再需要第二份ARM解包。

x86静态导出集中在agent/codex/x86-reuse/analysis；导出脚本在tools/x86-reuse/ghidra。已有Ghidra数据库NativeX86.gpr，用-readOnly -noanalysis顺序导出；Ghidra地址=ELF VA+0x10000。REA Windows provider不接受ELF，可直接用本地Ghidra headless。不要盲调未初始化的原objc运行库；MENU_setResolution是float，GAME_setResolution是int。

本机VS18 Community/MSVC14.51、Win32 CMake、JDK C:/java/jdk/21；Node为C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。agent/codex/tools中的REA/Ghidra/JADX是已注册工具运行时，不是调试垃圾，不可直接删除。

Android只保留TM_Original_API17_ARM_clean与对应SDK/镜像/原签名依赖，详情docs/ANDROID_REFERENCE.md；未启动模拟器或改变系统设置。可按需参考docs/reference和agent/codex/reference/arm的旧规格，非启动必读。

Windows PowerShell，不用gh；main默认不新建分支/worktree，代理通常127.0.0.1:7897。勿碰真实用户profile、其他服务或根TODO。用户原AGENTS改动不随手提交；任务文件不自行生成。过程放agent/codex，报告放agent/report。
