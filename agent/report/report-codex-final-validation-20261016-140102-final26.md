<!-- Final native acceptance facts; local UTC+8 filename and one immutable executable. -->
# 最终同EXE验收与便携包

最终EXE SHA256：`677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`。
host SHA256：`4C52456CECFDCDC66DC4D4F4B2201605119938FF1C1F79194A0F9F925B4EA870`。

`candidate26`冻结构建后用于所有final26验收；旧current运行自然结束后，同一EXE和输入原样提升到 `agent/codex/x86-reuse/current`，未重新链接，输入逐SHA校验通过。提升事实见 `final26/current-promotion.json`。

## 已通过范围

- 116个原地图的实际载入与渲染，GL错误0；这不是116图全部通关。
- 原声音source池、五类效果、两段音乐、停止/位置保留、资源失败隔离、pending切换取消。
- 原成绩日期/replay两次验收、实际1×/2×/4×/1×时钟。
- 12基础塔原API合法建造/升级/出售；97步原建塔轮盘GUI。
- 14项实际原武器战斗触发；全部显式marker、nativeExit0、GL错误0。伤害/减速/增益/地雷/诱饵断言均保持真实触发；隔离战斗以输局结束，不冒称通关。
- 13次F9恢复压力、跨进程continue、原New Game替换活动局；设置两次、Guide/Credits及五类DLC菜单入口。
- 外部Windows定向输入验证camera的13个样本、原限位、暂停隔离及玩家字段保持；普通menu冷启动暂停时钟稳定、Resume后推进且玩家字段保持。
- latebattle-final2：11塔、7敌人、2弹体的F5/F9精确恢复，继续推进，原自然胜利5生命；buildBound/nativeExit0/GL错误0。

四campaign原GUI自然全链最终已通过：PlainPlane10生命、DividedAttention10、MuttonsFolly10、Madness8，unlocked_medium/hard/insane/endofgame均为1。Madness最终score4582、finalScore4941、simulationMs1246925；nativeExit0、GL错误0。最终共148个native run全部通过且绑定同一EXE；latebattle及外部camera/cold/portable/nukeGUI另计，不凑入native run数。完整逐run与范围映射在 `agent/codex/x86-continue-20261016/final26/summary.json`。

## 失败与修正

前轮EXE的newgame验收两次失败：原GUI确实创建money50/gameStarted=false的新局，但测试在下一确认帧前耗尽预算。主代理仅为newgame测试尾部增加8秒；final26重新冻结并重跑全部验收，实际 `ORIGINAL_NEWGAME_GUI_DONE previousClock=36925`，新局clock1025、money50。原两次失败完整保留在summary中。

旧Flak单塔配置不能覆盖原随机出生方向，旧v2冻结EXE重跑也失败；改为原合法检查允许的四pad附近建塔，各原扣50总200，不改原波次、敌人或玩家资源。最终Flak仍要求真实damage大于0，已通过。

## 便携包

烟包：`agent/codex/x86-continue-20261016/Portable final26 smoke`。从 `C:/Windows/Temp` 启动含空格路径中的包内EXE，使用包内runtime/lib/assets/textures；实际F5保存、F9原恢复、普通menu冷启动均通过。测试存档写入包根data，未指向开发package。证据 `final26/portable-smoke.json`。

干净交付目录：`release/TowerMadness-x86-20261016-135600`。EXE同最终hash，2390项portable inventory逐SHA一致，data不存在，logs为空，未启动避免污染。证据 `final26/clean-release.json`。包无Android或开发工具依赖；原输入完整性证据由主代理记录在 `final/original-integrity.json`。

主代理独立便携包全链及终局冷启动已通过，证据 `portable-campaign-final26/acceptance.json`，同最终EXE哈希，native两次退出0、GL错误0。从 `C:/Windows/Temp` 运行包内runtime，data独立；四关生命10/10/10/9，Madness score4580、finalScore4975、simulationMs1249050。原胜利页F5保存终局后，普通menu重启忽略终局且F5无活动局，新游戏入口正常；8个进度/解锁值及scores_*完整内容前后相同，无重复结算。此实验包含测试存档，交付仍为上述未启动的干净release。该项加入summary的externalChecks/portableCampaignAndTerminal，不计入148个native run。

没有修改原APK/ELF、系统音量、用户任务文件、MEMORY、TODO；未提交git。
