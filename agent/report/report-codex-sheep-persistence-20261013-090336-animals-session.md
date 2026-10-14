<!-- Animal session persistence changes and focused native verification results. -->
# 羊群与抓羊保存简报

修改范围：`src/session.cpp`、`src/coverage_checks.cpp`、`docs/COVERAGE.md`。未修改TODO、MEMORY、README、其他source，也未执行Git写入。

## 实现

版本1保存新增可选`animals`：完整羊状态、预订关系、捕获起点/初始yaw、入侵者原wave/generation/模型/帧/位置/尺寸/速度以及独立动物clock。正常保存恢复保持全部字段及共享MT19937，捕获后hidden羊与入侵者保留一tick的边界也可恢复。

可选`sessionSeed`保留真实会话种子，恢复后的重开可复现原分配；旧版缺失时采用离线seed1。UFO角度与角速独立可选保存，旧字段缺失使用原分配默认值，不消费已恢复的战斗RNG。

坏/旧animals扩展只降级动物子系统：采用候选构造阶段的隔离同seed布局，将已扣生命对应的首批羊标hidden/reserved；不重新消耗已恢复的共享RNG，不拒绝有效核心局。此兼容适配无法还原缺失的历史动物状态，恢复消息明确提示重建。

## 验证

由root统一完整编译成功后，以真实`Start-Process -WindowStyle Hidden -PassThru`及`WaitForExit()`运行发布EXE的`--coverage-test --output E:/TowerMadness-Reborn/agent/codex/p8-animals-verified`，实际退出码0。

25项case全部通过，116原有限模式原资金无防御自然开局逐项通过。新增单case验证固定离线seed的9×羊数＋UFO数共享MT分配、首羊尺寸/颜色、idle/pause时钟、四tick批处理、原资金无防御自然首漏立即扣生命而羊仍显示、捕获600/2600ms边界、活动/hidden边界完整保存继续一致、坏wave/旧animals保留核心及RNG、非默认seed保存后重开精确复现。原武器概率与数值期望全部继续通过，未修改金钱/HP夹具来使此次结果通过。

结果：`agent/codex/p8-animals-verified/coverage-checks.json`。此验证证明本机恢复与规则检查通过，不证明Android动态逐帧等价；GUI验收由root承担。
