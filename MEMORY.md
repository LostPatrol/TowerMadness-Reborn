<!-- Current native baseline, durable recovery decisions, and unresolved fidelity limits. -->
# Project memory

## 当前基线

Windows x64原生C++20重建已进入P8首轮交互与表现开发基线，游戏品质及原作动态保真验收仍未完成。路线见docs/P8_GAME_QUALITY_PLAN.md：PlainPlane样板继续对照原作，再扩展全玩法并重新验收。私有仓库LostPatrol/TowerMadness-Reborn，本地main；默认不新建分支/worktree。最终交付不得以模拟器运行，开发期允许Android/ARM模拟器；用户已授权安装项目内原作对照环境。

两个原APK均保持不变且不进入Git。既有ARM包SHA-256为0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b；新增Madness_Zero-1.22.apk为真实x86包，SHA-256为7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd。两包同证书、classes.dex和1943项游戏资产相同，仅Info.plist版本码不同；原资源/规则基线无需替换。详见docs/APK_BASELINES.md；跨ABI原生行为尚未逐函数证明相同。

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

无限使用原2000个float32表样本、加权类型、每8波boss及四次HP多项式；地图SHA1种子来自原精确gamemode文本含NUL。Sandbox虽用Hard波资源，必须以10000金币配置计算种子。战斗随机算法恢复为MT19937，当前桌面初始种子1；原会话初始种子/完整消费顺序仍未逐项对齐。

修改入口见docs/DEVELOPMENT.md；原规则索引见RULES.md、PROJECTILE_RULES.md、EFFECT_RULES.md、MODES.md，场景见RENDERING.md。普通任务勿扫描agent/codex、agent/grok；需要证据时按这些文档定向读取。

Game::buildBlockReason/upgradeBlockReason/sellBlockReason与命令共用，试放只复制导航格并调用原距离场，不修改message/资金/占位/RNG；sellRefund保留原全额/75%截断与已排期金额。计时HUD使用nextWaveDelayMs/earlyWaveBonus，不另算规则。

Beam携带原炮口/链击XYZ、攻击kind、塔类型、出生时间及发射时heading/elevation/length/radius；Effect携带bornAt/塔类型/炮口姿态，并有death/split/sheepLeak纯表现事件。原beam2/railspiral/2plane、等级色、双层闪电atlas、plasma四帧/炮口火光、火焰、弹坑、普通爆炸和四层核爆已按静态公式接入；地雷III核爆保留地雷弹坑参数，plasma命中无通用爆炸，flak高度复用原已消费第三个战斗RNG样本。规格及适配差异见docs/COMBAT_VISUALS.md。塔炮口模型朝本地-Y，绘制yaw=heading+90°、pitch=-elevation，body离地0.25，炮座仍在0；turret绕原rotationoffset俯仰。Game::towerElevation与muzzle共用原目标/固定角。首关真实激光mesh-tip与攻击起点检查差距0。炮塔朝向平滑和特殊塔动画仍待恢复。

Enemy::visualHeadingDegrees直接绘制，不另加90°；行走帧为timeMs/(nativeSlot+trunc(walkanimfactor*trunc(150/speed)))，仅generation==1选subwalk。独立1024循环slot、stun刷新冻结时间、尸体float32三秒倒计时及完整冻结姿态存入可选session.animation扩展，旧/坏视觉扩展降级而保留有效核心。原死亡分段翻转/下沉/缩放及RGB变暗、非飞行directionalshadow模型和护甲aura已按静态证据接入；修改入口docs/ANIMATION.md。尸体仅供绘制和slot占位，不参与战斗/胜负。分裂初始yaw和死亡随机角使用近似，尚未插入原combat RNG消费序列。

Renderer新增非均匀scale、UVscale/offset及point-size billboard换算；视觉hash与战斗RNG分离，闪电每实际呈现一帧换两个不同atlas行。mapPreview使用同一场景路径离屏512×288，恢复活跃地图/相机/GL状态，最多缓存16张。火焰fireball采用additive，状态环用线而不把环VBO纹理直接当quad。

