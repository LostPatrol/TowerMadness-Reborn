<!-- P7 effect-rule evidence, implementation scope, and verification limits. -->
# P7 效果规则恢复简报

本子任务恢复火焰、闪电、减速、眩晕、增幅、地雷与诱饵的静态玩法规则；新增 `docs/EFFECT_RULES.md` 和 `src/effects.cpp`，后者实现 additive boost、slow 合并及 Tesla 拉动碰撞检查。随后按主代理明确分工，修正 `src/combat.cpp` 的 lightning 分支（随机高度、浮点累积伤害、固定炮口方向拉动、链顺序）及 `src/game.cpp` 的状态计时/减速回血。其余战斗、导航、构建接线由主代理负责，没有修改 TODO、购买/广告、音频或原 APK。

证据复用原 `p5-evidence`、map-full、beam、heat；三批 Ghidra `-readOnly -noanalysis` 导出到 `p7-effect-evidence`、`p7-effect-parsers`、`p7-eligibility-evidence`，常量解析在 `p7-effect-literals.tsv`。六个武器解析函数及六个补充玩法函数、damage eligibility 函数均导出成功。初次导出因并发数据库锁退出，协调后成功，未删锁或改数据库。

关键结果：火焰宽度比较的是平方垂距与原 radius，伤害先护甲截断后乘目标 heat；闪电跳跃使用 XY 寻近但 3D 随机高度衰减，total jumps 包含首击；slow 同强度/更强覆盖时长而弱效果仅延长；boost 多塔相加，包含建造/售卖等待中的 booster；诱饵以 footprint 四格替代整个地面/飞行距离场目的地。分裂子敌人继承 slow/stun 而重置 heat。

校核范围：四份 tech 的相关武器参数逐项读取并一致；解析、指令、PC 相对字符串与库 ELF section 映射交叉确认；`git diff --check` 通过。主代理确认首轮构建包含 effects/projectile/catalog 且成功；后续 lightning 和状态补正仍交由主代理最终统一编译及完整仿真验收，本子任务不宣称已完成独立运行/真机对照。

未解决指标：没有安卓真机，无法确认原 RNG 初态、精确炮口/高度、同帧死亡链及多诱饵飞行场缓存的动态等价；现有安全遍历架构暂存分裂子敌人，导致原作同次闪电可能命中的新生子敌人尚不能加入该次候选链；原粒子/声音不在此次静态复现范围。最终 MEMORY 基线由主代理合并本轮全项目修改后统一维护。
