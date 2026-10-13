<!-- Original ARM combat-effect geometry, timing, and implementation evidence. -->
# Combat visuals

本文公式来自原 ARM `libverde.so` 的静态行为；未经修改的原ARM游戏现已取得菜单、PlainPlane、Laser建造/选中及首波运行画面，见[ANDROID_REFERENCE.md](ANDROID_REFERENCE.md)，但尚未逐项完成效果动态等价验证。代码地址为 Ghidra 地址（ELF 地址 +0x10000）。复核入口是 `agent/codex/p8-native-effects/{effects,support,callflow,flags,impact-params}/` 中的 `pseudocode.c`、`instructions.tsv`、`strings.tsv`；`literals.tsv` 按 ELF PT_LOAD 映射保留字面值，`mesh-bounds.json` 保留资产顶点范围。反编译器会丢失软浮点参数，因此以下公式已交叉核对 ARM 调用与常数，不能直接照抄伪代码函数签名。

## 时钟、随机源与渲染约定

`get_current_realtime 002eba84` 返回 double 秒：`mach_absolute_time()*numer/denom/1e9`。效果记录 `+0` 为出生时间、`+8` 为到期时间、`+0x10` 为绘制回调。`GAME_render 0029efa0` 取同一个 realtime，释放 `expiry<=now` 的效果，然后调用各绘制函数。本文 `age=now-born`；`u` 是各节定义的归一化寿命。不能将时间单位误认为游戏毫秒。当前桌面交互已采用独立realtime，离线默认仿真时钟；暂停、加速和渲染中断后的最终动态表现仍需原作对照。

`GAME_randFloatExt 0029ceac` 为 `float(lrand48())*2^-31`，`GAME_randIntMinMaxExt 0029cec8` 为 `lo+lrand48()%(hi-lo)`，与战斗 MT 随机流分离。火焰还直接调用 `drand48`。重建可以保留独立视觉随机源，但不能声称序列等价，更不能为闪电帧或粒子消耗战斗 RNG。闪电命中高度的战斗随机样本已经属于伤害规则，应复用已有事件字段。

以下矩阵按 OpenGL 列向量表示，依次调用形成 `T*Rz*Rx*S`。原纵向效果网格沿本地 `+Z`，通常使用 `T(origin)*Rz(heading)*Rx(90-elevation)*S(width,width,length)`。`heading` 是原生角度，不是桌面的另外一套朝向偏移。现有模型的 geoset 矩阵仍须保留。

原 `PGL_renderGeosets 0028dfa0` 的颜色参数替换材质 RGBA；当 override alpha<1 时会设置普通 alpha blend。`effects.material.json` 中 beam2、railspiral、lightning、plasmaball、核爆冲击波/尘埃/碎片等使用 alpha blend；fireball、muzzlefx、mushroomcloud 两型使用 additive。本文颜色以替换值为准。相关材质均不写深度；沿用原 texture 采样配置，不要把 railspiral 的重复 UV 钳制到边缘。

## 激光与轨道炮

`EffectBeam_init/draw 002c75c8/002c7640` 加载 **beam2.model**，寿命 `double(float(.1))=0.10000000149011612s`。`beam2.mesh` 为三个交叉面（12顶点），Z范围0..1，XY半径约.5。令 `u=age/lifetime`：

```text
M = T(muzzle) * Rz(tower.heading) * Rx(90-tower.elevation)
    * S(.5*u, .5*u, L)
L = length((enemy.x, enemy.y, enemy.z+WAVES_getOffset(type))-muzzle)
```

宽度从零增至.5；不存在额外 alpha 淡出。`beamFireFunc 002d05cc` 使用当前炮口、当前朝向和俯仰，并按目标距离取 L；不能把模型朝向改成终点 look-at 而丢掉原瞄准状态。`towertype_beam_parse 002d1104` 的颜色：Kilowatt `(1,0,0,1)`、Megawatt `(1,116/255,0,1)`、Gigawatt `(1,1,0,1)`、Terawatt `(0,1,0,1)`、其它 `(0,0,1,1)`。

