<!-- Current native baseline, durable recovery decisions, and unresolved fidelity limits. -->
# Project memory

## 当前基线

Windows x64原生C++20重建停在P8首轮交互与表现基线，保留作参考，不再常规推进。2026-10-15用户通过agent/task/02-x86.md明确选择原x86核心复用路线，目标为完整Windows移植，优先复用原代码与GUI；旧重建不删除，固定参照Git标签reconstruction-reference-20261014/f4e8e08，原src/release保留。完整移植验收尚未完成。现状/证据/决策点见docs/TECHNICAL_ROUTE_REVIEW.md。私有仓库LostPatrol/TowerMadness-Reborn，本地main；默认不新建分支/worktree。最终交付不得以模拟器运行，开发期允许Android/ARM模拟器；用户已授权安装项目内原作对照环境。

两个原APK均保持不变且不进入Git。既有ARM包SHA-256为0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b；新增Madness_Zero-1.22.apk为真实x86包，SHA-256为7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd。两包同证书、classes.dex和1943项游戏资产相同，仅Info.plist版本码不同；原资源/规则基线无需替换。详见docs/APK_BASELINES.md；跨ABI原生行为尚未逐函数证明相同。

原x86核心复用已通过独立PlainPlane可行性门槛：tools/x86-reuse构建Win32宿主，直接执行原ELF机器码及原GAME/PGL；Windows鼠标经最小UIKit对象进入原EAGLView/GAME触摸逻辑，原轮盘建迫击炮、原按钮发波并发生击杀。激光原命令短局也成立。无需Android/CPU模拟器；现有产品源码/发行版/用户存档未替换。入口tools/x86-reuse/play.cmd，规格docs/X86_CORE_REUSE.md，原库副本及GS元数据在agent/codex/x86-reuse。优先继续原核心路线验证，但未授权以此单关原型直接替换发行主线。

复用宿主仍固定480×800、静音；支持原gamemode参数、原升级/出售/终局/倍速验证，新增menu/menuauto原菜单驱动。已真实走通原主菜单NEW GAME→MAPMENU→CAMPAIGN→PlainPlane TAP TO START→原GAME；033454/033918/034034截图由codex目视确认，尚无稳定HUD/原菜单战斗/暂停冻结/重开/胜利验收。关键ABI：MENU_setResolution接收float逻辑尺寸，不能传整数480/800位模式，否则UI投影塌缩。此前原命令激光建造/升级/出售实际50→40→20→50，原鼠标/倍速路径正常退出；五图开局正常；无塔和两激光4倍速均原失败，不等于通关。原进度/成绩CFString键与格式、unlocked读写、原保存仍在修复，不能把当前空profile返回0当持久进度。run.ps1支持OutputDirectory/BuildDirectory，开发产物在agent/dsh/build；旧play.cmd仍用旧agent/codex构建，发布入口未切换。GS/锁只支持单线程，未知导入/selector终止。全武器/关卡、长局、保存、DPI/透明状态/跨ABI数学未完整验；robosteal_walk0..19缺失未闭合。不要把已成功场景描述为完整移植或逐像素等价。

运行入口release/TowerMadness.exe；发布ZIP含SDL3.dll、assets、许可和中文README。12类武器分别实现beam、plasma、railgun、homing、mortar、flak、flamethrower、lightning、mine、slow、boost、bait，读取四套tech及原禁用/升级配置。116有限模式对应116地图；四战役按胜利进度解锁，其余112通过明确的桌面资源库开放，不能称为原作全部已解锁。DividedAttention胜利解锁Hard及无限；MuttonsFolly胜利解锁Insane。三无限难度和沙盒使用当前地图。

中文菜单/HUD、原塔图标、浏览/建造/选中状态、合法预览/取消、详情升级对比与精确退款、暂停、1/2/4倍速、重开确认及胜负结算已接入。关卡浏览使用独立Game及原地图GL缩略图，明确开始后才替换当前局；显示真实科技/波数/资金/成绩及保存摘要，胜利后记录新解锁并可直接下一战役。Game终局拒绝建/升/售/发波；按钮和键盘共用desktopInput，镜头事件经desktopEvent隔离菜单/模态/详情/建造栏，失焦清理拖动。Esc依次取消确认/菜单/建造/详情，再暂停；退出有明确按钮。战场矩形同时用于绘制、投影和拾取，900×700时也不让HUD遮住出生点和羊圈。修改入口docs/FRONTEND.md。

