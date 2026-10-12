<!-- Original-game reference tooling, artifact identities, and verified runtime limits. -->
# Android 原作对照环境

当前结论：已安装隔离 SDK 和官方镜像，未修改任何原游戏 APK，尚未进入原作菜单或 PlainPlane。原 ARM 包曾加载原生库并显示 Loading 封面。官方 API24 Google APIs x86 r27 开发镜像配旧 QEMU2，正常 adb29 transport 为 device，已实际安装并启动原 x86 APK；它在原 libcxx.so/系统 linker 的 unwind 调用链发生 SIGSEGV，未进入菜单。API24 Play 镜像的旧设备仍 unauthorized。没有启用 WHPX、安装虚拟化驱动或重启 Windows。

## 目标与原作候选

模拟器仅用于开发对照，Windows 发布仍为原生 EXE。广告、购买、联网排行不作为重建目标；本次只定位阻止游戏进入菜单的启动依赖，没有修改相关游戏代码或服务返回。

| APK | 已验证身份 |
| --- | --- |
| TowerMadness+Zero_+3D+TD_1.22_APKPure.apk | 原 ARM 静态基线；SHA256 0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b；23 个 lib/armeabi 库 |
| Madness_Zero-1.22.apk | 用户新指定的 x86 候选；47,715,178 字节；MD5 ecba0323d61e3c618a01a6ed5fa025e5；SHA256 7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd；23 个 lib/x86 库，libverde ELF32/e_machine=3 |

两包均为 com.limbicsoftware.towermadnesszero、1.22、minSdk9/targetSdk17、竖屏 Apportable/Verde 程序；ARM 包 versionCode1444639614，x86 包 versionCode1444641171。新 x86 包的证书 SHA1 实测为 CD1B31A61FB8A7C69E5C60F4B865093AA93D2C76，与用户提供值一致；不是基于文件名猜测架构，也不应称其为修改包。不同 APK 的动态证据须分别标明来源。

主 ARM manifest 证据：REA ev_43a81ed26bcfe394b845235ba9ac06830733185e4088d3d14a196dbdbcd170a1。新 x86 manifest 证据：ev_0672ab4e1c6bcb2f0e81c8310c8d7437817ecba3e5c855e2a7549d5c1c10383b。它们是静态证据，不证明运行成功。

## 当前工具与目录

所有对照内容位于 agent/codex/android-reference/，不属于游戏发布资产。runtime/sdk/tools 是 SDK Tools/Emulator25.2.5（build3567187）；runtime/sdk/emulator 是 Emulator37.2.12（build16428233）；runtime/sdk/platform-tools/adb.exe 为 adb37.0.1，runtime/sdk/platform-tools-29/platform-tools/adb.exe 为 adb29.0.6，当前旧设备使用后者。runtime/avd 保存独立设备，runtime/downloads 保存已校验包，runtime/logs 保存运行证据。archive-tools 是只解包、未系统安装的官方 7-Zip26.04，用于只读检查 Ext4 镜像。

本机为 Windows11 Pro10.0.26200、i5-12600KF、约64GB内存。BIOS VT-x/EPT 已开，HypervisorPresent=false；WHPX 可选功能查询需要管理员，状态未进一步确认。现有系统代理为127.0.0.1:7897；没有改系统 PATH、其他开发环境或本机服务配置。

## 安装与复现入口

tools/setup-android-reference.ps1 默认仅准备项目内 API17 Google APIs ARM 环境；-LaunchOriginal 才会启动对应 AVD、校验原 APK 身份并发送安装/启动请求。-Architecture x86 选择用户提供的新包及 API17 x86 镜像；-PlayStore 配置 API24 Google Play/当前 Emulator；-LegacyAdb 选择隔离的官方 adb29.0.6。脚本遇到 unauthorized 会明确停止安装请求并保留设备。后两镜像路线的限制见下表，安装完成、Activity start 请求均不能作为原菜单成功的证明。

`-DevelopmentImage -LegacyAdb` 准备官方 API24 Google APIs x86 r27 与旧 QEMU2，设备 TM_Original_API24_Google_x86_legacy/5588。该未改动的 userdebug 镜像正常提供开发 adb transport；没有注入 guest 授权密钥或修改 adbd。加 `-LaunchOriginal` 才安装启动原 APK。此路线解决了 transport 阻塞，但已观察到下表所列原生崩溃，不是已可用游戏环境。

