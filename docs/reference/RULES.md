<!-- Static gameplay evidence and reconstruction boundaries for the Windows sample. -->
# Recovered gameplay rules

> 旧 x64 重建与原作静态规格参考；当前主线见 [X86_CORE_REUSE.md](../X86_CORE_REUSE.md)。原 ARM 地址不能直接用于 x86。旧运行过程已清理，保留的静态导出在 `agent/codex/reference/arm/`。

本文件区分原库静态事实、复现推断与尚未验证的行为。Ghidra伪代码是分析证据，类型和浮点返回值须结合指令阅读，不能直接当源码编译。本文保留 PlainPlane 核心入口；新增武器见 PROJECTILE_RULES.md，状态／诱饵／增幅见 EFFECT_RULES.md，无限／解锁／计分见 MODES.md。实际覆盖需在当前候选重新验证。

## 样本与证据

样本为 `PlainPlane.gamemode`、`PlainPlane.map`、`PlainPlane.waves`、`Easy.tech`：初始50金币、10只羊，26波，包含飞行和分裂敌人。读取全部26波，不能把前几波演示称为完整关卡。

原文件位于 `agent/codex/x86-reuse/package/assets/`。原库SHA-256所属APK基线见根目录MEMORY。复用 `原 ARM 数据库（已清理，需要时从原 APK 重建）`，只读导出，无重分析或原库改写。

| 证据目录（相对项目根目录） | 核心入口 |
| --- | --- |
| `agent/codex/reference/arm/rules-evidence/map-full/` | MAP_load 002a3a08、MAP_prepareRendering 002a9ee8、MAP_updateDistanceGridImpl2 002a2d94、MAP_getNextMoveTarget 002a34a4、TOWERS_canBuildHere 002b551c、TOWERS_tryBuild 002b6754 |
| `agent/codex/reference/arm/rules-evidence/map-literals.tsv` | PC相对常量：fixedpath、pads、entrance、enclosure、8方向邻接及权重 |
| `agent/codex/reference/arm/rules-evidence/simulation/` | GAME_timestep 0029df78、GAME_update 0029e184、ENEMIES_sendNextUFO 00297638、ENEMIES_spawn 00297c24、ENEMIES_update 00296904、ENEMIES_checkWin 0029a994 |
| `agent/codex/reference/arm/rules-evidence/combat/` | GAME_alloc 0029b8e0、TOWERS_update 002b71f4、TOWERS_tryUpgrade 002b6b38、TOWERS_sell 002b6d34、ENEMIES_getTargetForTower 002994bc、WAVES_load 002bc70c |
| `agent/codex/reference/arm/rules-evidence/beam/` | beamFireFunc 002d05cc、ENEMIES_damageEnemy 0029997c、ENEMIES_damageThroughArmor 0029a560、resolveTowerType 002b3b14、TOWERS_smoothTurn 002b4928 |
| `agent/codex/reference/arm/rules-evidence/heat/` | ENEMIES_heatEnemy 0029a38c、GAME_getSpeedUpFactor 002a0090 |
| `agent/codex/reference/arm/rules-evidence/lifecycle/` | ENEMIES_alloc 002964a0、ANIMALS_isFinished 0029552c、TOWERS_muzzleOffsetTransform 002b4ae8、WEAPON_directAimWithZ 002dd9c8 |

每批保存 `pseudocode.c` 和 `instructions.tsv`；常量解析保留在对应 `*-literals.tsv`。地址为Ghidra地址；本次导入比ELF符号值高0x10000。脚本为 `TowerMadnessRules.java` 与 `TowerMadnessLiteralData.java`；Windows批处理会拆分逗号参数，前者兼容多个独立函数参数。

## 世界、地图与寻路（静态事实）

