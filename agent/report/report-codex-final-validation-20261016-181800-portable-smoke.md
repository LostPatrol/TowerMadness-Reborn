# 便携发行副本验收

源目录：`release/TowerMadness-x86-20261016-181000`。仅复制并运行 `agent/codex/fps-resize-20261016/portable-smoke/Portable Copy With Spaces/Play.cmd silent 1080 120`，未启动干净发行目录自身。

- portable-runtime.json 声明的 2390 个文件以及 EXE，源目录运行前、完整副本、源目录运行后全部 SHA256 相符。EXE 为 `618E8CD65A49580F06224027A2FE12580E040F8F7A21AE88C149E7171A04FB48`。
- 源目录验收后仍无 data，logs 为空。
- PerMonitorV2 调用线程读取真实客户区为 648×1080；原生日志 `WINDOW_INITIAL client=648x1080`、`FRAME_PACING target=120`。
- 原生标题初始为 `TowerMadness | 115.6 FPS | cap 120 | F6: 60/120/unlimited`，F6 后为 `TowerMadness | 5096.1 FPS | unlimited | F6: 60/120/unlimited`，日志目标变为 0。
- 仅向本次副本 PID 28648 的窗口发送 WM_CLOSE；native 与 launcher 均正常退出 0。未操作用户旧进程 46664。
- 这是启动冒烟而非稳定性能基准：约 2.94 秒 120 目标窗口记录 116.94 平均 FPS、1 longFrame、max 58.439ms；不限帧约 1.32 秒记录 5121.16 平均 FPS。不能以短窗口数据宣称战斗性能。

初版驱动的 cmd /C 嵌套引号未能启动，改用 CALL 解决。随后旧 DPI 上下文将 648×1080 虚拟化为 432×720，造成两次验收脚本误判；只关闭各自窗口，原生日志保留为 first-native.log、dpi-virtualized-native.log。纠正验收线程 DPI 上下文后通过，未修改产品源码。

可复核摘要与脚本：`agent/codex/fps-resize-20261016/portable-smoke/summary.json`、`verify-portable.ps1`。副本原生日志在其 logs/game.log。