`EffectRailgun_init/draw 002c7b4c/002c7bd4` 加载 beam2 + railspiral，寿命2秒。`railgunFireFunc 002daaf8` 传 `L=1.5*tower.range`、`r=weapon.radius`，与伤害截断逻辑无关。令 `u=age/2`、`q=1-(1-u)^3`，同样使用炮口与原塔朝向：

| 层 | XY scale | Z scale | 替换颜色 / 存在条件 |
| --- | --- | --- | --- |
| beam2束芯 | `u*(.5*r+6)` | `10*u*L` | 白，alpha=`1-10*u`，仅 `u<=double(float(.1))` |
| railspiral尾迹 | `q*(.15*r+.25)` | `u<=double(float(.1)) ? 10*u*L : L` | 等级RGB，alpha=`2*(1-u)^3`，由GL钳制 |

轨道炮I–V依次红、橙116/255、黄、绿、蓝；其它（Gauss Cannon）为 `(159/255,0,1)`。`railspiral.mesh` XY范围-1..1、Z0..1、UV的V范围0..10，原重复纹理是螺旋外观的一部分。

## 闪电和减速光束

`EffectLightning_init/draw 002c6e78/002c705c` 寿命 `double(float(.2))=0.20000000298023224s`。加载 **2plane.mesh**（两张交叉面，XY范围-.5...5，Z0..1）及 lightning atlas。塔的 `istesla` 整数字段非零时改用 lightning2；`TOWERTYPE_parse 002dcac8` 在 `002dd484–002dd4bc` 将此字段存到 `+0x98`，fire原样传入。

初始化一次 `w=(.75+.25*randFloatExt())*clamp(.25*L,1,2.5)`。每次 draw 重新抽 `[0,4)` 两个不相同的 atlas frame，并绘制两层：

| 层 | XY scale | 普通RGBA | Tesla RGBA |
| --- | --- | --- | --- |
| 外层 | `1.25*w` | `(195/255,217/255,1,1)` | `(1,1,136/255,1)` |
| 内层 | `w` | `(1,1,1,1)` | `(1,1,0,1)` |

两层 Zscale 均为 L，没有寿命淡出。两 atlas 都为256×256，四条子图256×64，位置 `(0,64*i)`。`ATLAS_getSubTextureInfoGL 002cb354` 返回 `(tx,ty,sx,sy)=(0,3-i,1,.25)`，调用顺序是纹理矩阵 `S(sx,sy)*T(tx,ty)`，故 `u'=u; v'=.25*v+.75-.25*i`。应结合现有模型UV/PNG方向约定使用，不能把整张四帧图作为一条闪电，也不能先把ty当作最终UV再乘一次。

`lightningFireFunc 002d5e08` 生成每段 origin、L、heading、elevation。指令 `002d6158/002d616c` 和 `002d66d0/002d66e4` 明确：`heading=atan2(dy,dx)*radToDeg+90`，**`elevation=atan2(dz,L3D)*radToDeg`**，后者不是常规 `atan2(dz,Lxy)`。L仍为sqrt(dx²+dy²+dz²)，所以模型末端不一定正好落在用于衰减的目标点。首段目标Z包含原WAVES offset和额外半幅随机高度；后续段使用已恢复的两个战斗高度样本（见 EFFECT_RULES），不得为绘制重新抽取。

`EffectLightningSlow_init/draw 002c7298/002c73e8` 同为.2秒、2plane + lightning atlas，宽度改为 `.35+.15*randFloatExt()`；外层上述普通浅蓝，内层 `(64/255,150/255,238/255,1)`。`slowFireFunc 002db9b4` 使用炮口到 `enemy.z+WAVES_getOffset` 的3D长度，并直接用塔的原朝向和俯仰；没有闪电链的特殊 atan2 重算。

## 等离子、炮口火光与火焰

`plasmaLoadProjMaterial 002d9c3c` 固定四材料 `plasmaball,plasmaball2,plasmaball3,plasmaball4`。`PROJECTILES_draw 002ad824` 对 model=-1 的投射物执行：

```text
frame = trunc(gameTimeMs * .01) % 4
sizePx = sizeParameter / distance(projectile,camera) * game[+0xbf8] / 320
RGBA = (1,1,1,1)
```

