<!-- Scoped static-analysis report for original combat effect reconstruction. -->
# P8战斗视觉静态规格

本次仅定向读取原ARM libverde.so及既有只读Ghidra数据库，新增 `docs/COMBAT_VISUALS.md`，没有修改战斗/渲染源码、MEMORY、TODO、README、分支或提交。所有过程输出位于 `agent/codex/p8-native-effects/`。

恢复了beam2激光、beam2+railspiral轨道炮、2plane双层闪电/减速、四帧plasmaball、muzzlefx炮口、火焰轨迹、普通爆炸、弹坑及核爆/氢弹的原模型、主要参数与生命周期。重点修正可能的误读：激光宽度随寿命增加而非淡出；闪电每次draw抽不同atlas帧；核爆蘑菇几何不乘splash；效果使用double realtime秒，视觉随机来自lrand48/drand48；第三阶地雷因无后续升级而走核爆分支。

验证采用函数伪代码、关键ARM软浮点/矩阵调用、ELF PT_LOAD字面值映射及原资产描述/mesh顶点交叉检查。四次headless导出均为 `-readOnly -noanalysis`，保留函数/指令/字符串证据。没有运行原游戏，也未将静态证据称为动态等价；此项不需要新增单元测试。

未完成指标：导弹命中多层效果只导出未全部归纳，投射物拖尾采样频率、Flak调用参数、屏幕尺寸/横竖屏标定及核爆2D创建条件字段语义尚未闭合。原实时钟在暂停/加速后的动态表现、外部随机种子和完整消费序列、最终透明排序/材质状态仍需原作运行对照。主代理将决定并实现本轮视觉范围，本文不声称规格全部已接入桌面实现。
