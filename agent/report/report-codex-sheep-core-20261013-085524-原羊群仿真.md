<!-- Sheep simulation implementation and targeted verification report. -->
# 原羊群仿真执行简报

实现范围：`src/animals.cpp`、`src/game.hpp`、`src/game.cpp`、`CMakeLists.txt`。主代理负责渲染、完整验证、发布以及 MEMORY；保存代理负责会话和核心行为覆盖检查。

## 完成内容

- 按原每只九次共享 MT19937 消费恢复羊位置、目标、尺寸、蓝色概率。原整数选格恢复有符号绝对值取模，位置和目标保留 field 插入顺序。
- 羊按插入顺序更新吃草、避邻移动、转向、捕获；隐藏和捕获羊继续参与原邻居列表。动物钟在所有羊和入侵者更新之后增加 25ms，准备阶段亦更新。
- 入侵者在入口扣生命时独立保留；首只未预订羊优先，保留原大小、模型、子代 walk、speed、walkfactor、飞行高度。恢复每 tick `.05` 追逐、600ms 捕获转向、2000ms 升空时钟、羊隐藏和严格 `>2600ms` 入侵者清理边界。
- 生命周期保持最后羊被预订时锁建造/升级/出售/发波，完整战斗继续直至羊隐藏才结束失败。胜利等待原 activeIntruders（追逐或捕获 elapsed<2600）归零。终局冻结允许保留边界上的入侵者，与原代码相同。
- 原 ENEMIES_alloc 每 pad 在羊初始化之后消费一个随机数，保留 `Ufo.headingRadians`。Game 第三个可选参数提供原会话种子，restart 保留种子；默认 1 供离线确定性验证，生产入口由主代理传入原微秒种子。
- UFO 初始角乘数使用原 literal `0x40490fda`；phase 不逐 tick 累加，保留原 rotationRate：初始/发波10、inbound .2、departure 用移动前 `(50-z)/100+.1`，hover 沿用。呈现用 phase 加速率乘绝对 game time。
- 旧自定义地图缺 enclosure 时使用 gate 格并显示警告，保持可运行；正常原地图严格按 enclosure 顺序消费。

## 验证与限制

- MSVC 14.51 的 `/std:c++20 /W4 /Zs /EHsc` 定向检查 animals.cpp、game.cpp 返回 0，无警告。
- `git diff --check` 返回 0。
- 未单独抢占完整构建或运行原游戏动态比较；最终生产构建、保存恢复、自然策略结果、渲染和发布检查由主代理统一进行，不能把定向语法检查称为动态等价验收。
- 恢复羊和 UFO 共享随机消费必然改变此前未消费羊 RNG 的战斗轨迹，不能用旧 1422 分 golden 锁死结果。
- 避免 `INT_MIN` 有符号绝对值的 C++ UB 采用 int64 扩展；极罕见的该样本采用数学绝对值，原 ARM 二补码边界没有动态证据。
- 完整原随机序列还受既有敌人、塔初始化和死亡近似影响；此次恢复羊群与 UFO 分配序列并不宣称全游戏随机序列已完整闭合。

证据：`docs/SHEEP_VISUALS.md`，定向原导出 `agent/codex/p8-sheep/{core,state,abduction,integration}`；GAME_timestep、终局和 seed 补充由 schedule 子代理从原库复核。没有新分支、worktree、git 提交或推送，没有修改 TODO、README、MEMORY 或历史报告。
