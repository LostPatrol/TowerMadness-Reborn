<!-- Immutable APK identities, asset equivalence, and Android reference boundaries. -->
# APK 对照基线

最终交付仍为 Windows 原生程序；Android 模拟器仅用于开发期观察原作。

## 包身份

| 项目 | 既有 ARM 包 | 新增 x86 包 |
| --- | --- | --- |
| 文件 | `TowerMadness+Zero_+3D+TD_1.22_APKPure.apk` | `Madness_Zero-1.22.apk` |
| 大小（字节） | 44,479,763 | 47,715,178 |
| MD5 | `958a657e17ccdc11f44fd3cedc8ec816` | `ecba0323d61e3c618a01a6ed5fa025e5` |
| SHA-256 | `0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b` | `7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd` |
| 包名 | `com.limbicsoftware.towermadnesszero` | 同左 |
| versionName | `1.22` | `1.22` |
| versionCode | `1444639614` | `1444641171` |
| minSdk / targetSdk | 9 / 17 | 9 / 17 |
| 原生库 | 23 个 `lib/armeabi/*.so` | 23 个 `lib/x86/*.so` |
| 实际 ELF 头 | ELF32，`e_machine=40`（ARM） | ELF32，`e_machine=3`（Intel 80386） |

新增包的大小、MD5、版本号与用户提供的下载页面记录吻合。ABI 结论来自全部原生库 ELF 头，不仅来自目录名。两包均无其他 ABI 的原生库。

## 资源与规则

两包各有 2159 个非目录 ZIP 成员、1944 个 assets 成员。逐成员解压内容 SHA-256 比较：

- 1943 个 assets 的路径及内容完全一致，包括 116 个 `.gamemode`、89 个 `.waves`、4 个 `.tech`、118 个 `.map`、321 个 `.model`、202 个 `.vbo`、422 个 `.png` 和 36 个 `.caf`。
- 唯一 assets 差异为 `Info.plist` 的 `APVersionCode`，由 `1444639614` 改为 `1444641171`。
- `classes.dex`、全部 Android `res/` 与 `resources.arsc` 内容一致。DEX SHA-256 为 `488fdb3171592199751d516d9c1f9bc98355e4acaea67b748d74d1b13bcbc7a0`。
- 其他差异为 `AndroidManifest.xml`、三项 `META-INF/` 签名成员及按 ABI 替换的 23 个原生库。

因此现有游戏资源/配置基线无需更换。继续用 ARM 包及既有 Ghidra 数据库追溯已恢复规则；当前成功的动态对照使用原ARM包与API17环境，新x86包在API24仍有原生崩溃，见[ANDROID_REFERENCE.md](ANDROID_REFERENCE.md)。跨 ABI 的原生代码行为尚未逐函数证明等价；资产相同也不证明运行表现完全相同。

## 签名与证据边界

JDK 21 `keytool -printcert -jarfile` 读取两包得到同一签名证书：

- Subject：`CN=Limbic Software, OU=Limbic Software, O=Limbic Software, L=Palo Alto, ST=California, C=CA`。
- 证书 SHA-1：`CD1B31A61FB8A7C69E5C60F4B865093AA93D2C76`，与用户提供记录吻合。
- 证书 SHA-256：`FAFAEC17E115A8F88CC9203A19FFCB4E16E3F7E0EB595F1EE8FB04F099DD4F2B`。

现代 JDK 默认禁用两包的旧 SHA1 JAR 签名。在显式单次验证进程中仅放宽该历史算法限制，`jarsigner -verify` 对两包均返回 `jar verified.`、退出码 0。仍报告自签名证书链不受信、无签名时间戳、弱算法及 JarFile/JarInputStream 签名读取差异；不据此宣称现代 APK 签名方案、发布来源或 Android 安装兼容性已经验证。未修改原包或全局 Java 安全配置。

REA 静态包证据：ARM `ev_fa740f1b56e1de223d38b3a4be5643a13226b2ca4469f48a607b48ad30625652`；x86 `ev_297109a3925c46f95537786c13946f3334ddf9e0a9f229ed6ba57c92035e9cf5`。成员清单及内容哈希保存在 `agent/codex/x86-package-compare/comparison.json`，复查脚本为同目录 `compare-apks.ps1`。本比较不包含启动或玩法观察。
