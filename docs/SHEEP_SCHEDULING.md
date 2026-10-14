<!-- Static evidence for sheep scheduling, shared RNG initialization, and end-game timing. -->
# 羊群调度与终局

基于原 ARM `libverde.so`（SHA256 `92c4663dfa4a8b6e7045516f6f5b31e90090de58b96394cd0e8efcee7e94edee`），通过保留 Ghidra 数据库 `-readOnly -noanalysis` 定向导出。以下地址是 Ghidra 地址；减 `0x10000` 得 ELF 虚拟地址。新证据在 `agent/codex/p8-sheep-schedule/{core,end,outer,alloc}`，对应 `instructions.tsv` 可核对 ARM 寄存器、分支和调用，`bytes.tsv` 保存关键位置原字节。既有羊状态证据见 [SHEEP_VISUALS.md](SHEEP_VISUALS.md)。本文件描述静态原行为，不表示桌面实现或动态对照已全部通过。

## 同步随机数初始化

原单人会话种子不是固定 1，也不是无限波次配置的 SHA1。`GAMEMODE_load` 调用的 `FUN_002a10b4` 在加载 mode 时调用 `gettimeofday`，把 `tv_usec` 写入 `mode+0x2c`：

| 地址 | ARM 指令 | 小端原字节 |
| --- | --- | --- |
| `002a12d4` | `add r0,sp,#0x10` | `10 00 8d e2` |
| `002a12d8` | `mov r1,#0` | `00 10 a0 e3` |
| `002a12dc` | `bl gettimeofday` | `d6 d3 f8 eb` |
| `002a12e0` | `ldr r0,[sp,#0x14]` | `14 00 9d e5` |
| `002a12e8` | `str r0,[r4,#0x2c]` | `2c 00 84 e5` |

ARM32 `timeval` 的第二个 32-bit 字段是 `tv_usec`，故取微秒部分（通常 0..999999），不是秒或总微秒。此赋值发生在 `param_4` 加载选项分支之前。相同 mode 对象再次分配 game 会复用该字段；不能无依据声称每次重开必重新取种子。

`GAME_alloc`：`0029b91c add r0,r5,#0x2c`，`0029b920 ldr r1,[r10,#0x2c]`，`0029b924 bl sync_initRand`。`sync_initRand` 建立标准 624-word MT19937 状态；首次抽数先 twist，不跳过随机输出。算法原证据在 `agent/codex/p7-sync-evidence`。

初始消费顺序为：

1. `PLAYER_alloc`，只初始化计分、生命和资金，无随机消费。
2. `ANIMALS_alloc`，按插入顺序每羊 9 个 MT 输出：位置的格索引/X/Y，目标的格索引/X/Y，尺寸随机、尺寸分类、颜色。因此 N 只羊消耗 9N 个输出。每个格索引是 signed32 绝对值取模；其余是 unsigned32 转 float32 再乘 `2^-32`，不能交换。
3. `ENEMIES_alloc`，按 pad 顺序每个 UFO 消耗 1 个 `GAME_randFloat`（`0029653c`），初始化角度为 `a=random*pi; angle=a+a`。pi literal `00296834 = da 0f 49 40`，即 `0x40490fda`/`3.1415925f`，比常见 `3.1415927f` 小1ULP；`00296544` 乘法、`0029654c` 加法、`00296550` 写 UFO `+0x2c`。即使尚未发波，这些消费已经发生。
4. `TOWERS_alloc`、`PROJECTILES_alloc` 无同步随机调用；初始对象只有空塔/投射物列表。初始化直接调用及所有 `GAME_randFloat` 静态引用列表见 `rng-callers.tsv`。

例如 10 羊、1 pad 的局，完成上述分配后已消费 91 次 MT 输出。随后 Preparing 中的羊吃草、生成目标也继续消费同一 MT。固定 seed=1 可以作为可重复验证入口，但应标明测试/产品选择，不能称为原作会话种子。

森林使用独立栈上 MT：`MAP_prepareTrees 002a9d34..002a9d48` 以 `r11-0x9e8` 为状态指针、`r1=0` 调 `sync_initRand`。它与 `game+0x2c` 无共享。森林的模型选择及两个位移样本不会推进战斗 RNG。

## 每个 25ms 步长

`GAME_timestep @0029df78` 的确定调用顺序：

| 顺序 | 调用/写入 | 地址 |
| --- | --- | --- |
| 1 | 可选 before-step hook；处理命令 | `0029df98`、`0029dfa0` |
| 2 | arrows spawn/update；wave preview 数量，读取旧 game clock | `0029dfac..0029dfc0` |
| 3 | `game.clock += game.step`，默认 step=25 | `0029dfc4..0029dfd0` |
| 4 | 若 started `game+0x18 !=0`：spawn → projectiles → enemies → wave preview | `0029dfe4`、`0029dfec`、`0029dff4`、`0029dffc` |
| 5 | towers update，含 Preparing | `0029e004` |
| 6 | animals update，含 Preparing | `0029e00c` |
| 7 | 若 started 且 lives<=0，设置 input lock `game+0x1c=1` | `0029e010..0029e02c` |
| 8 | 可选 after-step hook | `0029e040` |

