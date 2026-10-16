<!-- Isolated portable original-menu residency measurement on this Windows machine. -->
# 原菜单长驻稳定性专项

结论：通过。2026-10-16北京时间14:29:06.510至14:57:10.179，实际1683.669秒（28分03.669秒）；原WM_CLOSE正常退出，nativeExit0，GL错误0，29个一分钟间隔及末帧样本全部通过定向WM_NULL响应。没有重启或写进程内存。

从交付ZIP解压到独立 `agent/codex/x86-continue-20261016/menu-soak-final26/portable`，工作目录 `C:/Windows/Temp`，隐藏启动原menu、音频env0。EXE SHA256始终为 `677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`。仅该实验副本生成data；没有运行干净release目录、修改原资源或真实用户profile、调整系统音量。

| 指标 | 首样本 | 末样本 | 全程范围 |
| --- | --- | --- | --- |
| Private bytes | 224464896 | 225804288 | 224464896–226037760 |
| Working set bytes | 166088704 | 166305792 | 166088704–166371328 |
| Handles | 328 | 327 | 327–329 |
| Threads | 8 | 6 | 6–9 |

Private首末增加1339392字节，Working set增加217088字节，Handles减少1。初始轻微波动后资源基本持平，未观察到单调增长或失去响应。最后原日志 `ORIGINAL_MENU_FINAL ... game=0`，未发现HOST_ERROR、NATIVE_EXCEPTION或UNIMPLEMENTED。CPU累计约40.7秒。

证据：`agent/codex/x86-continue-20261016/menu-soak-final26/acceptance.json`、`samples.jsonl`、`native.log`、stdout/stderr。过程脚本 `agent/codex/x86-continue-20261016/menu-soak.ps1`。

此项仅验证本机原菜单长驻，不代表完整长局、全部玩法长时稳定性或多机兼容。未采集截图。未修改综合报告、MEMORY、docs或summary。
