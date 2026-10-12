<!-- Current project baseline, durable analysis decisions, and unresolved risks. -->
# Project memory

## 当前基线

Windows x64原生C++20工程已建立。GitHub私有仓库为LostPatrol/TowerMadness-Reborn，本地main；不创建新分支或worktree。原APK不进入Git，SHA-256：0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b，保持原件不变。

运行入口release/TowerMadness.exe，同目录SDL3.dll与assets。支持完整PlainPlane26波、激光升级链、出售、飞行/分裂、胜负、暂停、1/2/4倍速、重开、相机控制；静音且不初始化音频。其他武器、关卡选择及存档未实现。

Android包 com.limbicsoftware.towermadnesszero；minSdk9、targetSdk17；只有armeabi。核心为libverde.so，32位ARM ELF，Apportable的Objective-C/iOS移植结构，不是Unity项目。核心库保留4324个动态函数符号，缺原始调试类型；Ghidra识别10390函数并完成12个玩法样本伪代码/指令导出。

tools/prepare-assets.mjs可重复生成202个统一TMMS网格、420标准PNG、36实际Ogg的CAF及全量配置；116gamemode显式引用闭合。2个广告SDK CgBI图不转换，6个全局未引用旧缺失边保留清单，不影响PlainPlane。原model变换行序，渲染转列序；模型原UV配PNG topdown，basetiles.atlas位置bottom-origin，仅地表atlas转换V。见docs/ASSETS.md、RENDERING.md。

PlainPlane地形11×16对应导航22×32，每tile2单位；terrain0 mud可走不可建、1 grass可走可建、2 forest阻挡。8邻接禁切角，塔占2×2且可奇数格放置；pads是UFO锚点，enemy从pad+(2,2)出生，entrance为羊圈终点。固定25ms，自动发波严格>20s。beam按float乘heat截断为整数后再乘护甲截断；heat增加/衰减，refund累计，售塔延迟退全额或75%，spawn1死后产生2子。证据索引见docs/RULES.md。

## 工具与已验证路径

REA6.4.0、Ghidra12.1.4、jadx-headless-mcp0.7.1、JADX1.5.6位于agent/codex/tools，用户config.toml已注册REA并保留.rea.backup。复用Node22.23.1与C:/java/jdk/21；证据按需定向读取，勿全扫描agent/codex。

原生基线为MSVC、CMake、SDL3 3.4.18、OpenGL3.3 core、WIC PNG、nlohmann/json3.12.0；用户已批准third_party局部依赖。tools/setup-dependencies.ps1固定依赖摘要，tools/build.ps1构建release；MSVC静态运行库。仓库含准备资源，解包目录不在时仍可构建。本机Visual Studio18 Community（MSVC14.51、SDK10.0.26100、内置CMake4.3.1）；Node在C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe。系统代理通常127.0.0.1:7897；Windows curl的TLS偶发失败时可用Git自带curl。不得关闭本机其他服务，尤其MC服务器。

PowerShell入口：agent/codex/rea.ps1、probe-native.ps1。MCP验证：verify-rea-mcp.mjs。证据与用法见agent/codex/README.md，数据库为agent/codex/ghidra-project/TowerMadness.gpr；原生证据位于native-evidence。

REA Windows Ghidra只接受PE，确实拒绝此ARM ELF；用直接Ghidra进行核心分析。Ghidra脚本目录必须为独立ghidra-scripts，避免把整套工具/解包Java源码纳入脚本编译。Ghidra本次地址比ELF符号值多0x10000，补丁必须确认文件映射。

## 可行性与未解决风险

当前目标为Windows x64原生重建，双击EXE即可游玩。用户禁止模拟器，不得使用虚拟Android或ARM指令模拟；原SO不直接运行。购买验证与广告排除。原型可玩不等于原作动态等价，不将静态伪代码视为可编译源码。

本次没有安卓真机可用，真机对照暂跳过。Android SDK/adb不是原生Windows构建前置。

本机RX9070XT上GL3.3 core、原场景、1280×900与1000×700拾取已验证，未跨显示器验证不同DPI。正常金币/建造/升级指令自然完成26波保住10羊；无防御自然失败。15/144FPS仿真一致；SDL鼠标/键盘事件经实际输入处理完成胜利、重开、失败。开发验证入口见docs/DEVELOPMENT.md。

原随机序列、精确相机/动画插值、阴影、地形过渡、skybox、粒子、炮口与羊被抓收尾未完全恢复；非PlainPlane护甲/regen不在验证范围。堵路检查活地面敌人可达，原作完整活动链与移动目标的检查细节未完全对齐。桌面持续2/4倍速为便利功能，原作临时2倍持续20s。P2为技术路线与场景验证，完整视觉保真未通过；P4原型闭环已验证，不代表P5–P7或全游戏重建。无限波次、全部武器、存档继续待恢复。JADX全量导出有7处错误。

详细基准报告：agent/report/report-codex-20261013-013339-REA配置与TowerMadness逆向评估.md。该历史报告中的模拟器候选已被用户后续要求排除。

总体路线仍见WINDOWS_REBUILD_PLAN.md，源代码修改入口见docs/DEVELOPMENT.md。原生技术选择已由本机原场景验证；新增范围先恢复规则再实现，不用beam替代其他武器。每次代码任务需新报告并维护本文件，测试/执行过程不堆入此处。
