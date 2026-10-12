<!-- Read-only APK identity, architecture, content comparison, and verification limitations. -->
# 双 APK 基线核对

执行 agent：codex-x86-package-compare；日期：2026-10-13。

## 已完成

- 对用户指定的新旧 APK 核对大小、MD5、SHA-256。新包的 47,715,178 字节及 MD5 `ecba0323d61e3c618a01a6ed5fa025e5` 与用户记录一致。
- 完整读取两包各 2159 个非目录 ZIP 成员，无重复成员路径。新包 23 个原生库全部是 ELF32 x86；旧包 23 个原生库全部是 ELF32 ARM。
- 对每项解压内容计算 SHA-256。1944 个 assets 中 1943 个内容一致；唯一差异为 `Info.plist` 的版本号。规则、地图、模型、纹理、声音文件不需要换基线。
- 确认相同 `classes.dex` 和 Android 资源，同一证书 SHA-1 与用户记录相符。显式历史 SHA1 验证配置下两包均 `jar verified.`，退出码 0。
- 新增开发文档 `docs/APK_BASELINES.md`，完整清单、比较脚本及签名输出放在 `agent/codex/x86-package-compare`。

## 边界与未完成项

此子任务未启动 APK，不能宣称新包已可运行，也未逐函数证明 ARM/x86 原生代码等价。签名验证仍报告自签名证书链不受信、无时间戳、弱算法与 JarFile/JarInputStream 差异。现代 JDK 默认配置会把历史 SHA1 签名视为禁用；放宽仅应用在指定 verifier JVM，未改全局 Java 配置。

建议保留既有 ARM Ghidra/规则基线，优先让开发期对照环境尝试新增 x86 包。最终 Windows 原生交付边界不变。未修改任何原 APK；原包 SHA-256 复核一致。未使用 Ghidra 共享库、未写游戏代码、未改 TODO。

MEMORY 由父 agent 统一维护，以免并行覆盖；已向父 agent 提供两包身份、内容等价范围与开发期模拟器授权的持久信息。