这是 point sprite 的屏幕像素尺寸（`PGL_flushPointSprites 0028e894` 实际调用 glPointSize）；`+0xbf8` 是既有屏幕尺寸字段，其横竖屏含义仍需平台初始化对照。`plasmaFireFunc 002d98fc` 在 damage<450 时设 `sizeParameter=164+.2*damage`，否则254；damage为已经乘boost并截断的投射物伤害，不是基础tech.damage。命中回调只伤害/眩晕，不创建一个通用爆炸。

`EffectPlasmaFlash_init/draw 002c874c/002c87ec` 与 `EffectMortarFlash_init/draw 002c8944/002c89e4` 使用2plane、muzzlefx atlas，寿命 `0x3fc0a3d700000000=0.12999999523162842s`。两者都是 `u=age/life`、`S(.5*u,.5*u,u)`、白色 alpha=`1-u^3`，炮口/heading/elevation来自发射时塔对象。Plasma子图muzzleflash2=(0,64,128,64)，mortar子图muzzleflash=(0,0,128,64)，atlas128×256，UV规则同上一节。核弹的发射也经过 mortarFireFunc 创建该炮口效果。

`EffectFlamethrowerFlame_init/draw 002c76fc/002c788c` 寿命.5秒、材质fireball。init收到origin、塔目标heading/elevation（fire中塔字段+0x1c/+0x24）、tower.range=R、game指针，以及game[+0xbf8]/320作为屏幕倍率k。角度转弧度常数是float .01745329238474369：

```text
v.z = R*sin(elevation)
v.y = R*cos(elevation)*sin(heading-90deg)
v.x = R*cos(elevation)*cos(heading-90deg)
v.z += drand48(); v.y += drand48(); v.x += drand48()
u = age/.5; p = float(u^3)
position = origin + p*v
growth = 1 - (1-1.1*p)^3
alpha = u<.2 ? u/.2 : (u<.7 ? 1 : 1+(u-.7)/(-.3))
sizePx = (128+1024*growth)*k / distance(position,camera)
```

随机偏移是每轴正向[0,1)，并非中心化抖动；位移按寿命立方变化。`002c7750–002c77c4` 修正伪代码将cos/sin传参显示错位的问题，`002c77c8–002c7854` 证明抽样顺序Z、Y、X。这里只是视觉轨迹，禁止反向修改火焰伤害带。

## 普通爆炸、弹坑与核爆

`mortarProjectileHit 002d8038` 普通弹落地同时创建 `EffectGroundDamage(originXY,.75*splash,5s)` 和 `EffectExplosion(originXY,splash)`；Nuke/H Bomb创建 `GroundDamage(originXY,.5*splash,10s)` 和NukeExplosion，H Bomb标志由weapon parser保留。`EffectGroundDamage 002ca900/002ca97c` 加载crater，Z=.02、XYscale=传入半径、随机Z旋转、alpha=`1-(age/life)^3`（替换原材质alpha）。

这里originXY来自**命中时投射物自身**，不是目标的当前位置。普通/核爆初始化函数不接收impactZ，绘制采用下表固定地面高度；不能把projectile.z加到它们的Z上。mortar音效仍使用命中时projectile真实Z，这不改变视觉地面定位。

`EffectExplosion_init/draw 002c8cdc/002c8d68` 寿命1秒。令 `u=age`、R=splash、`a=R*(1-10*u)+.7*R*(10*u)`、`b=.1*R*(1-u)+R*u`：

| 模型 | 平移Z | Scale | 颜色与条件 |
| --- | --- | --- | --- |
| blast | .02 | uniform `4*a` | 白、alpha `1-10*u`，仅 `u<=.1` |
| rangecircle | .2 | `(4*b,4*b,1)` | 白、alpha `1-2*u`，仅 `u<.5` |
| dustcone | .02 | `(2*u*R,2*u*R,sin(pi*u)*R)` | RGB17/255、alpha `1-u^3`，全程 |
| flash | .3 | uniform `a` | 白、alpha `1-10*u`，仅 `u<=.1` |

blast/crater实际用rect VBO、dustcone用cone、flash/debris用3plane；不能仅凭模型名称生成billboard替代。

