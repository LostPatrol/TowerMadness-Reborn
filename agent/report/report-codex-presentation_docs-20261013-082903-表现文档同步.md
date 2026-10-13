<!-- Documentation synchronization scope, verification, and outstanding visual fidelity. -->
# 表现文档同步

更新docs/RENDERING.md、COMBAT_VISUALS.md、PROJECTILE_VISUALS.md、DEVELOPMENT.md、COVERAGE.md与P8_GAME_QUALITY_PLAN.md；未修改源码、MEMORY、README、TODO或Git状态。

文档对齐当前源码：交互短暂Effect/Beam独立realtime及离线确定性fallback；原相机方向/height50与桌面fit差别；unlit texture×RGBA及point byte截断；塔build/sell、升级旧/新模型、directionalshadow、Tesla附件、boost圈与可选appearance保存；原selection/已有塔rangecircle及预览范围限制。同步24项CPU检查范围，并链接已成功运行未经修改原ARM游戏的Android对照说明。

核查来源为当前SRC diff、CAMERA.md、TOWER_VISUALS.md与ANDROID_REFERENCE.md。执行git diff --check通过；本次仅文档更新，没有重复运行游戏检查。根代理继续负责最后图形修改与验收，本文不声明该验证已完成。

未闭合：完整nativeAimSmooth战斗gate、初始随机yaw/idle、预览/升级范围反馈、非法选中500ms淡出、诱饵屏幕aura、条件核爆闪光、羊完整动作、Menu world；原视觉随机序列、屏幕参数与透明/深度排序、密集局及动态等价验收仍待完成。原ARM运行成功不等于P8整体完成；x86 API24 unwind问题仍由Android文档保留。
