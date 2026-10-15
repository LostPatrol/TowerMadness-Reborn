<!-- Original sheep placement, state, animation, and abduction evidence. -->
# 羊群表现

> 旧 x64 重建与原作静态规格参考；当前主线见 [X86_CORE_REUSE.md](../X86_CORE_REUSE.md)。原 ARM 地址不能直接用于 x86。旧运行过程已清理，保留的静态导出在 `agent/codex/reference/arm/`。

本规格来自原 ARM `libverde.so` 的保留 Ghidra 数据库，属于静态证据。库 SHA256 为 `92c4663dfa4a8b6e7045516f6f5b31e90090de58b96394cd0e8efcee7e94edee`。Ghidra 地址减 `0x10000` 为 ELF 地址。原始导出位于 `agent/codex/reference/arm/p8-sheep/`，未修改 APK 或数据库。运行时截图不能单独证明随机消费序列或捕获计时已等价。

## 初始化与模型

`GAME_alloc @0029b8e0` 先 `sync_initRand`，紧接着 `ANIMALS_alloc`，之后才分配敌人和塔。羊群使用同一个 game RNG；菜单羊群使用外部随机源。`ANIMALS_generateFieldPoint @002939a4` 从 map 的 `+0x54` short 格数组、`+0x58` 格数中随机取一个格，map `+0x24` 为导航宽度：

```text
cell = field[randomInteger(0, fieldCount)]
x = cell % navWidth + randomFloat()
y = cell / navWidth + randomFloat()
```

上界不含。`GAME_randIntMinMax @0029ce6c` 对 `sync_lrand48` 的有符号值取绝对值，再 `% (max-min)`；不能用浮点随机选择格替代。每只 `ANIMAL_init @002955e8` 依次生成位置、目标各一次，状态设为 1，朝向为 `atan2(targetY-y, targetX-x) * 57.29578f`。ARM 参数已核对，`00295698..002956a8` 将 dy/distance 放 r0、dx/distance 放 r1 调 atan2f，再乘 `002956b4` 的 `e2 2e 65 42`。普通渲染没有额外 90° 偏移。

随后 `ANIMALS_alloc @002956b8` 每只再消费三个 float，顺序是尺寸随机 r0、尺寸分类 r1、颜色 r2：

| 条件 | 原始值 |
| --- | --- |
| r1 < .2 | size = .6 + .1*r0 |
| r1 >= .2 | size = .9 + .2*r0 |
| r2 < .1 | color = 2，blue |
| 其余 | color = 0，white |

literal 字节：`00295ae0 cd cc cc 3d`=.1，`00295ae4 cd cc 4c 3e`=.2，`00295ae8 66 66 66 3f`=.9，`00295aec 9a 99 19 3f`=.6。颜色表为 white/red/blue，只有 `GAME_setMultiplayerColor` 调用统一颜色覆盖；单人分配不覆盖随机蓝羊。新加羊 `ANIMALS_addSheepToFlock` 使用调用者指定的颜色、size，不执行初始尺寸分类。

`ANIMALS_draw @00294cb8`：`translate(x,y,0) → rotateZ(yaw) → renderAnimal`。后者按该羊 size 均匀缩放，直接绘制原模型，不能再做按列表索引的偏移或统一缩小。

## 时钟与状态

`ANIMALS_timestep @00295540` 先按羊插入顺序更新所有羊，再更新所有入侵者，最后 `clock += 25`。clock 初始化为 0；`GAME_timestep @0029df78` 在无活动波时仍更新羊群。暂停与倍速应由游戏 timestep 调度控制，不使用呈现帧或 realtime 改变羊状态。

`ANIMALS_renderAnimal @0029497c` 用 `0x51eb851f * uint32(clock)` 的高32位右移5，取 `&3`，等价 `floor(clock/100)&3`。全羊同步，无 index 相位：

| 状态 | 模型 | 更新 |
| --- | --- | --- |
| 0 Eating | `sheep[_red/_blue]_anim_graze0..3` | 每步一个 RNG float；若 >.98，进入 2 并生成新目标 |
| 1 Grazing | `sheep[_red/_blue]_anim_walk0..3` | 邻居避让与移动 |
| 2 Turning | `sheep[_red/_blue]` 静态 | 朝目标平滑转向 |
| 3 Abduction | 静态羊、beam2 | 捕获翻转与升空 |
| -1 | 不绘制 | 不再更新 |

`AnimalsSyncData_init @002b1b8c` 的常量：`+0x14=600ms`、`+0x18=2000ms`、`+0x1c=25`（升空高度）、`+0x20=.05`（入侵者步长）、`+0x24=.02`（羊步长基数）、`+0x28=.1`（转向因子）、`+0x2c=.98`（吃草停留阈值）、`+0x30=1`（邻域半径）。浮点 literal `002b1c0c..002b1c18` 依次为 `cdcc4c3d / 0ad7a33c / cdcccc3d / 48e17a3f`。

`ANIMAL_updateStateTurning @002951c8`：目标 yaw 与当前 yaw 的差只修正一次 ±360，使之落在 [-180,180]。若 `abs(diff)>2`，`yaw += diff*.1`；否则设置精确目标 yaw，状态改 1。此状态不移动。

`ANIMAL_updateStateGrazing @00294f6c`：遍历所有其他羊（原代码没有排除捕获或已隐藏羊），若距离平方 `<1`，求这些羊位置均值，将目标设为 `2*self-mean` 并进入 2。若目标不在任何 field 格的闭区间 `[cellX,cellX+1]×[cellY,cellY+1]`，重新抽目标。随后即使改成状态 2，当前这一步仍调用 `ANIMAL_move`。