`EffectNukeExplosion_init/draw 002ca0b0/002ca1ac` 寿命4秒。令 `u=age/4`、R=splash。初始化随机yaw=`360*randFloatExt()`，选择mushroomcloud或mushroomcloud_hbomb，加载nukedust、blast、debris、nukeshockwave、smokepuffflat。当前draw真正使用的四层如下；**不能因为init加载了blast/smokepuffflat就声称draw使用它们**。

| 模型 | 平移Z | Scale | RGBA |
| --- | --- | --- | --- |
| nukeshockwave | .13 | `(.1+28*u,.1+28*u,1)` | 白，alpha=`u<.333 ? 1-3*u : 0` |
| debris | .08 | uniform `.7*R*(u<.5 ? sin(pi*u) : 1)` | 白，alpha=`u<.5 ? 1-2*u : 0` |
| nukedust | .08 | uniform `2*R*sin(pi*u/2)` | RGB68/255，alpha=`u<.5 ? 4*u*u : 2-2*u` |
| mushroomcloud[_hbomb] | 0 | `(2+u,2+u,.1+5*u)` | RGB=`sin(sqrt(pi*pi*u))`，alpha1 |

蘑菇云的几何尺寸并不直接乘R；原mesh本体XY约±.7371、Z.0032..1.1958。蘑菇纹理矩阵围绕(.5,.5)缩放，倍率 `1-.7*(1-u)`；.7实际来自float -.699999988079071转double。模型再绕Z旋转初始随机yaw。氢弹额外旋转 `360*(.5-.5*cos(u*3.141592502593994))`；此处pi是不同精度字面值，不能从伪代码擅自补参数。证据 `002ca5e8–002ca73c`。

核爆还可生成 `EffectNukeExplosion2D 002ca788/002ca7f8`：仅前2秒，将爆点投影到屏幕后绘制blast，scale=`750*(1-2*u)`。创建条件是mortar hit时 `game.towers[+0x28]<4`，该字段语义未在本次闭合，不能无条件加全屏闪光。

地雷所有等级在塔中心 `(tower.x+1,tower.y+1)` 无条件创建弹坑，参数都是 **`.75*splash,5s`**；包括末级核爆地雷，不能套用mortar核弹的`.5*splash,10s`。`mineFireFunc 002d73e4` 先在 `002d7558–002d7580` 创建该GroundDamage，再检查tower type字段+0x64（upgrades数量，通用parser `002dd4fc`）：为零时选NukeExplosion，否则普通Explosion。因此原配置的末级Proximity Mine III使用核爆表现，前两级使用普通爆炸。

## 补充投射物规格

`EffectMissileExplosion 002c921c/002c93ec` 初始化白/蓝flash三选一、smokepuff、blast、debris、rangecircle，大小输入乘3、随机scale=.75+.5*randExt、随机yaw和随机材料；到期是 `1.5+3*randExt` 秒，但draw归一化固定除1.5。多层缩放、透明度及固定像素闪光已闭合并接入Windows，详见[PROJECTILE_VISUALS](PROJECTILE_VISUALS.md)。init最后flag非零选blueflash/2/3，否则flash/2/3。

`homingProjectileHit 002d3c24` 只创建MissileExplosion，不调用普通地面Explosion或GroundDamage。视觉origin=`projectile.xyz+.25*(Rx-.5,Ry-.5,Rz-.5)`，三个R按X/Y/Z顺序取 **GAME_randFloatExt**，各轴偏移[-.125,.125)。传入radius=`projectile.splash(+0xa0)`，最后的白/蓝选择flag=`projectile[+0x60]`（既有pulse标志）。原XYZ被保留，包括空中高度；伤害先在未抖动的projectile原XY发生。指令 `002d3c70–002d3d58`。

`EffectFlakExplosion 002c9a6c/002c9b20` 寿命2秒；scale=(.5+randExt)*输入半径，随机选择flakburst1/2；`u=age/2`，alpha=`1-u^3`。前u<double(float(.1))时主sprite尺寸=`2560*u*scale/distance`，同时在Z+.05处绘制blast；之后仅主sprite，尺寸=`(256+128*u)*scale/distance`。