Profile和完整局内状态位于LocalAppData/TowerMadness-Reborn/profile.json；带版本、临时文件替换、损坏文件备份，保存实体、投射物、UFO、状态、计分、活动波计数及MT19937。菜单/重开确认冻结仿真，30秒及退出自动保存。requestExit共用窗口关闭/菜单退出：保存失败保留当前局，再次明确退出才丢弃未保存进度，手动保存成功清除失败状态。声音默认两项音量0，绝不初始化设备；原36Ogg已离线解码，用户自行调高音量后才可打开音频。

Android包com.limbicsoftware.towermadnesszero，minSdk9/targetSdk17。既有静态基线仅armeabi，新增对照包仅x86。核心libverde.so是Apportable Objective-C/iOS移植，非Unity；ARM库4324动态函数符号，Ghidra识别10390函数。保留数据库供定向导出，不把伪代码当可编译源码。

## 数据、规则与修改入口

tools/prepare-assets.mjs可重复生成202统一TMMS网格、420标准PNG、36实际Ogg及全配置，116gamemode显式引用闭合。2广告CgBI图排除。清单6个旧缺失边中cube/default已由PGL_init证实为内置checkerboard/UVscale8并实现；剩余5个全局旧边未被单人模型使用，保留原清单。材质缺tex表示纯色；必须读取texture.file别名，例如woodmapboxshadow对应uialphaelements。

模型矩阵保留原行序，渲染转列序；模型UV配PNG topdown。地表atlas为bottom-origin，格底V=(H-pos-size)/H、格顶V=(H-pos)/H，勿再次反转模型UV。地图专用basetileAtlas/transitionAtlas、原组合过渡及森林已恢复。树林按x外/y内、MT种子0、unsigned modulo选模型、两次随机*.47轴偏移；缺省grove1..5，显式空treeModels禁树。

PGL_loadTexture已证实缺省NEAREST min/mag、REPEAT S/T，再应用min/mag/wraps/wrapt；材质缓存保留texture描述名与file别名及采样器，不要再次统一CLAMP。skybox底面UV超出1、moon tscale20依赖重复。GAME_drawSkybox将原模型平移到相机位置；已接入普通/月球天空模型，精确镜头仍待动态对照。证据agent/codex/p8-render-evidence。

PlainPlane地形11×16、导航22×32，每tile2单位。terrain0可走不可建，1可建且fixedpath时阻挡，2阻挡不可建。建塔使用bit2及实际塔占位检查；固定路径塔不改变导航格。开放地图8邻接禁切角，塔2×2，可奇数格放置。pads是UFO锚点，实际spawn=pad+(2,2)，不能要求锚点可走；AshMountain等固定路径锚点位于阻挡格。诱饵bit1|bit8可走，建成后替代羊圈距离场目标。

固定25ms、自动发波严格>20s。第一波earlybonus0，其余提前奖励20-trunc(elapsedMs/1000)，待原波计数归零且最后移除为击杀才加入bonus，单人不加钱。原计数spawn非零一律count*3；分裂父怪漏掉只减1，不能按场上清空补发奖励。最终分=score+bonus+40*lives。售塔延迟后退累计投资全额/75%且扣min(score,10)。

伤害为整数HP。beam先trunc(damage*boost*heat)再护甲截断；flame先boost/护甲再heat截断；flak/lightning/mine忽略boost。线攻击比较垂直距离平方与原radius本值。slow是速度减少比例，弱效果延长强效果，强/相等可重置成更短时间；跨零最后一步仍有效，缩放regen。分裂子代继承slow/stun，heat重置1；deque保持引用稳定，子代可参与后续连锁跳跃/塔/投射物。