所有表现仍用仿真时钟而非原realtime；视觉hash不等于lrand48/drand48，point-size以桌面战场宽度换算、原屏幕字段未标定。导弹多层命中/原trail、flak早期blast、条件式核爆2D闪光、特殊塔动画、塔接地shadowblob及血条/状态辅助仍未完成或近似。短暂effect不保存，冻结尸体及动画状态保存。

## 工具与验证条件

MSVC、CMake、SDL3 3.4.18、OpenGL3.3 core、WIC、nlohmann/json3.12.0；音频新增项目局部stb_vorbis1.22，固定commit和摘要在setup-dependencies.ps1。MSVC静态运行库，运行依赖仅SDL3和Windows系统DLL。VisualStudio18 Community（MSVC14.51、SDK10.0.26100、CMake4.3.1）。Node在C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。build.ps1默认clean-first，曾确认工具保留旧时间戳导致增量编译漏新代码；需快速增量时显式-Incremental。

REA6.4.0、Ghidra12.1.4、jadx-headless-mcp0.7.1、JADX1.5.6位于agent/codex/tools；JDK为C:/java/jdk/21。原数据库agent/codex/ghidra-project/TowerMadness.gpr；Ghidra脚本仅使用独立ghidra-scripts目录，复用-process libverde.so -readOnly -noanalysis。并行headless仍可能争用项目锁，需顺序导出。REA Windows provider拒绝ARM ELF，直接Ghidra为已验证路线。Ghidra地址比ELF值多0x10000，文件映射需确认。JADX全导出7处错误不影响已恢复原生入口。

代理通常127.0.0.1:7897；Windows curl TLS失败可用D:/Git/mingw64/bin/curl.exe。不使用gh，不关闭本机其他服务，尤其MC服务器。任务指令由用户提供；新报告位于agent/report，TODO未经允许不改。

GUI验收注意：Profile::userDirectory使用SHGetKnownFolderPath，覆盖LOCALAPPDATA环境变量不会隔离真实存档；手工测试需先备份profile或使用显式测试目录。现有自动验证入口使用输出目录的测试profile，不会触及真实存档。

## 验收范围与未解决风险

本机RX9070XT已通过22项规则/保存检查、116模式原资金自然开局、116地图实际GL绘制、420PNG/321模型完整加载、36Ogg离线解码。PlainPlane普通50金币操作自然26波保住10羊，鼠标键盘重放胜利/重开/自然失败，15/144FPS调度一致。1280×900和1000×700拾取通过。发布ZIP解压到中文含空格目录、从C:/Windows/Temp及仅Windows PATH启动，菜单和保存继续通过。仍只测试一台Windows，非真正干净OS。

P8首轮原资金50金币、生产SDL输入重放自然26波保住10羊（523200ms、235击杀、最终1422分）及自然失败成立；23项规则/保存检查、三窗口布局和116地图绘制通过。生产菜单/预览不弃局或改相机、胜利解锁/下一战役/重复胜利及真实保存失败保留当前局通过。--visual-test在原10000金币沙盒合法API建塔/发波覆盖12基础类别及5高级形态的17个真实事件和GL截图，不改资金/HP/波/RNG；高级塔直建不等于UI升级路径。测试和截图只证明本机当前实现，不构成原作等价或人类可用性最终验收。

没有真机；项目内Android对照环境与版本路线见docs/ANDROID_REFERENCE.md。旧QEMU2+API24 Play x86已进入Android主屏，但adb仍未认证，尚未安装该设备的x86原游戏；用户已手动允许调试，不能凭此描述transport或游戏成功。没有启用WHPX、改Windows系统功能或重启Windows。行走/死亡/敌人投影有静态实现，动态保真未验；精确炮塔平滑、天空镜头/粒子、羊被抓完整收尾和原手机UI未恢复。桌面持续2/4倍速不同于原临时2倍20秒。完整活动链/多诱饵堵路及同刻排序仍有差异。116关并未全部自然通关；长时高压无限局、跨机器、高DPI跨显示器和实际声音播放未验。购买验证与广告仍排除。

性能检查为完整自然局及其轻负载峰值场景的12000帧、关闭VSync且glFinish同步；不能据此保证所有大地图/多小时局的60FPS。无限已发波历史仍保留在状态数组，超长局内存覆盖不足。旧Android存档兼容、联网/排行/多人不在本次交付范围。