`ANIMAL_move @00294e1c`，令 d=目标距离：若 d<.1，状态变 0，不吸附至目标；否则：

```text
step = .02 * (abs(sin(d*16)) * 3 + .1)
position += (target-position)/d * step
```

这是每25ms的步长，无额外 deltaSeconds。`.1` literal 在 `00294f4c` 为 `cd cc cc 3d`。原状态0吃草转移概率每步约2%，不能替代为固定持续时间。

## 偷羊与捕获

`ENEMIES_update @00296904` 在敌人进入 map `+0x4c/+0x50` 入口格后：调用 `ANIMALS_addIntruder`、扣生命、移除战斗敌人。显示羊和偷羊者必须独立保留，不能按新 lives 立即裁剪羊列表。原羊分配计数、未预订数、未隐藏数是三个字段；`ANIMALS_isFinished @0029552c` 检查未隐藏数归零。

`ANIMALS_getAbductableIndex @00294248` 按插入顺序找到第一只 `reserved==0` 的羊，置 reserved=1。`ANIMALS_addIntruder @0029429c` 保留敌人的原 xyz、size、飞行标记、type/base/submodel、walk帧、walkfactor与speed。generation==1使用存在的subwalk。入侵者离开原战斗列表，不再是可攻击敌人。

`ANIMALS_intruderUpdateStateChasing @0029458c` 重新朝当前目标羊计算 yaw；距离 `<.5` 时进入捕获：羊 state=3、target=入侵者 xy、captureStart=clock、startYaw=原 yaw。否则每25ms朝目标移动 .05。飞行入侵者每步 z减.05，最低 .6（`002946fc 9a99193f`）。

`ANIMAL_updateStateAbduction @00295354` 只在 elapsed<600 时转向：由 startYaw 插值到 `atan2(targetY-y,targetX-x)*57.29578 +180`，取最短差，`yaw=startYaw+diff*(elapsed/600)`。abs(diff)<=2时原代码不写新yaw。elapsed>=2600，羊 state=-1、未隐藏数减1。`ANIMALS_intruderUpdateStateAbducting @00294700` 在 elapsed严格>2600时移除入侵者，故边界上存在一 tick 差异。

`ANIMALS_renderAnimalAbduction @002947b8` 在普通羊外层 `T(x,y,0) Rz(yaw)` 中追加：

| elapsed | 局部矩阵 |
| --- | --- |
| <=600 | `T(.4*f,0,.6*f) Ry(-90*f) S(size)`，f=elapsed/600 |
| >600 | `T(.4,0,.6+25*(elapsed-600)/2000) Ry(-90) S(size)` |

羊使用静态 base model。`.4/.6` literals 为 `00294974 cdcccc3e / 00294978 9a99193f`。beam2另一个push中使用 `S(1,1,50) Rz(fmod(elapsed/10,360))`，RGBA=`(1,1,1,sin(elapsed*pi/2600))`；alpha 在 assembly `00294828` 写入颜色第4项，被伪代码局部变量裁掉。pi literal `0029496c da0f4940`，360 double高字 `00294970 00807640`。该beam围绕羊的yaw和位置，绘制顺序在羊之前。

入侵者捕获前使用走帧 `clock / trunc(walkfactor*trunc(150/speed)) % numWalkFrames`（不含敌人slot偏移）。捕获后静态base model；elapsed>600时在自身原xyz之上追加 z=`25*(elapsed-600)/2000`，scale为原敌尺寸。

最后一只羊被预订时，`ANIMALS_addIntruder` 隐藏pie menu、取消选择、锁输入、静音，并将camera target移至该羊xy/z=10、lookAt target移至羊xy。该收尾不能仅靠显示羊模型覆盖；其他胜负窗口和相机插值需沿现有游戏生命周期处理。

## 阴影

`Animals_alloc @0029391c` 加载 `directionalshadow`（PC相对字符串 `0035cfeb`）。`ANIMALS_renderAnimalShadows @00294a4c` 在羊之前绘制。令：

```text
a = abs(sin((yaw-45)*pi/180))
length = 1.8*(1-a) + 2.6*a
offset = .1 + .15*sin((yaw-135)*pi/180)
fade = elapsed>600 ? max(1-(elapsed-600)/500,0) : 1
```

普通羊fade=1；矩阵：`T(x+offset-4*(1-fade), y+offset-4*(1-fade), .05) Rz(45) S(1.7*fade,length*fade,1) Rz(-135) S(size)`。阴影 constants `00294ca0..00294cb4`=`1.8 /2.6 /.15 /.1 /.05 /1.7`，完整字节在 `literals.tsv`。

入侵者阴影同一原模型，`T(x-4*(1-fade),y-4*(1-fade),.05) S(2*size*fade,2*size*fade,1) Rz(-90)`；fade取目标羊捕获进度。

## 适配边界

当前原生实现已接入：`src/animals.cpp`维护共享MT布局、吃草/转向及追羊/捕获；`src/main.cpp::animalScene`绘制100ms帧、翻转/升空/光束与阴影；`session.cpp`保存可选animals扩展。详细原外层时钟、终局与镜头约束见[SHEEP_SCHEDULING.md](SHEEP_SCHEDULING.md)。25项CPU检查及真实GL采样覆盖现有实现，原作稀疏抓羊/失败截图保留在历史过程记录（已清理），尚非逐帧计时等价验收。

以上明确恢复了布局算法、尺寸分布、姿态、状态顺序、100ms同步动画、追逐/翻转/升空和阴影矩阵。仍需运行时验证：完整原作随机消费顺序、不同地图/游戏种子的逐只精确位置、暂停/倍速/终局时完整调度，以及原GL透明材质状态。不能把新桌面截图或静态恢复称为这些动态验收已通过。
