# FPS 与窗口缩放独立验收

验收对象：candidate3，EXE SHA256 `618E8CD65A49580F06224027A2FE12580E040F8F7A21AE88C149E7171A04FB48`。本轮只增加隔离验收脚本与证据，没有修改产品源码、真实用户存档、系统音量或用户原有游戏进程。

## 已通过

- 正常原生菜单入口、全新隔离状态：实际客户区依次为 480×800、648×1080、1280×720、最大化 2560×1369、480×800；每个尺寸原生 Options/New Game 点击有效。横屏与最大化留黑区域点击无效。证据：`agent/codex/fps-resize-20261016/validation/candidate3-menu/evidence/run/acceptance.json`。
- F6 切换 60→120→不限帧→60，原生日志 FRAME_STATS 对应目标变化。稳定区间约 59.99–60、119.99–120 FPS；不限帧菜单约 5329.8–5376.35 FPS，记录区间 longFrames=0。首次窗口操作统计约 59.60 FPS，p99 22.19ms，最大 38.23ms；不能只引用后续稳定样本。最后额外一次 F6 的结束片段只有 1 帧，不作为性能依据。
- 648×1080 与 1280×720 两组原生菜单 Resume 进入隔离测试存档：拖动左右移动原始摄像机，滚轮缩放生效且近远极限稳定，暂停滚轮不改变摄像机或游戏时间，F5/F9 精确恢复 map/money/lives/score/gameStarted/simulationMs；各 14 个原生只读快照，退出码 0。证据分别为 `validation/candidate3-camera-final-648x1080/evidence/run`、`validation/candidate3-camera-final-1280x720/evidence/run`（均位于本轮 fps-resize 目录）。
- 主代理补充的 648×1080 原生 GUI 建造、升级、出售链 19/19 通过，native exit 0、GL error 0；实际初始客户区日志为 648×1080，主代理已检查同尺寸 H-Bomb-upgraded 截图视觉正常。证据：`agent/codex/fps-resize-20261016/nuke1080-candidate3/runs/20261016-175508-928/acceptance.json`。本项引用主代理证据，没有重复运行。

## 误判与保留记录

- candidate2-menu 初版驱动错误地假设 Esc 可以从原版 Options 返回主菜单，导致脚本失败；修改驱动操作顺序后的 candidate2-menu2 通过，原失败目录保留。
- 两个 candidate3-camera-f9 初版驱动错误地限定 F9 日志必须 `restore=1`；启动恢复已占用第 1 次，因此 F9 实际为 `restore=2`。原生日志已显示精确恢复。驱动改为接受数值恢复计数，并核对实际存档字段后，上述两个 final 重跑均通过。原失败目录 `candidate3-camera-f9-648x1080`、`candidate3-camera-f9-1280x720` 保留。
- candidate2 初始窗口尺寸仍为 480×800，不能作为真高分辨率验收。高分辨率结论仅使用修复后的 candidate3 证据。

## 范围与未覆盖

- 不限帧数字来自原生菜单，不代表战斗 FPS；本轮没有测定战斗不限帧上限。FRAME_STATS 已核对，窗口标题文本没有单独自动断言。
- 摄像机/F5/F9 使用旧的隔离验收存档副本；全新状态验证用于菜单与动态尺寸，不能把二者描述成同一条全新开局流程。
- 不足资金升级停滞由主代理继续定位，本报告不将其标记为已解决。candidate3 不是该问题修复后的最终候选。
- 测试使用 Windows 消息输入与原生只读内存快照，声音通过测试进程环境关闭；未使用模拟器交付。

验收脚本：`agent/codex/fps-resize-20261016/validation/window-validation.ps1`、`agent/codex/fps-resize-20261016/validation/scaled-camera.ps1`。

## 后续 DPI 精度更正

便携副本验收发现，当前 PowerShell 调用线程的 GetClientRect 会受 150% DPI 虚拟化影响；之前动态 resize 表中的数值是该调用方坐标，不能严格称为物理像素尺寸。动态缩放、点击与留黑行为的通过结论仍成立，但原始尺寸表仅表示验收驱动坐标。之后便携验收使用 PerMonitorV2 线程读取，已独立确认 `silent 1080 120` 的真实客户区为 648×1080；见 `agent/codex/fps-resize-20261016/portable-smoke/summary.json`。
