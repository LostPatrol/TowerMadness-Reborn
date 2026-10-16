<!-- Original weapon trigger evidence and integration limits; local UTC+8 filename. -->
# 原武器战斗观测与集成验证

## 变更

新增 `tools/x86-reuse/weapon-combat-cases.json`，汇集14种原武器行为配置，补齐显式key/expect。新增 `verify-weapon-combat.ps1`，参数化构建目录与隔离输出目录，保存marker、哈希、计数和终局；失败保留原证据并停止当前批次，可分批继续。

`weapon-combat.hpp`在首个真实伤害后继续原tick，250毫秒后采集 `combat-hit-visible.png`。Flak固定单塔测试因原出生点变化存在漏射，增加可选placements：在原合法建造检查允许的附近位置建造四塔，每塔真实完成、原金币扣费后再发波；不改原地图、波次、敌人、金钱、生命或胜利字段。新增配置仍要求真实damage大于0。

README及便携包README模板明确：关闭窗口不自动保存，退出前按F5，重启读取上次F5存档。

## 已有v2证据范围

`agent/codex/x86-continue-20261016/weapons/combat/evidence-v2`共有14个成功run，均nativeExit0、acceptancePassed=true、GL错误0。冻结EXE SHA256为 `4A64A0C4F001C9448F370EF6CE268989818A9D289E537CD934906257339524FC`。

| case | 原行为观测 |
| --- | --- |
| flame | damage450，60伤害事件，60次fire，10击杀，heatedEnemy |
| mine | damage135，3事件，armedClock84950，removed=true |
| bait | effectAdds2，armedClock84950，removed=true，slow0.25/3000ms |
| slow | effectAdds34，34次fire，slow0.25/3000ms |
| boost | companionBoostMax1.10000002，伴随Laser damage133/34事件 |
| rail | damage450，10事件，6次fire，10击杀 |
| missile | damage154，15事件，19次fire，4击杀 |
| nuke | damage500，15事件，4次fire，15击杀 |
| hbomb | damage570，16事件，3次fire，16击杀 |
| laser | damage99，33事件，33次fire，heatedEnemy |
| plasma | damage85，17事件，17次fire |
| mortar | damage109，30事件，16次fire |
| shock | damage450，10事件，10次fire，10击杀 |
| flak | damage1275，60事件，33次fire，5击杀 |

以上14个终局均为lives0/won=false；其范围是原武器触发与效果观测，不能冒称地图通关或完整玩法验收。

## candidate24实测

构建成功。EXE SHA256 `841BB15F1A9455B169A94677AC19EB34F2C2697CC792A6AB55C0721D701F27FB`；host SHA256 `7B8328002B4DA66BDD0EC2F37CB4EFA8C26E291739116CDADAFB882ED24B0676`。

`combat24/weapon-combat-summary.json`记录除Flak外13项真实成功，均有WEAPON_COMBAT_PASSED、nativeExit0、GL错误0。伤害型项保存了延迟命中截图。Flak原单塔配置失败，nativeExit3、GL错误0、无真实伤害，原始证据在 `combat24/flak/evidence`。

为排除新mode集成回归，完全相同原v2冻结EXE和原flak.json在独立state复跑也失败，证据在 `combat24/flak-v2-recheck/evidence`。原Peach有四个pad，Flak I射程5且只对空，单塔位置12,10不能稳定覆盖各出生方向；不能以反复重跑碰到通过代替稳定策略。

## 尚未闭合

candidate25四塔Flak已真实通过，nativeExit0、GL错误0、WEAPON_COMBAT_PASSED。EXE SHA256为 `AFD3FE3D56D6360DEF6AACA91E729D5D458E0DDDC8611ABE7A8EFA2A39CBB7C9`，host SHA256为 `0175E13C46482B5B440172200B750309D7F2713BD37BAFC163D1FF443D89C76B`。实际原检查允许位置为[6,6]、[26,6]、[10,24]、[26,24]，各扣50，总扣200；观测35次fire、59个伤害事件、伤害1275、76次效果分配。延迟命中截图已目视检查原HUD、四角Flak及飞行敌人正常。该case仍以输局结束，仅证明触发。多塔的damage/fire计数按类型汇总，kills/towerDamage字段仍是主塔读数，不代表四塔总击杀/总伤害。

candidate24与旧v2成功不能拼接为最终同一EXE全集验收；最终current仍需全14项复验。candidate25证据位于 `combat25/flak/evidence/weaponcombat-20261016-133914-489`。

另外只读核查latebattle-active旧run：ACTIVE及RESTORED_EXACT均有7敌人、2弹体且玩家状态精确一致，但最终HOST_ERROR Late restored battle lost、nativeExit3；没有ADVANCED/DONE成功标记。后续修复及新证据由主代理另行统一。
