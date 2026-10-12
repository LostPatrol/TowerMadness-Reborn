<!-- Projectile recovery scope, implementation, verification, and remaining limits. -->
# P5 投射物规则恢复与实现

执行者：codex-projectile_rules。日期：2026-10-13。作为主任务 P5–P7 并行子任务执行；不运行手机、模拟器或音频。

恢复了 plasma、railgun、homing、mortar、flak 及共享圆形/直线伤害规则。补充导出 projectile create、预测器、武器解析器、单/双炮口变换、塔角度和转向函数，均以 Ghidra `-readOnly -noanalysis` 读取已有数据库；未修改 APK、原 SO 或数据库内容。首次只读导出因另一子任务持有项目锁失败，协调后导出完成。

新增 `docs/PROJECTILE_RULES.md`，记录原函数地址、字段、精确分支、常量和未验证项。通过原 ELF PT_LOAD 段和已验证的 Ghidra +0x10000 映射读出相关常量，证据位于 `agent/codex/p5-evidence/projectile-literals.json`；读取脚本位于 `agent/codex/projectile-literals.mjs`。关键发现包括炮口变换使用负角度常量、追踪转弯角随累计距离增加、flak 不乘塔伤害加成、rail radius 直接与横向距离平方比较、范围伤害线性衰减、核弹下落阶段转为追踪、H Bomb 先加热后伤害。

在主代理明确授权后新增 `src/projectile.cpp`，实现 Game::muzzle、Game::ballisticLaunch、Game::updateProjectiles。接入源码清单、发射创建、伤害和存档由主代理负责。已向主代理给出字段约定、弧线公式、原预测器毫秒系数、homing 随机采样顺序及核弹状态初始化要求。

验证：使用本机 Visual Studio 18 C++ 工具链对该文件执行 `/std:c++20 /EHsc /W4 /utf-8 /c`，两次编译均退出 0，无编译警告。比较 Easy/Medium/Hard/Insane 五类武器配置，数字参数差异数均为 0。主代理随后确认整合编译通过；完整游戏运行检查由主任务统一进行，本报告不将编译作为完整游戏验证。另定向读取投射物模型加载函数，并通过 ELF 常量确认 guided missile 使用 grenade，ispulsemissile 使用 pulseburst；普通炮弹模型为 grenade/Mortar I 和 mortarshell/其他，核弹分别为 nukemissile、hbombmissile。

未达到或未验证：没有原安卓动态对照；原始逐位 ARM 浮点行为、极端共线反向追踪轴、平滑炮口俯仰时间未完全等价。当前桌面炮口俯仰直接取目标，极端退化向量保留有限值。已在规则文档中明确这些限制。主代理维护共享 MEMORY.md 时应加入该规则文档入口和上述限制，避免并行覆盖其正在更新的基线。
