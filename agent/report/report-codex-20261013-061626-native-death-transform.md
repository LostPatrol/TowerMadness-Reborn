<!-- Static death geometry investigation outcome and unresolved fidelity scope. -->
# 原生死亡几何静态恢复

已在 `docs/ANIMATION.md` 添加独立死亡绘制规格，覆盖4号护甲、飞行、普通低速地面、快速地面正负随机角共5类分段变换；明确原矩阵调用顺序、RGB变黑但alpha保持1、阴影三次方缩小、分裂父代短暂保留及后续移除。

复用 `p8-animation-evidence` 的绘制证据，并顺序只读导出6个相关函数到 `agent/codex/p8-animation-timing/death-transform`、`death-material`。原ELF常量位型已逐项解析；原directionalshadow模型/材质与Ghidra加载名称相互核对。

确认PGL变换接口仅转发标准OpenGL调用；尸体颜色在每个geoset设置材质之后覆盖RGBA，而非乘材质色。死亡随机角普通结果为整数[-45,44]，原作消耗战斗RNG。视觉独立随机可作为有标记的方向近似，不应伪称原随机序列一致。

未修改核心源、MEMORY、原APK或原数据库；未构建或提交。未完成原作动态对照、跨ABI校验、全部同tick绘制/移除顺序与原随机消费顺序。静态规格本身不构成最终视觉保真验收。
