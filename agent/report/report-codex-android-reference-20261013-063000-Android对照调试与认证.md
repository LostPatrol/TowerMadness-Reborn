<!-- Android reference environment checkpoint and unresolved authentication result. -->
# Android 原作对照检查点

已完成项目内官方 SDK25.2.5、Emulator37.2.12、adb37.0.1/29.0.6 与 API17 ARM/x86、API24 Play x86 镜像的下载校验和隔离安装。下载身份及复现参数见 docs/ANDROID_REFERENCE.md；安装入口 tools/setup-android-reference.ps1 新增 -LegacyAdb，并明确报告 unauthorized，不把安装/启动请求视为游戏验收成功。

主 ARM APK SHA256 为 0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b，versionCode1444639614。API17 Google ARM 对照已显示原 Loading，但菜单前崩溃。截图：agent/codex/android-reference/launch-api17-google-arm.png。

用户新指定 x86 APK SHA256 为 7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd，versionCode1444641171，真实 ELF32 x86 库。API24 Play＋旧 QEMU2 在纯软件模式正常到 Android 主屏；用户手动允许调试后，旧 adb29 及一次正常设备重启仍 unauthorized。2026-10-13 06:26 最后一次 get-state exit1、install-r 失败，证据：agent/codex/android-reference/runtime/logs/x86-api24-final-attempt-20261013-062654.log。

唯一保留设备 TM_Original_API24_Play_x86_legacy:5586，检查点 QEMU PID22884、adb29 server PID30800。旧客户端路径：agent/codex/android-reference/runtime/sdk/platform-tools-29/platform-tools/adb.exe。原 host keypair 已只读验证匹配，无 key 或 guest 安全设置修改；失败原因未确认，不足以断言 APK 与 Android Studio 不兼容。

未取得原菜单、PlainPlane、建造、塔详情或战斗画面，动态保真验收未完成。重复无新增证据的轮询已停止，失败实验已停止；保留该环境供后续正常认证诊断。没有改系统 PATH、启用 WHPX、安装虚拟化驱动、重启 Windows、修改原 APK、登录 Google 或修改广告/购买行为。未修改游戏源码或 MEMORY.md。
