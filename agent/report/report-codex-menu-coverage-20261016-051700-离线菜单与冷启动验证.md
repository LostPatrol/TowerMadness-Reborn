<!-- Completed isolated verification of original native menu behavior and the root agent's cold-start fix. -->
# 离线菜单与冷启动验证简报

执行代理：codex-menu-coverage。范围为独立验证与隔离测试driver；没有修改主host、MEMORY或根文档。candidate23冷启动修复由root实现，本代理只负责真实Windows输入复验。全部运行关闭游戏音频，没有调整系统音量。

## 已完成结果

| 项目 | 实际结果 |
| --- | --- |
| candidate22普通menu冷启动基线 | 有真实F5存档仍只显示3个原菜单项；三次F5均NOGAME，确认Resume缺口，进程退出0 |
| root的candidate23冷启动修复 | 普通menu自动加载已复制的真实checkpoint并显示原4项菜单；暂停时钟34575→34575ms；真实点击Resume后增长至39550ms，钱40、血10、PlainPlane保持，进程退出0 |
| 已通关四campaign地图 | 完成全部campaign后的真实存储，仅以原New game与真实拖动回访Madness、MuttonsFolly、DividedAttention、PlainPlane，截图核对通过，退出0；连续101次拖动没有误启动GAME或卡死 |
| 四本地成绩原组件 | 真实存储下显示HOST成绩1564、1939、3482、4790；四页与Back正常，退出0 |
| 原Achievements及左上g+ | 离线点击返回Game Center不可用；后续原New game可正常进入地图选择，退出0 |
| 强制News组件入口 | NewsFetchDelegate alloc缺失，出现UNIMPLEMENTED并退出1；未实现该在线组件 |

冷启动脚本验证了启动前后EXE SHA256一致、原Resume路由、暂停时钟不变、恢复时钟推进、钱血地图保持、原生退出0和无未实现/异常/GL错误。正式结果均有acceptance.json；candidate22使用明确的缺口预期断言。

## 原实现边界与未完成范围

已通关后Map List替代旧campaign/add-on双tab是原游戏设计。原MAPMENU_init及MAPMENU_render根据unlockedEndOfGame切换，全地图carousel内仍包含四个campaign，不能把旧坐标打开Map List误判为宿主缺失菜单。

成绩测试直接进入原scoresMenuPage，再点击原Local scores、四campaign行与Back，属于原组件覆盖。原mainMenuPage/mainMenuPagePaid没有Scores或News入口，活动局仅新增Resume；本次未构造新菜单或新增入口，不能声称玩家普通菜单已有High Scores入口。没有找到独立Stats页面；原Reset Stats操作是清空四个成绩列表。成绩回放按钮未验证。

News失败来自强制原组件入口，普通原菜单未暴露该入口；没有观察到它阻止正常离线地图选择。Game Center仍无在线服务或本地成就替代面板，点击在原实现中静默返回。

## 证据与改动

证据目录：`agent/codex/x86-continue-20261016/menu-analysis/offline-pages/`。

- `cold22-final/evidence/run/acceptance.json`：candidate22缺口对照通过。
- `cold23-final/evidence/run/acceptance.json`：candidate23真实冷启动Resume通过。
- `map-four/evidence/run/`：四campaign原地图截图与精确索引日志。
- `scores-real/evidence/run/`：四个真实本地成绩页面与返回日志。
- `gc-final/evidence/run/`：真实Achievements/g+点击及后续New game。
- `news/evidence/run/native.log`：强制News入口失败证据。
- `completed-map-static/pseudocode.c`：原已通关Map List分支静态证据。
- `RESULTS.md`：详细范围、冻结EXE哈希及早期过程结果排除说明。

隔离改动仅包括本目录的测试driver、可复用cold-resume.ps1及静态导出脚本。实际campaign存储在其日志出现ORIGINAL_ALL_CAMPAIGN_DONE后才复制，所有测试均使用独立state。原Ghidra数据库只读打开，导出结束丢弃修改并释放。
