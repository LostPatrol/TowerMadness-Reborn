<!-- P8 animation implementation scope, validation status, and remaining fidelity limits. -->
# P8 敌人原生动画状态接入

执行代理：codex-animation_state。时间：2026-10-13 06:11:38（Asia/Shanghai）。

依据 `docs/ANIMATION.md` 已恢复的 ARM 静态证据，将行走取帧、独立原生槽位、眩晕取帧时间与平滑视觉朝向接入 Windows 仿真。修改 `src/game.hpp`、`src/game.cpp`、`src/combat.cpp`、`src/projectile.cpp`、`src/session.cpp`、`src/coverage_checks.cpp`，并维护动画设计文档。主代理负责渲染接入、统一构建验证与 MEMORY 维护；本子任务未修改 TODO、原 APK、逆向数据库。

`Game::walkFrame(const Enemy&) const` 保留 float32 reciprocal×150 第一次截断、walk factor 第二次截断、槽位加到帧持续时间的运算顺序。仅 generation=1 且 subwalk 非空时使用子代列表；stun 使用捕获时间，死亡记录对应的冻结时间。空列表/非正速度返回零帧，非正持续时间小容错为1ms。

新增独立1024槽位循环分配器，首槽0，不共用桌面实体ID。耗尽时标记−1并继续战斗，不引入敌人数上限。漏怪立即释放；非分裂/末代死亡保留3秒float32倒计时占位，分裂父保留到下次占位更新。占位更新处于 enemy movement 阶段入口，projectile 击杀可能在同tick更新，tower击杀在下tick更新；原作每个调用者的同tick排序尚未完全证明。

`visualHeadingDegrees` 按原最短角差、2度死区及每毫秒0.005系数平滑，stun 在tick入口生效时冻结；原有瞬时 radians heading 和战斗位置/伤害/RNG消费保持。实际stun应用均刷新取帧时间，split子代取帧时间初始化0。split原随机初始朝向暂近似0，未声称原RNG消费已完全恢复。轻量尸体记录只保留槽位占用，未实现完整尸体几何或死亡变换。

存档仍为version1且核心实体字段严格解析。新增可选 animation 段保存实体视觉字段、slot counter与尸体倒计时；缺失或损坏的视觉附加段按实体顺序重建，保留可用核心游戏并通过load message说明重建。旧存档不能恢复历史精确槽位、尸体占位或眩晕冻结时刻。

验证：扩展现有coverage用例覆盖首槽0、分阶段截断、stun冻结、generation=1选择、split父占位、尸体保存/继续和旧存档兼容；未创建新测试框架。`git diff --check`通过。本子任务遵循主代理要求未运行构建或运行测试，运行结果待统一验证，不能据静态代码检查声明原作动态保真通过。