`ANIMALS_update` 直接进入 `ANIMALS_timestep`，后者按羊插入顺序更新，再按入侵者插入顺序更新，最后才 `animal.clock +=25`（`002955c4` 调入侵者列表，`002955c8..002955d0` 递增）。两时钟初始均 0，步内 game clock 已是新值，羊逻辑仍使用旧 animal clock；渲染通常看到递增后的 animal clock。

入侵者在 `ENEMIES_update` 进入入口格时插入；同一步先让全部羊执行一次原状态更新，再让新入侵者追羊。捕获开始发生在入侵者阶段，因此这一步不会再对新进入 Abduction 的羊执行 Abduction 更新。

`GAME_update @0029e184` 按 realtime 追赶固定步长；倍速字段是步长倒数倍率，0 时不推进仿真。它在每个 timestep 前调用 `ANIMALS_isFinished`（`0029e3a4..0029e3b0`），完成则立即返回。它没有以 lives=0 或 input lock 提前跳过 combat。因此最后偷羊开始之后，整个 spawn/projectiles/enemies/towers/animals 调度仍运行，直至最后羊隐藏；不能把失败待结算阶段改成只跑羊群。

## 抓羊与终局边界

`GAME_hasEnded @0029c4b0` 与 `GAME_lockInput` 是两件事：

| 条件 | 原结束判定 |
| --- | --- |
| lives<=0 | `ANIMALS_isFinished`，即 animals `+0xc` 未隐藏数为 0 |
| lives>0 | `ENEMIES_checkWin` 且 `ANIMALS_activeIntruders` 返回 0 |

`ENEMIES_checkWin` 要求原敌人链为空、所有 UFO 状态为3、所有波已发完；原敌人链包含尚未清理的死亡实体。`ANIMALS_activeIntruders @00294004` 对 Chasing 始终返回活跃；对 Abducting 仅在 `elapsed <600+2000` 时活跃。比较地址 `00294074 cmp r1,r2`，`00294078 bcc`（字节 `01 00 00 3a`）。它不是“入侵者列表非空”。

捕获的三个边界必须分开：

- 羊 `ANIMAL_updateStateAbduction` 在旧 animal clock 的 `elapsed>=2600` 时变 `state=-1`、未隐藏数减1。
- 入侵者 `ANIMALS_intruderUpdateStateAbducting` 在旧 clock 的 `elapsed>2600` 时删除；`00294730 cmp r0,r1`，`00294734 ldmials sp!,{r4,r5,r11,pc}` 对 `<=` 直接返回，字节 `30 88 bd 98`。
- 外层 `GAME_hasEnded` 在本轮 `GAME_update` 返回后，用已递增 animal clock 检查。若其他条件均满足，胜利可以在渲染 elapsed=2600 时判定，此时最近羊更新使用2575；失败要等旧 elapsed=2600 的更新隐藏最后羊，再见 animal clock=2625。失败时最后入侵者可能尚保留在列表中并随游戏冻结。

上述 2600/2625 是单步边界，不保证每个呈现帧都出现：`GAME_update` 可一次追赶多步；胜利检查位于整个 update 外面。外层 `FUN_002e0ab0` 的顺序为 `002e0d70 GAME_update` → `002e0d7c GAME_hasEnded` → 若真，`002e0da0 GAME_silence` → `002e0da8 MENU_endGame`。菜单显示后，该外层走 MENU_update 分支而不再调用 GAME_update。

最后一只可预订羊在 `ANIMALS_addIntruder` 被预订时即隐藏 pie menu、取消选择、`GAME_lockInput`、静音，并将 camera position target 移到该羊 XY/Z10，lookAt target 移到该羊 XY。这发生在追逐开始时，并非捕获600ms边界。`GAME_lockInput @0029e04c` 只写 `+0x1c=1`；`GAME_touchesBegan 002f1750..1758`、`GAME_touchesEnded 002f2860..2868` 读该字段并返回。原触摸层锁定已证实；桌面窗口关闭/保存退出属于桌面适配，不能从原 ARM 触摸锁推断应禁止操作系统关闭窗口。

## 入侵者运算顺序

`ANIMALS_intruderUpdateStateChasing @0029458c` 每步重新取目标羊位置与朝向。距离严格 `<.5` 时设置 intruder state=1、sheep state=3，羊 target=入侵者XY，captureStart=旧 animal clock，startYaw=当前羊 yaw；否则按 `(target-self)*.05/distance` 前进。飞行 z-=.05、最低 .6 仅发生在移动分支，捕获开始分支不降 z。

行走帧须保留 ARM float32 运算次序，不能仅由伪代码推测成单次 `150/speed`：