`flakFireFunc 002d1ac0` 调用参数为 `EffectFlakExplosion_init(effect,game,enemy.x+R1-.5,enemy.y+R2-.5,enemy.z+R3-.25,weapon.splash)`。R1/R2/R3是开头连续三次**战斗**GAME_randFloat；第三个虽不影响伤害，却必须复用已消费样本，不能多抽。没有WAVES_getOffset或Z钳制。该效果XYZ在伤害后重新读取原enemy记录，然后加保留的偏移；伤害XY是在伤害前读取。输入半径取weapon+0x40，与ENEMIES_damageEnemiesInRadius的splash相同，未乘boost；原内部视觉scale随机仍用外部RNG。关键指令 `002d1bac–002d1bb8` 和 `002d214c–002d21a8`。

`EffectTrail 002c9e70/002c9ed4` 是.25秒的白色无纹理四边形，非smoke贴图；端点/宽度及距离/时间采样已复核，按正XYZ对角偏移及additive接入。alpha乘game[+0xa68]，已证明为播放速度倒数；非零图形模式禁止尾迹，缺省正常模式为0。具体矩阵、命中跳过采样、核弹转换保留阈值与前缀例外见PROJECTILE_VISUALS。

仍需动态验收：原设备横竖屏point-size标定、暂停/倍速的realtime结果、模型透明排序、颜色/光照状态、外部随机初始状态及完整消费序列、同帧删除和高帧率闪电闪烁。本文记录静态证据，不能以规格闭合代替原作运行对照。

## 当前Windows实现与修改入口

`Game::fire`及projectile命中回调产生携带塔类型、出生时间和发射姿态的真实事件；`main.cpp::makeScene`组装上述激光、轨道炮、闪电/减速双层atlas、plasma四帧、炮口火光、火焰、弹坑、普通爆炸与四层核爆。地雷III保留地雷弹坑参数而使用核爆；plasma命中不再添加通用爆炸。flak复用既有第三个战斗随机样本的高度，伤害和战斗RNG消费不变。

`Renderable`提供非均匀缩放和UV动画；`$effect:material`直接组合原2plane网格与原材质。Renderer把point-size参数按实际相机距离与桌面战场宽度换算为billboard，火焰沿用additive。闪电图集每次实际呈现换帧，视觉hash独立于战斗RNG。

导弹现在按固定1.5秒绘制进度组装五层原模型/点精灵，保留真实impactXYZ及独立视觉抖动；白/蓝闪光使用对应三张原材质。Flak早期blast使用真正点精灵并与主burst共用到原点的相机距离。两类点大小没有plasma/flame的screen-width/320倍率。原trail在非命中移动更新按距离/弹道时间留下无纹理四边形，不再使用烟点近似；Energy Pulse等原武器名前缀禁用trail，保存可选采样点/阈值。

交互模式通过`Game::advanceEffects`提供独立单调realtime，`addEffect/addBeam`保存秒制出生/到期时间，`makeScene`据此计算短暂效果年龄，暂停时也可到期；离线验证未提供realtime时沿用确定性Game毫秒。原游戏毫秒仍用于移动投射物、plasma帧、塔建造和敌人动作，不能将所有动画改成墙钟。Renderer对point RGBA执行原byte截断，原材质缺省采用unlit texture×RGBA。

明确适配差异：视觉hash不是lrand48/drand48原序列；plasma/flame屏幕字段尚未标定；透明排序及trail继承的深度写入状态待动态对照。增幅rangecircle及Tesla附件已接入，塔建造/升级/出售见[TOWER_VISUALS.md](TOWER_VISUALS.md)。已有塔选中使用原selection纹理及rangecircle，建造预览范围仍为桌面线；非法选中500ms淡出及升级范围realtime淡出未恢复。条件式核爆2D闪光、诱饵屏幕aura、完整羊动作仍未恢复；诱饵/分裂反馈仍有桌面辅助。原暂停/倍速的最终效果需动态验证。

`--visual-test`已在本机原沙盒真实战斗覆盖12基础类别及6高级形态，包含Energy Pulse Missile，共18种；导弹/炮弹等待真正命中并检查trail存在或原前缀禁用，再捕获生产GL画面。它不改资金、HP或战斗RNG，高级塔直接由Game API建造；不证明手机动态等价或UI升级操作完成。
