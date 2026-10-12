<!-- Static evidence for flame, lightning, consumables, and enemy status effects. -->
# Effect rules

以下为原 ARM 库静态事实，未进行真机执行。时间单位为游戏毫秒，距离为导航/世界单位。核心入口及指令位于 `agent/codex/p5-evidence/`；本次补充为 `agent/codex/p7-effect-evidence/`、`p7-effect-parsers/`、`p7-eligibility-evidence/` 和 `p7-effect-literals.tsv`。地图证据复用 `agent/codex/rules-evidence/map-full/`。地址为 Ghidra 地址，比 ELF 符号值高 0x10000。数值经配置、解析函数及关键 ARM 指令交叉检查；不能将伪代码的错误参数/浮点返回类型直接复制。

## 配置与攻击时序

四份 `assets/data/{Easy,Medium,Hard,Insane}.tech.json` 中下列武器参数相同；塔的价格、禁用状态、建造时间另按关卡 tech 读取。塔中心为 `(x+1,y+1)`，普通目标获取严格距离 `< range`，保留目标允许 `<= range`。未触发的 mine/bait 仍经过普通选敌、转向误差 `<5°` 和 `elapsed > reload` 的门限；触发后每步直接调用 fire，无需原目标仍存在。塔更新位于敌人移动之后，因此本步施加的状态从下步移动开始生效。

| 武器链 | 静态参数 |
| --- | --- |
| Flamethrower I / II / III | damage 8 / 60 / 245；radius 1.5；range 4 / 5 / 6；reload 25ms |
| ElectroShock I / II / III / IV / Doom Capacitor / Tesla Coil | damage 75 / 175 / 300 / 450 / 650 / 4444；jumps 2 / 5 / 8 / 11 / 15 / 15；range 4；前五级 reload 900ms，Tesla 800ms；仅 Tesla attracts=0.1 |
| ElectroSlow I / II / III / IV / Muscle Melter | duration 3000ms；factor .25 / .30 / .35 / .40 / .50；splash 1 / 1 / 1.5 / 1.5 / 1.75；range 6 / 6 / 6 / 7 / 7；reload 1000ms |
| ElectroBoost I–VI | boost .10 / .20 / .35 / .45 / .60 / 1.50；range 3 |
| Proximity Mine I / II / III | damage 500 / 5000 / 50000；splash 4 / 6 / 9；triggerdelay 2000ms；range 3；I、II仅地面，III地面和空中 |
| Flash Bait I / II / III | splash 4 / 6 / 9；stunduration 1000 / 2500 / 5000ms；slowduration 3000 / 10000 / 15000ms；factor .25 / .50 / .75；triggerdelay 3000ms；range 3；地面和空中 |

`reload=25` 配合原 25ms 步长及严格 `>` 门限，持续开火时实际最快每 50ms 一击。上述 damage 是每次发射基础值，不是每秒伤害。各类型的直接伤害仍走原护甲处理；光束热量规则见 [RULES.md](RULES.md)。

## 火焰与闪电

`flamethrowerFireFunc 002d2e80` 调用 `ENEMIES_damageEnemiesInLine 002996a8`：令方向向量 `v=target-center`，对敌人位移 `q=enemy-center`，必须满足 `cross(v,q)^2/|v|² < weapon.radius`、`dot(v,q)>0`、`dot(v,q)^2/|v|² < (1.5*range)^2`。这是一条前向有限宽带；radius 1.5 在解析时不平方，物理半宽为 `sqrt(1.5)`。所有合法空/地敌人收到同一原始整数 `trunc(damage*boost)`，没有径向衰减；共同伤害函数先护甲截断，再按各敌人 heat 相乘并截断。伤害后只对主要目标执行 `heat +=0.05` 的共同热量函数；火焰自身没有持续灼烧状态。

`ENEMIES_canTowerDamageEnemy 002998d0` 确认来源塔仍存在、敌人存活及空/地资格，isflame 为真返回 2，其余返回 1。`ENEMIES_damageEnemy 0029997c` 只在返回 2 时施加上述护甲后的 heat 乘数。普通 beam 的 heat 已在 fire 中、整数截断前相乘；原配置仅三个 flamethrower 塔设置 isflame，因此它们不能套用 beam 的截断顺序。Armor4 不接受 heat 增量，且 isflame 伤害为零。

`lightningFireFunc 002d5e08` 先对主要目标施加 `trunc(damage)`，没有 boost/heat 乘数。jumps 为包含首击的总命中上限。每一跳遍历活动链，选择距前一目标 XY 最近、严格距离 `< tower.range`、存活且空/地资格合法的未命中敌人；等距离保留链中先遇到者，不重复命中。

链式伤害累积浮点值 `D` 从配置 damage 开始。每跳取两个随机高度 `a,b=0.5+0.5*randFloat()`，计算 `L=sqrt(dx²+dy²+(next.z+b-prev.z-a)²)`；当前跳造成 `trunc(max(0,D*(1-L/range)))`，随后 `D *= max(0,1-L/range)`。距离候选筛选只用 XY，但衰减用上述 3D 特效距离，随机高度会影响实际整数伤害。`D` 保留浮点累积值，不能用上一跳已截断的伤害作为下一跳基值。

