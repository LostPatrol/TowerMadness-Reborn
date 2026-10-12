<!-- Frozen corpse presentation state, optional persistence, and pending integration validation. -->
# P8 敌人冻结尸体状态接入

执行代理：codex-animation_state。时间：2026-10-13 06:17:29（Asia/Shanghai）。

将既有纯视觉死亡占位最小扩展为 `EnemyCorpse`，修改 `src/game.hpp`、`src/game.cpp`、`src/combat.cpp`、`src/session.cpp`，并扩展现有 `src/coverage_checks.cpp` 尸体保存用例及 `docs/ANIMATION.md`。未改main、math、renderer、MEMORY或TODO；主代理负责原死亡transform渲染接入和统一构建验证。

死亡时保存完整冻结Enemy、独立slot、death-frame timestamp以及 `frame = Game::walkFrame(enemy)`。只读接口 `Game::corpses() const` 返回记录数组；每项提供 `enemy`、`slot`、`remainingSeconds`、`frame`、`deathAngleDegrees`、`drawable`。末代/无分裂尸体沿用3秒float32占位倒计时，分裂父drawable=false并在下一占位update删除。漏怪不创建尸体；slot耗尽时−1的敌人仍可呈现死亡。尸体不进入active enemies，不参与伤害、寻路、波计数或胜负判定。

原death angle的整数区间为[-45,44]度；本次以独立确定性slot/id散列映射此区间，方向明确为近似，不增加战斗RNG消费。原完整随机消费仍未闭合；分裂初始朝向继续近似0。既有projectile死亡可能同tick进入后续敌人update、tower死亡在下tick进入的顺序保持，没有更改游戏计时或核心战斗行为。

可选 `session.animation.slotHolds[].corpse` 保存冻结Enemy（含其animation字段）、frame与deathAngleDegrees。旧附加段仅含slot+remainingSeconds时继续恢复占位，drawable=false，不凭空生成模型/姿态。坏尸体附加数据降级为视觉状态重建，保留有效version1核心游戏，核心实体解析仍严格。

验证：扩展既有用例断言死亡帧/姿态捕获、方向范围、split父不可绘制、完整尸体保存恢复、旧纯占位附加段兼容，以及坏尸体源数据不破坏核心状态或RNG。`git diff --check`通过。本子任务未运行构建/运行检查，结果待主代理统一验证；不将这些断言或静态检查描述为原作动态保真验证。
