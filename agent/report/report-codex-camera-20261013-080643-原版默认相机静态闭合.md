<!-- Focused camera recovery report; process details are intentionally outside MEMORY. -->
# 原版默认相机静态闭合

使用 reverse-engineer-anything 技能及现有 ARM Ghidra 数据库，以 `-readOnly -noanalysis` 导出相机、游戏初始化/约束、分辨率、菜单相关函数；未控制 Android 设备，未改 APK、源码、MEMORY、README、已有文档或 Git 分支。新增开发规格为 `docs/CAMERA.md`，过程证据位于 `agent/codex/p8-camera`。

已闭合：PlainPlane 原版默认 target=(11,16,0)、eye=(11,21,50)、up=(0,0,1)、垂直 FOV45°、near1/far100、aspect=width/height。相机位于 +Y 侧，屏幕向右为世界 -X；地图出生圆环在上、羊圈在下。默认偏移 +5 是固定世界距离，不随高度按比例变化。约束高度 .8..50，FOV=45+25*(50-h)/49.2；原版初始化不按地图或屏幕自动 fit。

验证：查看已经捕获的未修改原游戏 480×800 首关教程图和主菜单图，运行独立 Node 计算脚本；原矩阵预测地面顶部 y=103.44、底部 y=715.97，与截图边界一致。导出完整 view/projection 数值及七个地图标志投影到 `default-camera.json`。这是静态公式与截图构图交叉验证，不是运行时矩阵读取或自动像素等价。

主菜单是另一个 Menu 世界，12×18，对应 eye=(8.4,20,2)、target=(8,9,1)、FOV60°。选关预览使用 +Y 侧动画透视视角，动画末端转为游戏默认姿态。桌面静态缩略图与横屏保留战场矩形均应明确为适配，不宣称完整恢复原菜单动画。

未实现/未验范围：本子任务按分工不修改 renderer，不实现原 pan/pinch，不验证动态缩放/旋转事件，不运行 Windows 游戏，不恢复选关动画。父代理已收到精确方向、参数、投影及适配边界，可直接实施。所有 Ghidra 会话已结束，无数据库写入。
