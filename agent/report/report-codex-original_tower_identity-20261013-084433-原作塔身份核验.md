# 原作塔身份核验简报

2026-10-13，原版APK，API17 ARM窗口 `TM_Original_API17_ARM_clean:5592`。通过sanctioned `node_repl` / `@oai/sky`普通UI点击核验；未使用adb input，未修改APK、金钱、HP、波次或服务。

- Mortar独立新局：50币10羊 → 建造10币后40 → 升级轮盘25 → 升级后15。教程明确命名Mortar，绿色军用炮模型与旧证据吻合。
- Laser第二新局：先建Mortar后40 → 能量子菜单顶部金色盘状Laser建造后30 → 升级轮盘20 → 升级后10。教程明确命名Laser，升级前后金色盘状模型符合Laser。没有开波，分数保持0，羊保持10。
- 未完成严格的Laser独占新局50→40→20截图；实际连续资金序列包含一座未升级Mortar，但Laser实际建造和升级扣款完整可见。
- UI相机zoom会迁移轮盘，早期截图之后的点击造成家族误选。已纠正归属：绿色上方为弹道，右橙色为能量（顶部Laser、右侧Plasmatron），星形为商城特殊塔。曾进入售塔预览但取消，未出售。
- 旧 `plainplane-build-choice.png` 与 `plainplane-selected-range.png` 实际是Mortar，不是Laser。不能将该旧塔的射击现象标注为Laser。

新增开发者证据说明：`docs/ORIGINAL_TOWER_IDENTITY.md`。截图保存在 `agent/codex/p8-original-tower-identity/`，文档明确标注早期误命名文件不可用于Laser结论。未修改src/MEMORY/README/TODO/git。