脚本采用官方固定 URL/摘要、显式代理、独立端口、进程局部 SDK 环境变量和无音频启动；没有自动启用硬件加速。旧启动器的 image.sysdir.1 必须为 SDK 相对路径，API17 ARM 要显式 -engine classic。

已到 RSA 授权阶段的路线是旧 SDK25.2.5 QEMU2 与 API24 完整 Google Play 镜像。现有 AVD 名称 TM_Original_API24_Play_x86_legacy，目标端口5586。后续可从项目根目录的 PowerShell 终端复现：

```powershell
$referenceRoot = Join-Path (Get-Location) 'agent/codex/android-reference/runtime'
$env:ANDROID_SDK_ROOT = Join-Path $referenceRoot 'sdk'
$env:ANDROID_AVD_HOME = Join-Path $referenceRoot 'avd'
$env:QEMU_AUDIO_DRV = 'none'
& (Join-Path $referenceRoot 'sdk/tools/emulator.exe') -avd TM_Original_API24_Play_x86_legacy -engine qemu2 -port 5586 -accel off -no-snapshot -gpu host -show-kernel
```

这些变量只影响该终端及其子进程。该 AVD 已禁用 hw.audioInput/Output；旧 QEMU2 不接受旧包装器由 -no-audio 生成的 -audio 参数，故以 QEMU_AUDIO_DRV=none 保持静音。保留原镜像及 AVD 配置，避免为了完成截图改写游戏或 guest 授权密钥。

## 已证实的运行限制

| 对照环境 | 实际结果 |
| --- | --- |
| API17 default/Google APIs ARM＋25.2.5 classic | Android4.2.2可启动，ABI=armeabi-v7a/armeabi；主 APK 安装及原生库加载成功，Google 版显示原 Loading；进入菜单前因缺少启动服务而空 Intent 崩溃 |
| API17 Google APIs x86＋25.2.5 classic，无加速 | 系统的 system_server 在包证书扫描反复 SIGSEGV；未运行新 x86 游戏 |
| API24 Google Play x86＋37.2.12，无加速 | guestCPU android32、1536MB；host GLES 初始化到 RX9070XT Translator，软件模式为 SwiftShader；后续一直 offline，未形成有效 guest 启动证据；已停止 |
| API24 Google Play x86＋25.2.5 QEMU2，无加速 | 实际 Android 主屏可见；用户手动允许调试后，adb37/官方 adb29 及一次正常设备重启仍 unauthorized；2026-10-13 06:26 的 get-state 与 install-r 均失败，未安装或运行 x86 游戏 |
| API24 Google APIs x86 r27＋25.2.5 QEMU2，无加速 | 正常 adb29=device、userdebug、boot_completed=1、Android 主屏；原 x86 包实际安装并启动，MainThread 在系统 linker dl_iterate_phdr←原 libcxx.so _Unwind_Find_FDE 链 SIGSEGV/SEGV_ACCERR；未进菜单 |
| API24 default x86 r8＋25.2.5 QEMU2，无加速 | 正常 adb29=device、userdebug、PackageManager 启动；未安装/启动游戏，已正常停止，以集中测试已知能加载原生库的 ARM 路线 |

API17 Google APIs 自带 com.android.vending1.8，只提供许可服务，不能满足原启动器需要的 v3 服务。REA 定向启动证据 ev_fe993212dc9c91a90cf8ef606acbf522c92bc789fc9acf52c18c97c4ac4f23e2 表明服务无法唯一解析时返回 null；对应动态崩溃与该条件一致。没有继续分析购买功能。

从官方 API24 Play 镜像只读提取的 Phonesky6.7.15：minSdk14、target23、无 native 库、Google 证书 SHA1 38918A453D07199354F8B19AF05EC6562CED5788，文件 SHA256 337c7438f115dd3fa4ec8ad4c02b18b3c162466a347cf4b032943201951175e8。普通 adb install-r 在 API17 返回 INSTALL_PARSE_FAILED_INCONSISTENT_CERTIFICATES，未安装成功；没有重签、修补或修改镜像。该尝试不再作为已可用路线。

