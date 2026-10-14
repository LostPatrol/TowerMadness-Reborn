<!-- Focused verification scope, fixture boundaries, and reproducible commands. -->
# 玩法验证范围

`src/coverage_checks.cpp`经`Game`公开接口验证116个原始有限模式、四份科技、12类武器、状态/分裂以及存档。无窗口、无SDL初始化、无音频设备；报告写入指定输出目录。

```powershell
$run = Start-Process -FilePath .\release\TowerMadness.exe -ArgumentList '--coverage-test','--output','E:/TowerMadness-Reborn/agent/codex/p7-verification' -WindowStyle Hidden -PassThru
$run.WaitForExit()
$run.ExitCode
```

`coverage-checks.json`包含总结果、25项CPU规则/保存检查及116模式逐项记录。某一模式失败时仍继续检查其他模式；失败返回非零退出码。修改玩法规则后运行此入口，以及`--self-test`的自然完整胜负验证。界面/资源另用`--desktop-test`、`--resource-test`；音频解析用`--audio-test`。

| 范围 | 验证内容 |
| --- | --- |
| 116个有限模式 | 原地图尺寸/terrain闭合、羊圈零距离、实际出生位置可达；以原资金、原波次、无防御推进600个25ms tick，确认自然释放敌人 |
| 四份科技 | 原palette的enabled/disabled入口可见性；用真实build检查允许/拒绝及拒绝不扣费 |
| 固定路径建造/出售 | AshMountain原资金合法草地建塔、扣费和重叠拒绝、出售返还；建造/拆除前后原grid逐项一致、实际出生点可达 |
| 无限及沙盒 | Easy/Medium/Hard运行时首波生成、相同模式确定性、自然释放、不误判有限胜利、重开复现及原沙盒资金 |
| 沙盒seed | 首波逐字段等于原Hard波次源加沙盒10000金币配置hash的生成结果；Hard入口另按100金币配置hash校验 |
| 得分 | 原有限PlainPlane两波、原Gatling Laser stats，额外夹具资金；早发不即时加金币、未全灭不加bonus、全灭第二波加20、最终raw+bonus+40×羊数 |
| 目标选择 | 获取目标采用严格距离，保留目标允许恰好在射程上 |
| beam / plasma | heat乘伤害截断、heat上限；移动弹延迟命中、单次伤害及原Fusion Cannon概率眩晕 |
| rail / homing / mortar | 穿透及线外排除；多炮口伤害分配、延迟溅射；弹道升空、地面爆炸、真实移动目标预测 |
| flak / flame / lightning | 空中目标伤害及同爆炸地面排除；护甲先截断再乘heat、火焰免疫；即时链击、衰减和跳数 |
| mine / slow / boost / bait | 原触发延迟、消耗拆除和地块恢复；弱slow不覆盖强效果、相等效果重置时间；加法boost及严格边界；bait改道和原状态爆炸 |
| 普通tick | stun/slow跨零最后一tick仍生效、严格大于1秒regen及slow缩放、原分裂boss的两子体/金币/状态继承 |
| 动画状态 | 原时间帧/slot持续时间、眩晕冻结、generation-one帧选择、平滑朝向、分裂slot顺序及尸体冻结/释放；没有原作动态对照 |
| realtime效果 | 相同1200tick下独立realtime/4倍速时间映射不改变完整保存与战斗RNG；暂停后rail两秒寿命仍可到期且核心保存不变 |
| 羊群与抓羊 | 固定测试seed的原9样本分配、尺寸/颜色、无波次/暂停时钟；原资金无防御首漏早扣生命、追羊与600/2600ms边界；活动及隐藏一tick关系完整恢复、旧/坏扩展保留核心与RNG |
| Profile / session | 仅输出目录保存profile、最佳成绩、损坏文件备份及默认可用；自然局与活动导弹的逐字段保存/恢复/继续一致，非法版本不破坏当前局 |

## 数值夹具的边界

完整自然胜负保留在`src/checks.cpp`，使用原50金币以及普通建造/升级指令；本文件的武器夹具不代表自然通关。

武器夹具使用真实`Endless:Endless_Hard:PlainPlane`地图/Insane科技，以**原封不动**的PlainPlane有限wave作为目标定义。夹具公开字段设置1000000金币、1000000目标HP及长status timer，便于孤立观察伤害和延迟；这些参数没有写回资产。所有射击、移动、伤害、概率、拆除均由真实`tick`执行，未调用私有方法。护甲1夹具逐字导入Arcade的原wave；免疫导入Boneyard；regen导入AliensLanding。boss/分裂采用PlainPlane原第24波。数值夹具保留原武器damage、射速、范围、护甲倍率、波次速度和regen参数。

地图路径检查针对`pad+(2,2)`实际释放点。UFO的pad是锚点，固定路径地图可能把锚点放在阻挡草地上，例如AshMountain；因此不能把锚点本身必须能走作为原游戏规则。固定路径、大小地图均包含于116逐项记录。

保存恢复检查比较完整`saveState()` JSON，包括会话seed、随机引擎、UFO、敌人、塔、投射物及可选animation/trail/appearance/animals扩展；自然局恢复后推进40tick，导弹夹具先实际移动/采样再保存，并推进20tick验证继续一致。animals保存完整羊、入侵者及独立clock，捕获至隐藏边界继续一致；缺少或损坏扩展使用构造阶段的隔离同seed布局适配，将已扣生命对应羊隐藏，不消费已恢复的战斗RNG，此适配不能恢复旧羊历史；旧版无seed采用离线seed1。保存seed保留重开后的确定性。缺少或损坏trail字段保留核心状态及战斗RNG；旧纯占位扩展不凭空生成尸体，坏视觉扩展降级而保留有效核心。塔appearance扩展保留升级旧模型与起点；其兼容恢复不等于所有升级路径视觉均已验收。短暂渲染Effect/Beam及realtime时钟不属于session合同。测试使用指定输出目录的`coverage-profile`，不会触及真实LocalAppData。

此验证证明已覆盖的本机实现规则可运行，不证明所有116关都能自然获胜，也不构成Android动态等价验证。未经修改的原ARM菜单/PlainPlane/10金币塔/首波已有运行证据，见[ANDROID_REFERENCE.md](ANDROID_REFERENCE.md)，但原作全效果与玩法等价尚未完成。资源存在与原解锁状态的区别见[MODES.md](MODES.md)。