无限使用原2000个float32表样本、加权类型、每8波boss及四次HP多项式；地图SHA1种子来自原精确gamemode文本含NUL。Sandbox虽用Hard波资源，必须以10000金币配置计算种子。战斗随机为MT19937，交互加载mode时以时间微秒部分作为sessionSeed；离线检查默认1，相同mode重开保留种子。羊初始化9N输出后才分配UFO每pad1输出，森林用独立MT0。整局随机消费/跨ABI浮点仍未逐项对齐。

修改入口见docs/DEVELOPMENT.md；原规则索引见RULES.md、PROJECTILE_RULES.md、EFFECT_RULES.md、MODES.md，场景见RENDERING.md。普通任务勿扫描agent/codex、agent/grok；需要证据时按这些文档定向读取。

Game::buildBlockReason/upgradeBlockReason/sellBlockReason与命令共用，试放只复制导航格并调用原距离场，不修改message/资金/占位/RNG；sellRefund保留原全额/75%截断与已排期金额。计时HUD使用nextWaveDelayMs/earlyWaveBonus，不另算规则。

Beam携带原炮口/链击XYZ、kind、塔类型、仿真出生元数据及发射时姿态；Effect/Beam另有独立realtime寿命。原beam2/railspiral/2plane、双层闪电atlas、plasma帧/炮口火光、火焰、弹坑、爆炸/核爆已按静态公式接入；地雷III保留地雷弹坑，plasma命中无通用爆炸，flak复用原第三个RNG样本。塔炮口本地-Y、当前heading绘制+90°、pitch=-elevation，body离地.25，turret绕原rotationoffset。lightning/boost/mine/bait原Aim为no-op，当前固定零仰角，lightning起点恢复原挂点；首关Laser mesh-tip与攻击起点差距0。完整原visual/target双角度、随机初始yaw/idle、native角差gate和调用顺序仍未恢复。见docs/COMBAT_VISUALS.md、TOWER_VISUALS.md。

Enemy::visualHeadingDegrees直接绘制，不另加90°；行走帧为timeMs/(nativeSlot+trunc(walkanimfactor*trunc(150/speed)))，仅generation==1选subwalk。独立1024循环slot、stun刷新冻结时间、尸体float32三秒倒计时及完整冻结姿态存入可选session.animation扩展，旧/坏视觉扩展降级而保留有效核心。原死亡分段翻转/下沉/缩放及RGB变暗、非飞行directionalshadow模型和护甲aura已按静态证据接入；修改入口docs/ANIMATION.md。尸体仅供绘制和slot占位，不参与战斗/胜负。分裂初始yaw和死亡随机角使用近似，尚未插入原combat RNG消费序列。

Renderer保留非均匀scale、UVscale/offset和point billboard；原point RGBA按byte截断。视觉hash不消耗战斗RNG，闪电每呈现帧换不同atlas行。原PGL未初始化固定光照，模型缺省texture×RGBA unlit，移除额外方向光。Gameplay相机从正Y看导航格整数中心，eye=(centerX,centerY+5,Z)，原Z50/FOV45，低Z增加FOV，screen-right=-worldX；project/pick/pan/billboard共用基准。resetCamera(false)保留原Z50；默认桌面战场fit与放宽最大Z为适配。mapPreview离屏512×288、恢复活跃地图/相机/GL状态、缓存16张。见docs/CAMERA.md、RENDERING.md。

导弹五层命中按固定1.5s绘制分母接入，随机expiry1.5..4.5s不拉长动画；保留impactXYZ并使用独立视觉抖动，白/蓝flash三材料。Flak早期blast为真正点精灵，与主burst共用到原origin的相机距离。Flak/导弹固定像素大小没有plasma/flame的屏幕倍率。原trail为无纹理additive四边形，按原距离/弹道秒阈值在非命中更新采样；Energy等武器前缀禁用。播放速度倒数仅影响trail alpha，正常图形模式0启用trail。可选projectile.trail存采样点/阈值，旧/坏视觉字段保留有效核心继续。规格/证据/入口见docs/PROJECTILE_VISUALS.md。