```text
inverse = float32(1.0f / speed)        // 00293ed8..00293ee0
base = trunc(float32(inverse *150f)) // 00293ee4..00293ef0
period = trunc(float32(walkFactor * float32(base))) // 00293ef4..00293f04
frame = unsigned(animalClock / period) % frameCount // 00293f08..00293f18
```

没有 enemy slot 偏移。捕获后改用静态 base/submodel；在 elapsed>600 时，外层原 XYZ/yaw 下追加 `z=25*(elapsed-600)/2000`。原 scale 保留敌尺寸。完整矩阵和羊 beam/shadow 见 SHEEP_VISUALS。

## UFO 与镜头补充

原 `ENEMIES_draw @00297d4c` 并不逐步修改 UFO 初角。`00297ed0..00297f1c` 在绘制时计算：

```text
yawDegrees = (initialHeadingRadians + (rotationRate * .01f) * float(gameClockMs)) * 57.29578f
```

literal `002989e4=0ad7233c`（.01f）、`002989e8=e22e6542`（57.29578f）。UFO `+0x30` 是 rotationRate：alloc 和 sendNextUFO 设10；Inbound 的 `ENEMIES_update 00296a68..00296a6c` 设 `.2f`，Hover 沿用；Leaving 在更新Z之前改为 `(50-oldZ)/100+.1f`。因此应该保存初始相位与当前 rate，不应每 tick 给 heading 累加固定角度。模型姿态随后 `Rx(7°) S(.5)`。Hover 另有 beam，用 realtime 秒×100 取模360旋转，不是 UFO yaw。证据在 `ufo` 和 `ufo-camera-literals.tsv`。

原 `GAME_translateCameraTarget @0029b75c` 和 `GAME_translateCameraLookAtTarget @0029b76c` 实际将剩余位移写入 game `+0xa08..0xa10`、`+0xa14..0xa1c`。`GAME_updateCamera @0029bd9c` 取 realtime 差转 float32 为 dt，每分量计算 `move=dt*delta; move+=move`，平移真实 camera/lookAt，再 `delta-=move`。这是每次 game update 后按实时帧间隔衰减位移，不是每25ms仿真步插值，也不是指数函数精确积分。`CAMERA_translate @002905a4` 加到真实 eye `+0x108/10c/110`，`CAMERA_translateLookAt @002905f8` 加到真实 lookAt `+0x114/118/11c`。最后 `GAME_makeSureCameraIsGood` 施加地图/高度/FOV及eye重建约束；只接入delta插值不能自动宣称完整镜头约束等价。导出在 `ufo` 和 `camera`。

收尾目标取最后一次预订发生时的羊XY快照，不随尚在走动的羊重算。`ANIMALS_addIntruder` 只在这一次写camera deltas，后续 chasing/abducting 更新没有相机调用；它也不修改播放速度字段 `game+0xa68`。因此应保留当时倍速，并固定镜头锚点。

`GAME_makeSureCameraIsGood` **没有终局锁绕过**：入口 `0029b564 ldr r0,[r4,#0xa38]`、`0029b568 cmp r0,#0`、`0029b56c bne 0029b744` 仅在 tutorial/free-camera 标志非零时跳过；整个函数不读 input lock `+0x1c`。最后抓羊不会设置 `+0xa38`。即使传给平移逻辑的虚拟目标为 `(anchorX,anchorY,10)`，每帧最终仍重建：

```text
target = (clamp(interpolatedTarget.x,0,W), clamp(interpolatedTarget.y,0,H), 0)
h = clamp(interpolatedEye.z,.8,50)
theta = clamp(gameYaw,-pi/2,+pi/2)
eye = (target.x -5*sin(theta), target.y +5*cos(theta), h)
fovDegrees = 45 +25*(50-h)/49.2
```

`0029b6e8..0029b740` 从 game `+0xa9c` 取yaw、`+0xa6c` 取默认-5，分别计算cos/sin后重建eye。默认yaw0时收尾趋向 eye `(anchorX,anchorY+5,10)`、target `(anchorX,anchorY,0)`；并不是视线严格垂直。FOV随高度变化，Z10时约65.3252°。

`buildViewMatrix @00290674` 使用up `(0,0,1)`，没有垂直退化时替代up的特殊分支：`00290750/54/58` 读取up字段，`00290770..00290844` 计算forward×up及长度平方，`00290848` sqrtf，`00290858/86c` 等直接除长度归一化。此段无条件判零或备用向量。正常收尾由上述水平距离5的相机约束避免退化，不能私自加一项“原作垂直俯视纠偏”。该证据已在 `agent/codex/p8-camera/projection/instructions.tsv`，无需重开数据库。

## 保真范围

本次已闭合：原 mode seed 的来源、羊/UFO 初始消费、森林 RNG 隔离、Preparing 与战斗调度、输入锁、胜负等待和入侵者边界。未闭合：整局所有武器/分裂/塔视觉随机消费及跨 ABI 浮点一致性、回放/多人种子恢复链、原作与桌面同种子逐帧运行对照。不得以初始化和主循环静态恢复替代这些动态验收。
