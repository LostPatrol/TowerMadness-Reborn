<!-- Original-game reference tooling, artifact identities, and verified runtime limits. -->
# Android 原作对照环境

旧PlainPlane建造/选中及首波图片中的10金币绿色军用炮塔曾被误标为Laser；普通UI复核与原价格链确认该分支为Mortar。旧图片的升级25、未保存的短暂白线均不能用作Laser证据。原Laser10→20与Mortar10→25价格保持不变，依据见[UPGRADE_PRICE.md](UPGRADE_PRICE.md)；原菜单/地图/环境可运行结论不受塔种误标影响。

后续明确选择能量家族Laser的普通UI验证已取得建造/选中/升级图片：40→30→10，分别扣10/20，该局已有另一座Mortar。独立Mortar局为50→40→15。身份、完整连续操作及文件误名边界见[ORIGINAL_TOWER_IDENTITY.md](ORIGINAL_TOWER_IDENTITY.md)，不把混合塔局冒称Laser独占开局。

当前结论：默认API17 ARM官方镜像＋SDK25.2.5 classic＋原Google签名完整Phonesky4.3.11已实际运行未经修改的原ARM游戏，进入原作菜单、PlainPlane、建10金币迫击炮、选中范围与首波战斗。所有依赖均普通安装，未修改APK、签名、服务响应或镜像。官方API24 Google APIs x86仍因原libcxx.so/系统linker unwind链SIGSEGV未进入菜单；API24 Play旧设备仍unauthorized。没有启用WHPX、安装虚拟化驱动或重启Windows。

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
| API17 default ARM＋25.2.5 classic＋完整原签名Phonesky4.3.11 | 新隔离AVD5592正常安装依赖与ARM游戏，原作菜单、PlainPlane、10金币迫击炮建造/选中/首波战斗与Menu返回Resume已实际观察；旧5580有vending残留目录引发UID_CHANGED，不改其数据 |
| API17 Google APIs ARM＋25.2.5 classic | Android4.2.2可启动，主APK原生库加载并显示Loading；旧尝试因缺少启动服务而空Intent崩溃，未在此镜像重复完整依赖安装 |
| API17 Google APIs x86＋25.2.5 classic，无加速 | 系统的 system_server 在包证书扫描反复 SIGSEGV；未运行新 x86 游戏 |
| API24 Google Play x86＋37.2.12，无加速 | guestCPU android32、1536MB；host GLES 初始化到 RX9070XT Translator，软件模式为 SwiftShader；后续一直 offline，未形成有效 guest 启动证据；已停止 |
| API24 Google Play x86＋25.2.5 QEMU2，无加速 | 实际 Android 主屏可见；用户手动允许调试后，adb37/官方 adb29 及一次正常设备重启仍 unauthorized；2026-10-13 06:26 的 get-state 与 install-r 均失败，未安装或运行 x86 游戏 |
| API24 Google APIs x86 r27＋25.2.5 QEMU2，无加速 | 正常 adb29=device、userdebug、boot_completed=1、Android 主屏；原 x86 包实际安装并启动，MainThread 在系统 linker dl_iterate_phdr←原 libcxx.so _Unwind_Find_FDE 链 SIGSEGV/SEGV_ACCERR；未进菜单 |
| API24 default x86 r8＋25.2.5 QEMU2，无加速 | 正常 adb29=device、userdebug、PackageManager 启动；未安装/启动游戏，已正常停止，以集中测试已知能加载原生库的 ARM 路线 |

API17 Google APIs 自带 com.android.vending1.8，只提供许可服务，不能满足原启动器需要的 v3 服务。REA 定向启动证据 ev_fe993212dc9c91a90cf8ef606acbf522c92bc789fc9acf52c18c97c4ac4f23e2 表明服务无法唯一解析时返回 null；对应动态崩溃与该条件一致。没有继续分析购买功能。

从官方 API24 Play 镜像只读提取的 Phonesky6.7.15：minSdk14、target23、无 native 库、Google 证书 SHA1 38918A453D07199354F8B19AF05EC6562CED5788，文件 SHA256 337c7438f115dd3fa4ec8ad4c02b18b3c162466a347cf4b032943201951175e8。普通 adb install-r 在 API17 返回 INSTALL_PARSE_FAILED_INCONSISTENT_CERTIFICATES，未安装成功；没有重签、修补或修改镜像。该尝试不再作为已可用路线。

