<!-- Original tower price sources, static field mapping, and runtime identity limits. -->
# 原作升级价格

当前静态证据支持 `Kilowatt Laser → Megawatt Laser` 升级花费20金币，`Mortar I → Mortar II` 花费25金币。不能把原作轮盘中的25直接归到Laser，再修改资源价格。

## 原始资源与加载链

ARM原包 `TowerMadness+Zero_+3D+TD_1.22_APKPure.apk` 的 `assets/Easy.tech` 与解包文件全文一致。标准化的 `assets/data/Easy.tech.json` 保留这些价格。四套tech的Megawatt Laser均为20；PlainPlane.gamemode引用Easy。

| 塔 | cost | model | 升级目标 |
| --- | ---: | --- | --- |
| Kilowatt Laser | 10 | potatogun_i | Megawatt Laser |
| Megawatt Laser | 20 | potatogun_ii | Gigawatt Laser |
| Mortar I | 10 | mortar_1 | Mortar II |
| Mortar II | 25 | mortar_2 | Mortar III |

`TOWERTYPE_parse`（Ghidra地址002dcac8起）读取JSON键 `cost` 并写入塔类型偏移 `+0x3c`。原导出的strings.tsv漏列这个键，不能据此认定解析器忽略cost。直接解析ARM的PC相对字面量，目标地址0035de72确为 `cost`；证据脚本位于 `agent/codex/p8-upgrade-price/verify-price-evidence.ps1`。

`TECH_load`调用 `resolveTowerType`（002b3b14）递归解析升级目标，并把 `本级cost + 父级累计投资` 写入 `+0x40`，不修改 `+0x3c`。`PIEMENU_show`（002ab110）把唯一升级目标的 `+0x3c` 作为轮盘价格；`PIEMENU_render`直接格式化这个价格。`TOWERS_tryUpgrade`（002b6b38）检查金币并扣同一个目标cost，没有再加原塔投资。售塔选项使用当前塔累计投资 `+0x40`，开波后按75%计算。

静态导出位于 `agent/codex/p8-upgrade-price/pseudocode.c` 与 `instructions.tsv`；加载及升级旧证据位于 `agent/codex/native-evidence/gameplay-pseudocode.c` 和 `agent/codex/p8-tower-visuals/setup/pseudocode.c`。分析复用原ARM数据库，以readOnly/noanalysis导出，没有改原APK。

## 动态截图边界

未经修改ARM1.22的 `plainplane-build-choice.png` 可见资金50及建造选项10；`plainplane-selected-range.png` 可见资金40、退款10和升级25。两图位于 `agent/codex/android-reference/runtime/logs/TM_Original_API17_ARM_clean/`。这是实际截图观察，但这些数字不能独立证明塔名。

原作稳定轮盘及连续普通UI复核已确认旧图为Mortar：50→40→15，升级扣25。真实Laser在能量家族顶部，实际40→30→10，分别扣10和20；该局此前已建一座Mortar，不能写成Laser独占新局50→40→20。具名教程、选中和升级后形态提供独立身份依据；见[ORIGINAL_TOWER_IDENTITY.md](ORIGINAL_TOWER_IDENTITY.md)。没有Laser价格差异或覆盖证据，原资源和Windows价格保持不变。

本调查没有调整资源或游戏代码；明确塔身份的动态操作记录应作为后续验收依据。