Tesla 首击向塔炮口 XY 方向拉动 attracts；第 k 个后续目标向同一炮口拉动 `2*attracts/pow(2.5,k)`（k 从 1 开始）。`ENEMIES_attractEnemy 0029a77c` 先检查 `trunc(enemyPos+unitDirection)` 在地图内且地面格 bit1 为 0，再保存旧位置并执行 `enemyPos += unitDirection*amount`。检查用完整单位方向，非实际位移；不按 `(state&9)` 路径判据检查。伤害先于拉动。

## 减速、眩晕与回血

`slowFireFunc 002db9b4` 在主要目标当前位置施加圆形减速，无直接伤害。`ENEMIES_slowEnemiesInRadius 00299ff4` 与 `ENEMIES_stunEnemiesInRadius 0029a1ac` 采用严格 XY 距离 `< splash`，没有护甲、空/地或死亡状态过滤；它们遍历活动链并检查 ID 对象仍存在。

`ENEMIES_slowEnemy 0029a12c` 的合并规则：

```text
if remaining > 0 and existingFactor > incomingFactor:
    remaining = max(remaining, incomingDuration)
else:
    remaining = incomingDuration
    factor = incomingFactor
```

因此弱减速可延长强减速；同强度或更强减速覆盖时长，可缩短旧时长。不能将两种状态一律实现为 `max(factor)` 加 `max(duration)`。眩晕只取 `remaining=max(old,incomingDuration)`，同时写入敌人的状态时间戳。

`ENEMIES_update 00296904` 先执行回血和 heat 冷却，再检查眩晕/减速与移动。眩晕时不移动但减速倒计时仍消耗；正常移动时，减速若步开始仍 `>0`，先扣 dtMs，再将本步速度乘 `1-factor`。不强制倒计时钳制到零，跨零这一步仍受影响。回血每严格超过 1000ms 一次；减速有效时回血量乘 `1-factor`，加到当前 HP 后截断为整数并不超过最大 HP，眩晕不单独阻止回血。heat 每步冷却仍为 `max(1,heat*(1-.05*dtSeconds))`。死亡分裂的两个子敌人继承父对象 slow 剩余时长/factor 及 stun 剩余时长，heat 重置为 1。

## 增幅与一次性道具

`TOWERS_updateBoosts 002b7ac0` 将所有塔 multiplier 重置为 1，再对 antiair=0 且 antiground=0 的塔调用 fire。`TOWERS_update 002b71f4` 在距上次重算严格超过 3000ms 时调用它。扫描没有建造/出售过滤，尚在建造或出售等待的 booster 仍可贡献，目标塔也没有此过滤。

`TOWERS_boost 002b7b74` 对严格 originDistance² `< booster.range²` 的目标执行 `multiplier += weapon.boost`，多塔相加、不相乘、没有上限。排除名字前缀 `ElectroSlow`（11字符）、`ElectroBoost`（12字符）、`Muscle`（6字符）及 isbait；前缀已在 `p7-effect-literals.tsv` 解析。Boost 不改变 reload/range。Flame 使用 multiplier，lightning/mine fire 函数没有读取它。

`mineFireFunc 002d73e4` 首次合法目标将 armedAt 从 -1 设为 now；`now >= armedAt+triggerdelay` 时，在塔中心调用 splash 伤害，再移除塔。伤害为 `trunc(trunc(baseDamage)*(1-distance/splash))`，严格范围 `< splash`，随后走护甲；不依赖原触发目标仍活着。`baitFireFunc 002cf66c` 同样触发，但爆发顺序为圆形 slow、圆形 stun、移除塔，没有直接伤害。

## 诱饵寻路

诱饵通过全局距离场改变目的地，不是 range 内直线吸引。`TOWERS_tryBuild 002b6754` 为 footprint 四格同时设 bit1 和 bit8；原通行判据 `(state&9)!=1`，因此这些格可走，但仍占塔位置。

`MAP_updateDistanceGrid 002a3350` 在 !fixedpath 地图中选择塔链遍历时最后一个完成建造且未出售的 isbait。`MAP_updateDistanceGridImpl2 002a2d94` 将该 footprint 四格全部设为距离 0，替代 entrance 种子，照原八邻接规则扩展；地面和飞行都使用诱饵目的地。建造期间尚不作为目标，完成后更新场；移除后清 footprint bit1/bit8 并恢复剩余诱饵或 entrance 目标。

建造检查除验证真实出口，还验证已有诱饵及新诱饵的可达性。`has_bait_tower 002b54a0` 为建造检查返回第一个未出售 isbait（可包含未完成建造）。`MAP_updateDistanceGridImpl 002a32c8` 对飞行场缓存只比较诱饵存在标记；多诱饵之间更换位置而标记不变时，原静态代码存在飞行场保留旧目的地的可能，尚无动态对照。

## 实现边界

已恢复上述玩法算法和配置；原 RNG 状态序列、准确炮口变换/随机高度、同帧死亡对象生命周期及多诱饵缓存边界没有真机对照。Windows 实现若使用不同 RNG、简化高度或重算策略，应明确列为差异，不应称为逐帧等价。原粒子、声音和视觉时间堆不在本次证据复现范围。
