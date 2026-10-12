<!-- Current native baseline, durable recovery decisions, and unresolved fidelity limits. -->
# Project memory

## 当前基线

Windows x64原生C++20重建已推进到P7发布候选。私有仓库LostPatrol/TowerMadness-Reborn，本地main；默认不新建分支/worktree。原APK不进入Git且保持不变，SHA-256：0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b。

运行入口release/TowerMadness.exe；发布ZIP含SDL3.dll、assets、许可和中文README。12类武器分别实现beam、plasma、railgun、homing、mortar、flak、flamethrower、lightning、mine、slow、boost、bait，读取四套tech及原禁用/升级配置。116有限模式对应116地图；四战役按胜利进度解锁，其余112通过明确的桌面资源库开放，不能称为原作全部已解锁。DividedAttention胜利解锁Hard及无限；MuttonsFolly胜利解锁Insane。三无限难度和沙盒使用当前地图。

菜单/建造栏/升级/出售/暂停/1、2、4倍速/重开/相机可用。Profile和完整局内状态位于LocalAppData/TowerMadness-Reborn/profile.json；带版本、临时文件替换、损坏文件备份，保存实体、投射物、UFO、状态、计分、活动波计数及MT19937。菜单冻结仿真，30秒及退出自动保存。声音默认两项音量0，绝不初始化设备；原36Ogg已离线解码，用户自行调高音量后才可打开音频。

Android包com.limbicsoftware.towermadnesszero，minSdk9/targetSdk17，仅armeabi。核心libverde.so是ARM32 ELF、Apportable Objective-C/iOS移植，非Unity。4324动态函数符号，Ghidra识别10390函数；保留数据库供定向导出，不把伪代码当可编译源码。

## 数据、规则与修改入口

tools/prepare-assets.mjs可重复生成202统一TMMS网格、420标准PNG、36实际Ogg及全配置，116gamemode显式引用闭合。2广告CgBI图排除。清单6个旧缺失边中cube/default已由PGL_init证实为内置checkerboard/UVscale8并实现；剩余5个全局旧边未被单人模型使用，保留原清单。材质缺tex表示纯色；必须读取texture.file别名，例如woodmapboxshadow对应uialphaelements。

模型矩阵保留原行序，渲染转列序；模型UV配PNG topdown。地表atlas为bottom-origin，格底V=(H-pos-size)/H、格顶V=(H-pos)/H，勿再次反转模型UV。地图专用basetileAtlas/transitionAtlas、原组合过渡及森林已恢复。树林按x外/y内、MT种子0、unsigned modulo选模型、两次随机*.47轴偏移；缺省grove1..5，显式空treeModels禁树。

PlainPlane地形11×16、导航22×32，每tile2单位。terrain0可走不可建，1可建且fixedpath时阻挡，2阻挡不可建。建塔使用bit2及实际塔占位检查；固定路径塔不改变导航格。开放地图8邻接禁切角，塔2×2，可奇数格放置。pads是UFO锚点，实际spawn=pad+(2,2)，不能要求锚点可走；AshMountain等固定路径锚点位于阻挡格。诱饵bit1|bit8可走，建成后替代羊圈距离场目标。

固定25ms、自动发波严格>20s。第一波earlybonus0，其余提前奖励20-trunc(elapsedMs/1000)，待原波计数归零且最后移除为击杀才加入bonus，单人不加钱。原计数spawn非零一律count*3；分裂父怪漏掉只减1，不能按场上清空补发奖励。最终分=score+bonus+40*lives。售塔延迟后退累计投资全额/75%且扣min(score,10)。

伤害为整数HP。beam先trunc(damage*boost*heat)再护甲截断；flame先boost/护甲再heat截断；flak/lightning/mine忽略boost。线攻击比较垂直距离平方与原radius本值。slow是速度减少比例，弱效果延长强效果，强/相等可重置成更短时间；跨零最后一步仍有效，缩放regen。分裂子代继承slow/stun，heat重置1；deque保持引用稳定，子代可参与后续连锁跳跃/塔/投射物。

无限使用原2000个float32表样本、加权类型、每8波boss及四次HP多项式；地图SHA1种子来自原精确gamemode文本含NUL。Sandbox虽用Hard波资源，必须以10000金币配置计算种子。战斗随机算法恢复为MT19937，当前桌面初始种子1；原会话初始种子/完整消费顺序仍未逐项对齐。

修改入口见docs/DEVELOPMENT.md；原规则索引见RULES.md、PROJECTILE_RULES.md、EFFECT_RULES.md、MODES.md，场景见RENDERING.md。普通任务勿扫描agent/codex、agent/grok；需要证据时按这些文档定向读取。

## 工具与验证条件

MSVC、CMake、SDL3 3.4.18、OpenGL3.3 core、WIC、nlohmann/json3.12.0；音频新增项目局部stb_vorbis1.22，固定commit和摘要在setup-dependencies.ps1。MSVC静态运行库，运行依赖仅SDL3和Windows系统DLL。VisualStudio18 Community（MSVC14.51、SDK10.0.26100、CMake4.3.1）。Node在C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。build.ps1默认clean-first，曾确认工具保留旧时间戳导致增量编译漏新代码；需快速增量时显式-Incremental。

REA6.4.0、Ghidra12.1.4、jadx-headless-mcp0.7.1、JADX1.5.6位于agent/codex/tools；JDK为C:/java/jdk/21。原数据库agent/codex/ghidra-project/TowerMadness.gpr；Ghidra脚本仅使用独立ghidra-scripts目录，复用-process libverde.so -readOnly -noanalysis。并行headless仍可能争用项目锁，需顺序导出。REA Windows provider拒绝ARM ELF，直接Ghidra为已验证路线。Ghidra地址比ELF值多0x10000，文件映射需确认。JADX全导出7处错误不影响已恢复原生入口。

代理通常127.0.0.1:7897；Windows curl TLS失败可用D:/Git/mingw64/bin/curl.exe。不使用gh，不关闭本机其他服务，尤其MC服务器。任务指令由用户提供；新报告位于agent/report，TODO未经允许不改。

## 验收范围与未解决风险

本机RX9070XT已通过22项规则/保存检查、116模式原资金自然开局、116地图实际GL绘制、420PNG/321模型完整加载、36Ogg离线解码。PlainPlane普通50金币操作自然26波保住10羊，鼠标键盘重放胜利/重开/自然失败，15/144FPS调度一致。1280×900和1000×700拾取通过。发布ZIP解压到中文含空格目录、从C:/Windows/Temp及仅Windows PATH启动，菜单和保存继续通过。仍只测试一台Windows，非真正干净OS。

P7是可用发布候选，未达完整原作保真验收。没有真机，用户禁止Android/ARM模拟；本次不安装Android开发环境。原SO不直接运行；购买验证与广告排除。精确动画/俯仰平滑、阴影、skybox、粒子、羊被抓收尾和原手机UI尚未恢复；桌面持续2/4倍速不同于原临时2倍20秒。完整活动链/多诱饵堵路及同刻排序仍有差异。116关并未全部自然通关；长时高压无限局、跨机器、高DPI跨显示器和实际声音播放未验。

性能检查为完整自然局及其轻负载峰值场景的12000帧、关闭VSync且glFinish同步；不能据此保证所有大地图/多小时局的60FPS。无限已发波历史仍保留在状态数组，超长局内存覆盖不足。旧Android存档兼容、联网/排行/多人不在本次交付范围。
