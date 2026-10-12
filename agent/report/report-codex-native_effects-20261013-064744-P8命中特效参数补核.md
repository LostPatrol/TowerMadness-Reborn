<!-- Focused follow-up evidence for mine, mortar, homing, and flak effects. -->
# P8命中特效参数补核

补充 `docs/COMBAT_VISUALS.md` 的具体命中参数。复用现有mine/mortar导出，仅追加一次只读headless导出flakFireFunc和homingProjectileHit，证据在 `agent/codex/p8-native-effects/impact-params/`。

确认：所有地雷弹坑均为.75*splash、5秒，末级核爆地雷也一样；mortar核弹才用.5*splash、10秒。Mortar特效仅收投射物impactXY和splash，绘制Z由模型层常数确定。Homing保留真实impactXYZ，并加每轴.25*(外部随机-.5)，无地面归零。Flak以weapon.splash为输入半径、enemy.z+第三个战斗随机-.25为视觉Z，前两个样本仍为XY偏移；可保存已消费的第三个样本而不增加RNG消费。

伪代码参数已用ARM调用和立即数交叉核对；没有修改源码、伤害、随机算法或消费顺序，没有运行原游戏，也未新增测试。导弹多层完整draw公式、原动态帧和完整外部随机序列仍未闭合。
