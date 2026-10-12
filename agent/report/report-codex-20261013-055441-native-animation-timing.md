<!-- Static enemy animation investigation outcome and evidence boundaries. -->
# 原生敌人动画时钟静态恢复

已新增 `docs/ANIMATION.md`，记录可实现的原取帧公式、1024槽位分配和释放、减速/眩晕时钟、分裂取帧条件及朝向平滑。未修改游戏源码、MEMORY、APK、Ghidra数据库；未构建或提交。

主要结果：帧持续时间是 `nativeSlot + trunc(walkanimfactor * float(trunc((1.0f / speed) * 150.0f)))`，动画槽位不等于当前桌面共享ID。减速不减慢取帧；眩晕使用最后一次命中记录的游戏时间。subwalk仅用于generation==1。朝向每更新按最短角差乘以`deltaMs*0.005f`前进，2度以内不更新，眩晕时冻结。

槽位首值0，逐次从计数器环形扫描1024槽；死亡父代可能下次更新立即释放，末代尸体按float32的3秒倒计时延迟释放；漏怪立即释放。恢复完整取帧必须增加独立槽位/计数器/尸体占位以及眩晕时刻，不能简单替换距离公式。

验证方式：顺序执行Ghidra `-readOnly -noanalysis`定向导出17个函数，核对ARM寄存器流和两次整数截断；从原ELF PT_LOAD映射读取转向常数的字节值。证据保存于 `agent/codex/p8-animation-timing` 及其 `lifecycle`、`removal` 子目录，绘制证据复用 `agent/codex/p8-animation-evidence`。

限制：没有动态原作对照；未证明跨ABI等价；未穷尽同一tick全部调用顺序或PGL内部矩阵约定；旧桌面存档无法推回历史原槽位。未将这些静态结论宣称为完整视觉保真验收。