API24 Google APIs 同样内置 `/system/app/LicenseChecker`、com.android.vending1.8。对它普通安装该原签名 Phonesky 明确返回 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`，没有替换 stub 或修改证书。API24 x86 原生崩溃证据在 runtime/logs/TM_Original_API24_Google_x86_legacy/original-start-crash.txt，设备已正常停止。

默认 API17 ARM 没有 vending stub，原 ARM 游戏已安装、adb=device、boot_completed=1。普通安装同一官方镜像提取的 Phonesky6.7.15 返回 `INSTALL_FAILED_DEXOPT`：包内没有 `classes.dex`，是预优化系统组件，不能把它当作完整可普通安装的分发 APK。证据 runtime/logs/TM_Original_API17_ARM/phonesky-install-logcat.txt 中 `DexOptZ: zip archive ... does not include classes.dex`；剩余存储充足。没有拼接 ODEX、重新签名或修改镜像。需要另外取得 Google 原签名、完整含 dex 的兼容 APK，并验证签名和全部 JAR 摘要后再普通安装，才能继续该原生加载已知可用路线。未进入菜单的指标保持未完成。

## 下载身份与官方依据

| 固定包 | 官方 SHA1 |
| --- | --- |
| tools_r25.2.5-windows.zip | a7f7ebeae1c8d8f62d3a8466e9c81baee7cc31ca |
| API17 default ARMv7 r6 | a18a3fd0958ec4ef52507f58e414fc5c7dfd59d6 |
| API17 Google APIs ARMv7 r6 | a59f26cb5707da97e869a27d87b83477204ac594 |
| API17 Google APIs x86 r7 | 1ad5ffb51e31f5fe9fa47411fed2c2ade9a33865 |
| API24 Google Play x86 r19 | b52e9593ffdde65c1a0970256a32e8967c89cc22 |
| API24 Google APIs x86 r27 | 74505c33546fb9f6722fb7aa8fc1472777b924ff |
| API24 default x86 r8 | c1cae7634b0216c0b5990f2c144eb8ca948e3511 |
| API23 default x86 r10（仅准备，未运行） | f6c3e3dd7bd951454795aa75c3a145fd05ac25bb |
| emulator-windows_x64-16428233.zip | 488ed747e82de7e9bb5247becd1ac043c7e5e85d |
| platform-tools_r37.0.1-win.zip | e03e78b1d80b396f1c3358e31251cb31740e1110 |
| platform-tools_r29.0.6-windows.zip | c970e1216ef99abf9fd480b01439998c4780728e |

精确 URL 在安装脚本；各次下载实际 SHA256 在 runtime/download-identities*.json。API24 Play ZIP 的 SHA256为8056019893dffdf3d72c5ba3c207f3f0305cab7dd855933472df53cc8e2e53ff，Emulator37 Windows ZIP为3b524b6e0f1f5506d3d81af62ec9ce15b2a6f80f467e99574e167514ca181401。上述下载全部通过官方 SHA1校验。

官方来源：[旧工具仓库](https://dl.google.com/android/repository/repository-12.xml)、[当前工具仓库](https://dl.google.com/android/repository/repository2-3.xml)、[AOSP镜像](https://dl.google.com/android/repository/sys-img/android/sys-img2-3.xml)、[Google APIs镜像](https://dl.google.com/android/repository/sys-img/google_apis/sys-img2-3.xml)、[Google Play镜像](https://dl.google.com/android/repository/sys-img/google_apis_playstore/sys-img2-3.xml)。

当前 Emulator 不直接支持 classic ARM32；旧镜像的存在不代表新版能启动它。[官方启动器源码](https://android.googlesource.com/platform/external/qemu/+/emu-master-dev/android/emulator/main-emulator.cpp) 官方 Windows 推荐 WHPX，要求 Windows10 1803+、BIOS VT-x/EPT，并可能需要启用功能后重启；本轮没有执行。软件旧 QEMU2 已进入认证阶段，因此不能据此断言必须开启 WHPX。[官方加速说明](https://developer.android.com/studio/run/emulator-acceleration)

旧 Play 设备 TM_Original_API24_Play_x86_legacy:5586 与 adb29 保留；后续须按名称重新核验进程，不能将历史 PID 当作存活证明。host 原有 RSA keypair 未替换，未写 guest keys 或改安全设置；认证失败原因仍未确认。旧认证证据为 runtime/logs/x86-api24-final-attempt-20261013-062654.log。新开发镜像已经实际推进到原 x86 应用崩溃，仍须继续验证原菜单、PlainPlane、建造、塔详情和战斗，当前这些画面均未取得。
