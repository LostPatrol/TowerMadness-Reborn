<!-- Essential continuation facts for the current original-x86 port. -->
# Project memory

## 接续入口

目标是在Windows 10/11上完整移植TowerMadness，尽量复用原代码、资源和GUI，最终不依赖模拟器。当前主线仅在tools/x86-reuse；src是保留的旧x64重建参考，其能力不能算入x86进度。

先读docs/X86_CORE_REUSE.md。主代理负责核心开发、根因定位和验收；不要恢复已清理的DSH/Claude会话或按旧任务书中的预算/分派说明自动启动DSH。用户后续指令优先。

## 当前基线与缺口

当前原型直接执行原ELF机器码，适配Win32/OpenGL/输入与最小平台对象；固定480×800、静音。原APK/ELF不修改。

已修复D2：原TOWERS_fromIBS为104字节存档前缀只分配104字节，却写+0x68/+0x6c；tower-restore.hpp分配完整112字节运行对象，再走原读取/校验。F9及脚本恢复在帧边界先释放旧GAME再重建；不要用全局malloc扩容、跳过free或吞异常掩盖问题。

经验证的修复提交d5122b7；EXE SHA256为77d58c7c52f998fd3398da026e0d88f63729efb3f0d3274f32b5387475274eb1。实际源码/头文件/EXE对应关系看agent/codex/x86-reuse/current/build-manifest.json，勿以缓存哈希代替新验收。

已验证含塔活动局保存恢复、暂停冻结、跨进程继续及13次F9恢复/14次旧GAME释放；精简记录在agent/codex/x86-reuse/verification/baseline.json。旧首关Victory与116图绘制只保留历史摘要，不能算当前版本完整验收。

下一步优先修通原胜利页preferredLanguages接口及进入第二关的完整流程，再验证四战役/解锁、全部GUI和武器、真实倍速、跨地图/长时保存及独立包。原SCORES混用1970/2001纪元、robosteal_walk0..19模型缺失仍未闭合。

## 运行和工具

默认build/run目录agent/codex/x86-reuse/current；tools/x86-reuse/play.cmd进入原菜单。build.ps1冻结输入并默认clean-first；run.ps1绑定EXE/源码/脚本，缺少实际恢复/时钟推进步骤时失败。native0不等于验收成功。

F5/F9用menu-save-hotkey.bin，menuauto跨进程继续用menu-save.bin，不能互相冒充。测试用显式OutputDirectory，其evidence同级state与真实用户存档隔离；修改LOCALAPPDATA环境变量不能隔离旧重建SHGetKnownFolderPath。

必须保留：根目录两APK；agent/codex/x86-reuse的package、GS/GL JSON、libverde-elf.json、current、ghidra；assets；third_party。prepare-assets.mjs现在共享x86 package/assets，不再需要第二份ARM解包。

x86静态导出集中在agent/codex/x86-reuse/analysis；导出脚本在tools/x86-reuse/ghidra。已有Ghidra数据库NativeX86.gpr，用-readOnly -noanalysis顺序导出；Ghidra地址=ELF VA+0x10000。REA Windows provider不接受ELF，可直接用本地Ghidra headless。不要盲调未初始化的原objc运行库；MENU_setResolution是float，GAME_setResolution是int。

本机VS18 Community/MSVC14.51、Win32 CMake、JDK C:/java/jdk/21；Node为C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。agent/codex/tools中的REA/Ghidra/JADX是已注册工具运行时，不是调试垃圾，不可直接删除。

Android只保留TM_Original_API17_ARM_clean与对应SDK/镜像/原签名依赖，详情docs/ANDROID_REFERENCE.md；未启动模拟器或改变系统设置。可按需参考docs/reference和agent/codex/reference/arm的旧规格，非启动必读。

Windows PowerShell，不用gh；main默认不新建分支/worktree，代理通常127.0.0.1:7897。勿碰真实用户profile、其他服务或根TODO。用户原AGENTS改动不随手提交；任务文件不自行生成。过程放agent/codex，报告放agent/report。