交互短暂Effect/Beam由SDL单调realtime老化，暂停/菜单/失焦继续消退，倍速仅改变仿真及trail alpha；首次接入保留已有事件年龄。离线Game使用确定性仿真秒。短暂事件与realtime不保存；尸体、塔appearance、动画和trail采样保存。塔build/sell按原scale/Z升降、upgrade先旧模型缩回再新模型升起，独立底座、directionalshadow、Tesla双ring及boost rangecircle已接入。出售刚升级的塔使用当前模型，Tesla售塔首半段仍绘制。选中使用原selection rect/Z.1/XY2及rangecircle/Z.15/range/颜色；非法500ms淡出、升级橙色范围淡出及建造范围预览仍未完整恢复。视觉hash不等于lrand48/drand48，原plasma/flame屏幕倍率未标定，trail透明排序、bait屏幕aura、条件核爆2D闪光未闭合。

src/animals.cpp维护原尺寸/颜色、吃草/移动/转向及预订羊的入侵者，main.cpp::animalScene组装100ms同步帧、600ms翻转/2000ms升空/beam/shadow。每25ms先羊再入侵者，末尾递增独立animal.clock。敌人漏出立即扣生命，lives0锁输入但继续完整战斗至最后羊隐藏，胜利等待无活跃入侵者；隐藏>=2600ms、入侵者删除>2600ms不可合并。最后预订时固定镜头锚点，realtime残余位移插值，最终eyeY+5/Z10/FOV约65.3252，保留倍速；重试重置桌面镜头。可选session.animals保存羊/入侵者/时钟/锚点，坏或旧字段用隔离同seed构造布局兼容，不能消费已恢复战斗RNG，也不等于恢复旧羊历史。UFO保存初始heading/rate，绘制角度来自game clock，hover beam用realtime。静态依据见SHEEP_VISUALS.md、SHEEP_SCHEDULING.md；完整原作动态等价尚未验。

## 工具与验证条件

MSVC、CMake、SDL3 3.4.18、OpenGL3.3 core、WIC、nlohmann/json3.12.0；音频新增项目局部stb_vorbis1.22，固定commit和摘要在setup-dependencies.ps1。MSVC静态运行库，运行依赖仅SDL3和Windows系统DLL。VisualStudio18 Community（MSVC14.51、SDK10.0.26100、CMake4.3.1）。Node在C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。build.ps1默认clean-first，曾确认工具保留旧时间戳导致增量编译漏新代码；需快速增量时显式-Incremental。

REA6.4.0、Ghidra12.1.4、jadx-headless-mcp0.7.1、JADX1.5.6位于agent/codex/tools；JDK为C:/java/jdk/21。原数据库agent/codex/ghidra-project/TowerMadness.gpr；Ghidra脚本仅使用独立ghidra-scripts目录，复用-process libverde.so -readOnly -noanalysis。并行headless仍可能争用项目锁，需顺序导出。REA Windows provider拒绝ARM ELF，直接Ghidra为已验证路线。Ghidra地址比ELF值多0x10000，文件映射需确认。JADX全导出7处错误不影响已恢复原生入口。

代理通常127.0.0.1:7897；Windows curl TLS失败可用D:/Git/mingw64/bin/curl.exe。不使用gh，不关闭本机其他服务，尤其MC服务器。任务指令由用户提供；新报告位于agent/report，TODO未经允许不改。

02-x86由本机dsh承担主要研究实现与验收。用户澄清受限的是ChatGPT预算，dsh额度充足，不得要求省调用而降质量。过程目录agent/dsh不进Git。原Windows shell沙盒因SetNamedSecurityInfoW Win32 5不可用；保持workspace-write，不改ACL，禁用pwsh。已挂载有限project_native工具（bridge/load.patch.yml），只提供固定build/run/只读ghidra/status。build编译固定changes/tools/x86-reuse/host.cpp，输出agent/dsh/build；默认产品build仍编译product源码，每次HostSource明确覆盖CMake缓存。codex阶段结束才apply_patch提升已验副本，注意开发exe可能新于product cpp。原菜单主体/page/selector/保存流函数导出位于menu-evidence；独立qa/requirements与findings是带时间的快照，旧F1/F2不可见/首关未启动已解决，CFString/成绩/原保存等仍重要。主会话session-e2ef9fe5-098b-452f-a3a2-f9764733371d，配置workspace.patch.yml及bridge/load.patch.yml；续接先定向读status/stage7.events、bridge/jobs与qa快照，核对活跃进程后继续，避免重复分析。

