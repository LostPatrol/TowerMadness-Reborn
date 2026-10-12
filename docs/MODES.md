<!-- Original mode resources, endless rules, and implementation entry points. -->
# 关卡与无限模式

`src/catalog.cpp`枚举`assets/data/*.gamemode.json`，保留文件中的map、tech和waves引用。116个文件对应116张地图、84份有限波次配置，范围26–327波；Easy/Medium/Hard/Insane分别被1/1/7/107个模式引用。文件存在只代表资源齐全，不能推导原作已经解锁。

原作`MAPMENU_init`建立PlainPlane、DividedAttention、MuttonsFolly、Madness四个战役入口，后三个分别检查Medium/Hard/Insane进度。`MAPMENU_buildDLCList`建立附加关卡列表，部分条目还受内容包状态约束；通关结束后另将四个战役入口加入附加列表。当前Catalog的`selectableKnown`仅对已单独确认的四个战役入口置true；false表示尚未在该字段编码完整可选条件，不表示地图无效或永远不可选。原作购买验证、广告不在重建范围。

四份technology各含63种塔、12个基础塔入口，必须同时检查塔的`disabled`。Easy/Medium/Hard/Insane分别启用26/43/61/63种塔。Easy基础入口为Plasmatron、Laser、Mortar、Flamethrower、Bait、Mine；Medium增加Slow、Shock、Flak；Hard开放全部12个入口，仍禁用Nuke/H Bomb；Insane全部启用。升级目标同样需要遵守所属tech的禁用状态，不能按模型数量推断可建种类。

## 无限波生成

无限模式不是116个gamemode中的特殊文件。`GAMEMODE_createEndless`在选定地图上即时建立配置：Easy/Medium/Hard波次均使用Insane科技、10只羊、100金币；Sandbox使用Hard波次、10000金币。`MAPMENU_touchesEnded`中四个入口均检查`unlockedHard()`；`MENU_endGame`在DividedAttention胜利后设置该标记，因此无限模式在完成Medium战役时开放。PlainPlane胜利解锁Medium，MuttonsFolly胜利解锁Insane，Madness胜利设置通关标记。胜利只要求游戏结束且剩余羊数非零，不要求10羊全保。桌面入口若直接开放这些模式，应明确这是重建产品的可选策略。

`makeEndlessMode(waveSet,map)`保留原配置；`endlessModeSeedOffset()`重现原字符串及末尾NUL的SHA-1，取字节0–1和5–6的有符号小端16位数，分别加入s1、s3。后一位置是原ARM的非对齐`ldrsh`，不能改成字节4–5。`generateEndlessWave(source,n,seedOffset)`按需产生第n个零基波次，无预设波次上限，也不消耗可变随机状态。

类型选择读取原SO中2000个float32样本，索引为`uint32(s1+s2+s3+n+seedOffset)%2000`。普通types与bosstypes分别按float32累计recurrence再乘其倒数归一化；依次从样本减去权重，首次结果<=0的类型被选中。`(n+1)%bossfreq==0`选择boss。禁止以另一套LCG或随机库代替该样本表。

恢复的数值关系：

- Health：float32顺序计算`a0+a1*n+a2*n²+a3*n³+a4*n⁴`，乘healthmod，再除count，最后向零截断int64。
- Gold：`int(float(n/8+2)*goldmod)`；原getter不使用资源中的gold系数数组。
- Size：`sizefactor*0.5 + ((279470273*n)%65537)/65536`；Offset为原类型offset乘Size。
- Regen：`int64(float(Health)*regen)`；Spawn布尔值展开为0或1，保留count、speed、armor、frequency和动画模型。

极端无限波Health超过int64时，桌面实现饱和到int64最大值；原ARM浮点转换在该溢出范围未验证。正常范围逐步保留float32截断顺序。

## 分数与提前发波

`PLAYER_getFinalScore`返回`rawScore + bonus + lives*40`，不包含剩余金币。击杀generation0敌人获得该波gold，同时加入金币和rawScore；分裂子代不重复发金币。

首次发波的提前奖励为0。已开始的单人游戏，实际发波时奖励为`20 - trunc((timeMs-lastSuccessfulWaveAt)/1000)`；自动在严格超过20000ms时发波，奖励为0。奖励先存入波次计数，待该波计数归零时才计入bonus，不提前到账、不增加单人游戏金币。

计数在dispatch时建立：非分裂波为count，spawn非零为count*3。每次击杀或漏怪递减一次；计数归零的最后一只必须被击杀才能发奖励，最后一只漏怪则无奖励。先前曾漏怪并不会自动取消该奖励。分裂父怪漏掉时不会生成子怪且只递减一次，原波次计数可能因此保持非零；不能以“场上已经没有该波敌人”代替原计数归零。所有计数及奖励应随存档保存。

对应静态证据为`p7-wavebonus-evidence`中的`ENEMIES_sendNextUFO`、`ENEMIES_decrementWaveCounter`和`ENEMIES_damageEnemy`，以及`p7-earlywave-evidence`中的`GAME_processNextWaveCommand`。多人模式标记位于GAME+0x9f8，其奖励行为不同，本项目单人模式使用bonus字段。

## 静态证据与限制

证据位于`agent/codex/p7-mode-evidence`、`p7-rng-evidence`、`p7-menu-evidence`，以及前期`p5-evidence`。关键地址：getTypeForWave `0x2bd8b0`、loadTypesArray `0x2bbde0`、WAVES_getSize `0x2bda24`、WAVES_getHealth `0x2bdc50`、WAVES_getGold `0x2be1f4`、WAVES_updateSeed `0x2be4f8`、GAMEMODE_createEndless `0x2a2124`。样本表Ghidra地址`0x36a89c`，前面的`0x36a898`保存长度2000。地址比ELF虚拟地址多`0x10000`。

`sync_initRand/sync_lrand48/sync_frand48`另外属于战斗随机数，位于`p7-sync-evidence`；它们使用624字MT19937、397偏移和标准tempering。不能与无限波样本表混用。GAME_randFloat将unsigned输出转float32后乘2^-32；GAME_randIntMinMax将输出视为signed32、取绝对值，再对上界减下界取余。

本阶段没有运行原Android程序，也没有手机或模拟器对照；结论依据保留SO的静态指令和资源。独立本地检查包含21组波次/SHA-1精确参考值，以及三种难度各10000波的boss节奏和有效数值检查。战役门槛证据另见`p7-endgame-evidence`的MENU_endGame和`p7-mode-evidence/unlock-xrefs.tsv`。原内容包授权状态尚未重建。