terrain宽11、高16，但导航网格和世界范围为22×32。每terrain方格边长2世界单位，按行索引 `terrain[y*width+x]`，复制到导航2×2四格，无翻转。导航格中心为 `(x+0.5,y+0.5)`；塔占2×2导航格，存储左下/原点 `(x,y)`，中心 `(x+1,y+1)`。塔可放在奇数导航坐标，不能强制对齐terrain。

| terrain值 | 导航初值 | 移动 | 建塔 | 默认绘制材质 |
| --- | --- | --- | --- | --- |
| 0 | 2 | 可走 | 不可建 | mud |
| 1 | `fixedpath != 0`，缺省0 | fixedpath缺省可走 | 缺省可建 | grass |
| 2及其余 | 3 | 不可走 | 不可建 | forest（渲染仅显式0、1、2） |

材质名称来自MAP_alloc的字段0xe8/0xec/0xf0及prepareRendering映射，不应根据名称猜玩法通行性。PlainPlane的中央terrain1才是可建区域；两侧terrain0是只能行走的入口区域。

`pads=[[6,2],[12,2]]` 是UFO降落锚点，真实敌人出生世界坐标为 `pad+(2,2)`，即 `(8,4)`、`(14,4)`。它们不是建塔许可列表。`entrance=[[8,27]]` 是羊圈入口及寻路目标；`enclosure` 是羊活动区域。三个列表均以导航坐标直接索引。

路径使用从entrance反向扩展的距离场，8邻接依次为 `(-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)`，权重对角约1.4142135、正交1，未达格距离-1。导航阻挡判据为 `(state & 9)==1`；若正交邻格阻挡，关联对角也禁用，禁止切角。ground使用叠加塔占位后的格；flying使用原terrain格副本，忽略塔但仍受terrain阻挡，不是无条件直线穿地形。

下一目标为合法邻格的 `distance+步长` 最小值；等值保留上述顺序先出现者。当前世界坐标截断到整数格，目标为邻格中心。原算法每步重新求目标，不能把建塔前的一条折线路径永久缓存。

## 放置、升级与出售（静态事实）

放置要求完整2×2在范围内、四格bit2均为0、四格没有其他塔。开放地图试放时保存距离场并将四格OR1，计算新距离场，确认实际出生点`pad+(2,2)`可达，并检查活动敌人，再恢复试放状态；成功正式放置才扣钱。固定路径地图跳过试堵路及导航占位：terrain1仍可建塔，其原bit1是路径墙而不是已有塔。出售也不改变固定路径导航状态。活动链完整检查仍存在边界差异；诱饵的四格另加bit8使其可走，建成后可重定向距离场，详见 EFFECT_RULES.md。

`TOWERS_canBuildHere` 返回0可建、1一般不可建、2同原点已有塔、3堵路。金币不足不能通过放置。建造启动立即扣cost；完成前不攻击。开波前建造/升级固定500ms，开波后为tech.buildtime；完成条件严格 `elapsed > buildtime`。升级必须是当前塔的upgrades成员，且不处于建造、出售或禁用状态；立即切换类型、扣目标级cost，等待目标级buildtime。

refund由升级树递归累加：`refund(current)=cost(current)+refund(parent)`，不是仅当前级cost。出售在开波前等500ms，开波后等3000ms；等候期间塔不攻击，原占格到真正移除才释放。开波前退refund，开波后退 `trunc(refund*0.75)`，同时扣 `min(score,10)` 分。完成等待也严格大于。

## 时间、波次与敌人（静态事实及有限推断）

GAME_alloc固定步长25ms，即40Hz。每步先处理命令、再增加游戏时钟、出怪、弹体、敌人、波预览、塔、羊。移动为 `min(到目标距离, speed*dtSeconds)`，speed1就是每秒1导航/世界单位；临时加速可2倍持续20秒。生命归零进入失败；到entrance整数格就扣1生命，生成羊圈入侵动画并移除玩法敌人，无金币奖励。