GUI验收注意：Profile::userDirectory使用SHGetKnownFolderPath，覆盖LOCALAPPDATA环境变量不会隔离真实存档；手工测试需先备份profile或使用显式测试目录。现有自动验证入口使用输出目录的测试profile，不会触及真实存档。

## 验收范围与未解决风险

本机RX9070XT通过25项规则/保存、116模式原资金自然开局、116地图实际GL绘制、420PNG/321模型完整加载、36Ogg离线解码。1280×900和1000×700拾取通过；--smoke-test实际480×800原相机四角与独立静态投影最大差.006逻辑像素，并覆盖合法50金币建/升/售模型阶段、appearance保存/旧坏字段降级、真实wall-clock暂停Rail两秒到期/核心RNG冻结及自然羊群收尾序列。仅一台Windows，非真正干净OS；GL/投影检查不等于原作逐像素动态等价。当前打包和独立目录验证以2026-10-14收束报告为准。

P8首轮原资金50金币、生产SDL输入自然26波10羊（523250ms、235击杀、1422分）与自然失败成立。三窗口布局、菜单/预览不弃局/改相机、胜利解锁/下一战役/重复胜利、退出保存失败保留当前局通过。--visual-test原10000金币沙盒合法API建塔/发波覆盖12基础类别及6高级形态，导弹/炮弹检查真实命中及trail，不改资金/HP/波/RNG；高级直建不等于UI升级路径。只证明本机当前实现可运行，不构成原作等价或人类可用性最终验收。

没有手机；API17 default ARM＋SDK25.2.5 classic＋原Google签名完整Phonesky4.3.11已真实运行未改ARM APK，菜单、PlainPlane、50金币10羊、10金币迫击炮建造/选中及首波有PNG。原UI显示V1.21、manifest1.22，摘要不变；单迫击炮参考局第4波丢完10羊，未原作通关。原作羊群稀疏抓羊/失败PNG在agent/codex/p8-original-sheep-runtime，runtime-0016有4生命和抓羊束、runtime-0024为失败菜单；不是逐帧/同种子/计时对齐验收。新x86 APK在官方API24 Google APIs仍libcxx.so/系统linker unwind SIGSEGV，旧Play5586历史未授权；不改WHPX/系统功能或重启Windows。原ARM动态证据支持镜头/场景方向，完整动态等价未验。GAME_togglePlaySpeed静态1/2/4已知，原UI倍率未实测。完整活动链/多诱饵堵路/同刻排序与Menu world仍未恢复；116关未全部自然通关，密集长时无限、跨机器、高DPI跨显示器、实际声音播放未验。购买验证/广告排除。

性能检查为完整自然局及其轻负载峰值场景的12000帧、关闭VSync且glFinish同步；不能据此保证所有大地图/多小时局的60FPS。无限已发波历史仍保留在状态数组，超长局内存覆盖不足。旧Android存档兼容、联网/排行/多人不在本次交付范围。

可复用参考AVD TM_Original_API17_ARM_clean/5592，旧ARM5580已正常停止并保留数据，旧Play5586保留。Phonesky4.3.11普通安装依赖SHA256 04897ec9492ab119c137b18a021ec931f547c91faeb1d73d4a36b78592e2f03a，含dex、1089 payload签名摘要及Google证书已验证，来源/复现见docs/ANDROID_REFERENCE.md。勿再普通安装无dex的预优化Phonesky、拼接ODEX/改签名/模拟响应/注入guest keys。续接须核验实际进程/adb，历史PID不是存活证据。原羊RNG消费影响战斗，不能用独立视觉随机冒充原会话。原作旧截图的25升级来自被误标的迫击炮，不作为激光依据；原价Laser10→20、Mortar10→25静态链见docs/UPGRADE_PRICE.md；普通UI另实测Mortar50→40→15、混合塔局Laser40→30→10，身份依据见ORIGINAL_TOWER_IDENTITY。资源价格不改。