API24 Google APIs 同样内置 `/system/app/LicenseChecker`、com.android.vending1.8。对它普通安装该原签名 Phonesky 明确返回 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`，没有替换 stub 或修改证书。API24 x86 原生崩溃证据在 runtime/logs/TM_Original_API24_Google_x86_legacy/original-start-crash.txt，设备已正常停止。

旧默认API17 ARM普通安装官方镜像提取的Phonesky6.7.15返回 `INSTALL_FAILED_DEXOPT`：包内没有 `classes.dex`，是预优化系统组件。证据runtime/logs/TM_Original_API17_ARM/phonesky-install-logcat.txt；剩余存储充足。没有拼接ODEX、重新签名或修改镜像。此提取组件不再作为可安装候选；下述完整签名分发包已解决新隔离设备的启动依赖。

## 下载身份与官方依据

完整兼容依赖已从 [历史 GApps 分发仓库](https://github.com/DrMacinyasha/proprietary_vendor_google/blob/gh-pages/gapps-jb-20130812-signed.zip)取得，未将整个 GApps ZIP 安装或刷入设备。100751136 字节 ZIP 的 SHA256 为 `480a1bab87c05b1f7ef6a3643d93b83d2c3f42dbd48604834e99c741ee2550e7`；只读提取 `system/app/Phonesky.apk`，6038575 字节，SHA256 `04897ec9492ab119c137b18a021ec931f547c91faeb1d73d4a36b78592e2f03a`。该包为 Play Store4.3.11、minSdk8/target14，含4217764字节 `classes.dex` 和正常 `InAppBillingService.BIND` 声明。REA静态证据为 `ev_77f0fa3f3ff0fe3a9fbd913cad8d804769580dc1a596b67f8b82031be3031c2d`。

项目局部 Java JarFile 验证实际读完1089项非META-INF有效负载，全部JAR签名/摘要通过，每项Google证书SHA1均为 `38918a453d07199354f8b19af05ec6562ced5788`；未仅依赖分发站的签名声明。Java验证进程单独允许历史SHA1签名，不改变全局JDK配置、APK或网络TLS校验。验证器与原始候选位于runtime/downloads，结果位于runtime/logs/TM_Original_API17_ARM/phonesky-4.3.11-signature-verification.txt。

既有5580普通安装此完整包返回 `INSTALL_FAILED_UID_CHANGED`。包管理器无已安装vending，但 `/data/data/com.android.vending` 残留u0_a48目录；未删除该目录或改UID。使用同一官方镜像创建新隔离 `TM_Original_API17_ARM_clean:5592`，两包普通安装均Success，原菜单及PlainPlane已取得。旧5580正常emu kill停止且数据保留，旧Play5586仍保留。

复现已配置的新设备：设置进程局部 `ANDROID_SDK_ROOT=runtime/sdk`、`ANDROID_AVD_HOME=runtime/avd`，以 `runtime/sdk/tools/emulator.exe -avd TM_Original_API17_ARM_clean -engine classic -port 5592 -accel off -no-audio -no-snapshot -gpu host` 启动。用adb29显式 `-s emulator-5592`，待boot_completed=1后普通安装上述Phonesky和原ARM APK，再 `shell am start -n com.limbicsoftware.towermadnesszero/com.apportable.activity.VerdeActivity`。此AVD配置在runtime内；setup脚本的默认5580仍是旧设备。

实际PNG证据在runtime/logs/TM_Original_API17_ARM_clean：menu.png、plainplane-select.png、plainplane-initial.png、plainplane-build-choice.png、plainplane-selected-range.png、paused-menu.png及first-wave-frames/24张原帧。原UI显示V1.21，APKmanifest为1.22且摘要不变；按各自来源记录。普通UI50金币→建10金币迫击炮后40金币；选中显示退款10/升级25及范围。首波敌人/HP/塔转向、死亡爆炸、分数和金币变化已捕获；一次顶右箭头点击观察到白→绿，但未测定速度倍率。单塔局采样到第4波时羊降至0，没有胜利验收。此环境已可作为动态对照，尚未完成全表现/全玩法等价验证。

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

旧Play设备TM_Original_API24_Play_x86_legacy:5586与adb29保留，后续须按名称重新核验进程。host原RSA keypair未替换，未写guest keys或改安全设置；旧认证原因仍未确认。x86开发镜像原生崩溃尚未解决，当前可用动态对照来自ARM5592；不要混淆两包/两环境的运行证据。