发送一波先寻找空闲UFO并轮询pads，预建count个敌人。ENEMIES_alloc初始lastPadIndex=0，查找从下一项开始，因此PlainPlane第一波使用pad[1]，第二波用pad[0]。UFO从 `(navWidth/2,navHeight+20,10)` 飞往 `(padX+2,padY+2,3)`。指令确认下降每步长度为 `(sqrt(剩余3D距离)/400+0.001)*dtMs`；够近则snap、状态进入释放。这个双平方根是原指令确有的两次sqrtf，不是反编译噪声。

下降后以到达时刻为lastRelease，只有 `now-frequency > lastRelease` 才释放，随后lastRelease累加frequency，可补发多只。frequency单位毫秒，首波1200。使用上述式、25ms、双精度计算进行独立静态复现，PlainPlane两pad均下降4625ms，首只随后最早1225ms出现；实际ARM单精度及更新顺序可能差一帧，未声称动态验证。

链表释放完毕后UFO进入state2退场，xy保持，按 `z += dtMs*1.4/(50-z)` 垂直升高；当 `50-z < 0.01` 时state3并snap回降落目标，idle状态不显示。原作距上次发波严格超过20000ms会自动尝试下一波，即可重叠，仍需空闲UFO；用户也可手动提前。提前波的奖励在这一波最后一个敌人被击杀时计入bonus/score分支，并非即时金币。关卡胜利要求已发送全部26波、没有活动敌人、所有UFO回到idle状态3；羊被带走的视觉收尾另受ANIMALS状态控制。

HP直接为有符号64位整数，资源health10就是10，无缩放。regen每严格超过1000ms按整数回血并不超过最大HP。spawn是最大分裂代数：当 `generation < spawn` 死亡产生2子敌人；子generation=父+1，子HP=`原波baseHealth >> 父generation`，所以第一代仍有完整baseHealth。子出生坐标在父点各轴±1.5内随机，最多尝试10次找可达点，否则回父点；保留波编号，heat重置1。子死亡不奖励gold，只有generation0被击杀奖励该波gold及等额score。spawn1使每个原敌人总计最多3个可击杀对象，spawn2最多7个；原ENEMIES_sendNextUFO的完成计数分支只乘3，需要核对spawn2样本的实际波完成效果。

## Beam攻击（静态事实）

塔选范围内距离羊圈路径最短的有效敌人，需路径距离>0，获取目标时距离严格小于range。目标一直保留直到死亡/超范围；保留检查允许距离等于range。按antiair/antiground过滤。yaw将最短角差按 `error*12.5*dtSeconds` 平滑，射击要求转向误差绝对值<5°及 `elapsed > reload`。

beam发射先计算 `trunc(floatDamage*damageBoost*heat)` 得整数，再按armor1/2/3对应tech的lightdamage/heavydamage/bionicdamage乘后再次截断。armor0原值，armor4特殊 `(1-isflame)*damage`。基础Kilowatt damage3.75、无boost、heat1时首击3伤害；不得用连续float HP冒充原整数规则。

伤害后增加heat：`min(heat+0.05,2)`，armor4不加；每敌人更新冷却为 `max(1,heat*(1-0.05*dtSeconds))`。后续beam伤害会受热量提高，不是恒定3。生命伤害扣到0，伤害统计最多计算敌人剩余HP。beam产生瞬时命中及短时特效，无弹体飞行时间。

## 实现边界

当前实现支持12类武器、四科技、普通及无限／沙盒、护甲／飞行／再生／分裂、计分和保存继续。战斗随机算法为恢复出的MT19937；桌面固定种子与原作会话初始种子尚未逐项对齐，不宣称相同随机轨迹。精确相机、动画插值、炮塔俯仰平滑、阴影、skybox、粒子和羊被抓走收尾仍有差异，不能宣称与Android逐帧等价。

若Windows样本选择清场后自动下一波、简化UFO/羊动画、关闭声音等，应作为桌面版便利性/原型边界标注，不能将这些选择描述为原作静态事实。真实可玩和26波通关应分别验收，真实玩家操作结果仍需Windows运行验证。
