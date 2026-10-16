<!-- Diagnosis and isolated correction verification for original x86 point sprites. -->
# Madness point-sprite rendering

原 `EffectFlakExplosion_draw` 在爆炸出生帧提交两枚 `size=0, alpha=1` 的点精灵。`PGL_addPointSprite` 原样入队，`PGL_flushPointSprites` 执行 `glPointSize(0)`，触发 `GL_INVALID_VALUE (0x501)`。这不是 `driverBridge` 参数数量或调用约定错误。

## 证据

- 隔离证据目录：`agent/codex/x86-continue-20261016/gl-analysis`。
- 只读 Ghidra 导出：`ownership-export.txt`。`EffectFlakExplosion_draw` 的早期尺寸正比于 `renderTime-startTime`；出生时刻为零。两处提交的 ELF 返回地址为 `0x27999f`、`0x279a28`；`glPointSize` 的返回地址为 `0x24696f`。
- `evidence-trace/diagnostic.log` 记录42次原零尺寸提交及42次真实驱动错误，原游戏仍打印21条世界渲染错误。每枚爆炸两次非法调用在同一帧合并为一个GL错误flag。
- 诊断观察器临时读取真实错误后保留到原guest读取；最终修复验收没有使用这个观察器，没有改变 `glGetError`。

## 最小修复候选

`point-sprite-correction.hpp` 仅在原提交入口排除 `size<=0`。其余参数通过trampoline原样进入原函数，继续使用原batch与material cache；没有把尺寸clamp为1，也没有吞GL错误。

完整ABI：`void __cdecl(int material, float x, float y, float z, float r, float g, float b, float a, float size)`。

入口完整五字节为 `55 89 e5 53 57`，即 `push ebp; mov ebp,esp; push ebx; push edi`。trampoline复制这五字节后跳回原入口+5；原PIC相对调用不在复制范围内。安装前核对这五字节。

头文件SHA256：`4117255CDA5A90367D528F4DAAE80360525F4C03E86370BE265AFAC43244FBA0`。

主线接入点：在 `tower-restore.hpp` 后包含头文件，在 `correctTowerRestoreAllocation()` 后调用 `correctPointSpriteSubmission()`。构建冻结步骤需要复制并哈希新增头文件。隔离 `fixed-host.cpp` 的绝对include和计数观察器仅用于本次验证，不应照搬到主线。

## 验收与限制

`build-fixed` / `evidence-fixed`：原maze3策略的Madness自动战斗自然结束，进程退出0，stdout/stderr均0字节，GL错误为0。零尺寸排除42次；正尺寸提交7180次，其中Flak1470次；原batch实际产生7180次正尺寸驱动调用，非法尺寸驱动调用0次。已查看终局PNG，地图、原塔、敌人与GUI正常绘制。

`build-fixed-visual` / `evidence-fixed-visual`：独立复验同样退出0、stdout/stderr均0字节。零尺寸排除42次；正尺寸提交7203次，其中Flak1471次；真实正尺寸驱动调用7203次、非法尺寸驱动调用0次。`flak-positive-frame.png` 在首次正尺寸Flak绘制帧捕获并人工查看，爆炸、敌人、原塔、激光与GUI正常显示。此候选EXE SHA256为 `F8B8116DEF05935DB76026A7895E1AF08C8E5C74BA63986AE3EDE6B3EF9BD769`；构建manifest冻结宿主，新增头另以上述SHA256绑定。

这是GL修复验收，战斗结局为失败，不能算通关验收。未测试所有地图与全部特效。正尺寸行为保留原代码；非正尺寸图元以前会沿用旧点尺寸误绘，修复后作为零面积图元省略。主源码、原APK与磁盘ELF未修改；原ELF SHA256仍为 `adc78904cc5e89145fb9f4eca720d22cc48a89b634da1f95f4f053477c0ee7f6`。是否接入主线与MEMORY维护由主代理统一完成。
