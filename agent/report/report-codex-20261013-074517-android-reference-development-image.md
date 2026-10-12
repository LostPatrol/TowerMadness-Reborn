<!-- Verified reference-runtime progress and remaining original-game startup blockers. -->
# Android 原作开发对照环境简报

## 结果

原作菜单、PlainPlane、建塔/详情及战斗动态对照仍未完成。取得了原 x86 包实际安装和运行崩溃证据，解决了一个环境的 adb transport 阻塞。没有修改任何原游戏 APK、签名、服务返回或 guest 安全配置；Windows 功能、WHPX、驱动、重启均未实施。模拟器仅供开发，最终产品仍为原生 Windows EXE。

## 已验证的实际进展

- 初始重新核验：旧 Play API24/QEMU2 的 PID21760/22884 与 adb29 PID30800 存活，5586 仍 unauthorized。
- 从 Google 官方目录取得 API24 Google APIs x86 r27，889204295字节，SHA1 `74505c33546fb9f6722fb7aa8fc1472777b924ff`，下载校验通过。
- 旧 SDK25.2.5/QEMU2/纯软件CPU/hostGPU启动独立5588，未经修改的官方 userdebug 镜像正常 adb=device、boot_completed=1、真实 Android 主屏。
- 原 `Madness_Zero-1.22.apk` 实际安装，pm 返回 `com.limbicsoftware.towermadnesszero`，正常 am start 后出现 “Madness Zero has stopped”。MainThread SIGSEGV/SEGV_ACCERR，系统 linker `dl_iterate_phdr` ← 原 `libcxx.so::_Unwind_Find_FDE`，未进菜单。不能将 Activity start 请求当作兼容成功。
- 该 x86 APK SHA256再次核验仍为 `7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd`。
- 官方 API24 default x86 r8（313489224字节，SHA1 `c1cae7634b0216c0b5990f2c144eb8ca948e3511`）也正常 adb=device/PackageManager 启动，未安装或启动游戏。为集中验证已有原生加载证据的 ARM 路线，正常关闭5588和5590。仅准备官方 API23 default x86 r10（260804863字节，SHA1 `f6c3e3dd7bd951454795aa75c3a145fd05ac25bb`），未运行，不能推断其兼容性。

## 启动依赖的实际限制

官方 Play 镜像只读提取 Phonesky6.7.15 仍为原 Google 签名 artifact，SHA256 `337c7438f115dd3fa4ec8ad4c02b18b3c162466a347cf4b032943201951175e8`。API24 Google APIs 内置不同签名的 LicenseChecker1.8，普通安装返回 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`，没有替换它。

重启既有默认 API17 ARM/5580后，adb=device、boot_completed=1、原 ARM 游戏已安装且没有 vending stub。普通安装同一 Phonesky 返回 `INSTALL_FAILED_DEXOPT`。实际日志和 ZIP inventory 表明它没有 `classes.dex`，属于预优化系统组件，不能作为完整普通安装 APK。剩余存储充足。这纠正了仅依据 minSdk14 就认为提取组件可普通安装的假设。没有拼接ODEX、重建APK或重新签名。

后续需要可合法取得的 Google 原签名完整兼容 APK，验证 signer `38918a453d07199354f8b19af05ec6562ced5788`、完整JAR签名/每项摘要和 classes.dex 后再普通安装。当前尝试的分发源：APKMirror6.7.07页面可读（16633229字节、公开SHA256 `6843a303d8a398a361239d048755a7e74bf94351645aa6c738e8228a1d66228e`），但直接页面403、缓存下载nonce过期，IAB状态调用timeout；ApkFiles原版5.4.12下载429；OpenGApps项目SourceForgeARM4.4pico下载TLS EOF。未取得或安装这些完整分发APK，不能借站点声明声称签名已验证。

最后按父代理建议使用项目Node22.23.1/OpenSSL，通过原HTTP代理正常CONNECT及 `rejectUnauthorized:true` 请求同一SourceForge包；仍在TLS建立前 `ECONNRESET`。过程脚本在runtime/downloads/download-openssl.mjs，没有依赖或Windows配置变更。已停止对失败来源的重复请求，没有关闭TLS校验或绕过403/429/权限界面。

## 文件与验证

`tools/setup-android-reference.ps1` 新增 `-DevelopmentImage -LegacyAdb` 官方API24Google开发镜像路径；旧QEMU2用进程局部QEMU_AUDIO_DRV静音，保留无硬件加速。修复 Windows PowerShell5 对hashtable `Measure-Object -Property Size`不兼容问题。setup-only实际通过；LaunchOriginal曾发起真实安装，因慢速编译而停止客户端后游戏最终仍由普通PackageManager安装，脚本本轮被终止的客户端返回错误，不美化成完整自动运行通过。

开发文档已更新相应实际结论。没有改SRC/MEMORY/README/TODO，无git commit/push。

证据位于：

- `agent/codex/android-reference/runtime/logs/TM_Original_API24_Google_x86_legacy/original-start-crash.txt`
- 同目录 `android-properties.txt`、`normal-install-logcat.txt`、`install-latest-logcat.txt`、emulator stdout/stderr。
- `agent/codex/android-reference/runtime/logs/TM_Original_API17_ARM/phonesky-install-logcat.txt`
- 各官方目录XML、原始下载及download-identities JSON均在runtime内。

07:45重新核验存活实例共2个：旧Play5586 QEMU22884（unauthorized）和默认ARM5580 emulator-arm28248（device）；adb server30800。PID只作为此次观察，后续须重新核验，不应复用为活跃证明。保留ARM设备便于取得完整签名依赖后继续，不把当前Android主屏或原作Loading当作菜单保真验收。
