<!-- Focused static-rule reconstruction and handoff summary for task01. -->
# PlainPlane规则恢复简报

本子任务完成P3核心静态证据研究，并对主代理编写的Windows样本 `src/game.cpp` 做了局部正确性修复。没有修改原APK、原SO、既有native-evidence、Ghidra数据库、probe-native.ps1或其他系统进程；MC服务器未触碰。没有安装软件、运行Python、播放声音或执行Git提交。

## 完成内容

- 独占复用现有Ghidra12.1.4数据库，以readOnly、noanalysis、max-cpu2分批导出地图、寻路、放塔、时间步、波次、beam、热量、分裂和生命周期证据。导出均complete=true。
- 新增 `TowerMadnessRules.java` 和 `TowerMadnessLiteralData.java`，证据保存在 `agent/codex/rules-evidence/`，没有覆盖原native-evidence。
- 创建 `docs/RULES.md`，记录函数地址、PC常量、事实/推断和未恢复边界。确认导航是terrain的2倍格；pads是UFO出生锚点，entrance是羊圈终点；第一波是pad[1]；基础3.75beam首击整数3，之后受heat影响；refund为累计升级cost。
- 修复game.cpp的UFO退场公式、首pad轮询、频率补发、每tick邻格distance+cost选路、target范围保留边界、air/ground过滤和失败当步停止射击。新增文件级浮点常量均有英文说明。

## 验证与限制

独立PowerShell复现原UFO下降式：PlainPlane两pad按25ms步长均4625ms降落，随后首波最早1225ms释放第一只。此为静态公式复现，未声明ARM动态等价。资源统计26波：18普通地面、4飞行、4spawn1地面，均armor0、regen0。

代码变更交由主代理统一构建与Windows游戏运行验证，避免并行争抢构建目录；本子代理没有自行完成游戏通关或GUI验收。没有真实Android设备，无法进行原作对照。完整随机序列、所有武器、无限模式、诱饵、保存、视觉精确时序和活动链尸体建塔边界仍未恢复；这些限制已如实写入RULES。

MEMORY由主代理统一维护，避免多个代理写入冲突。原可持续基线与工具路径不因本子任务失效。
