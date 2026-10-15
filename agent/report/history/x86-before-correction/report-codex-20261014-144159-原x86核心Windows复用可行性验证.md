<!-- Actual native original-code feasibility results, reproducibility, and open limits. -->
# 原 x86 核心 Windows 复用可行性验证

执行者：codex；日期：2026-10-14，Asia/Shanghai。保留基线：main/897bc63，原 C++ 重建、发行目录和用户存档未替换。独立分析由 native_entry 与 x86_platform_audit 协助，最终宿主与运行验证由 codex 完成。

## 结论

本次核心指标已实现：原 x86 游戏机器码在 Windows 原生 Win32 进程中绘制真实 PlainPlane；Windows 鼠标进入原选塔轮盘并建塔；原右下按钮发波；原战斗推进并产生击杀、金币及分数。运行不需要 Android、CPU 模拟器或虚拟机，也没有使用现有重建的 Game/Renderer 生成这些结果。

据此，原核心复用值得优先推进。此前“全面人工恢复是唯一实用路线”的判断不足，已有 x86 包和原 C 核心入口使更直接的复用成立。当前仍是首关可行性原型，不能立即据此宣称完整移植或替换发行主线。

## 实际运行证据

| 验证 | 操作与结果 | 原进程退出码 |
| --- | --- | --- |
| scene | 原 PGL/WAVES/GAMEMODE/GAME 初始化，原 GAME_render 绘制关卡/HUD/羊 | 0 |
| input | Windows鼠标消息点击战场、原类别轮盘、原塔轮盘，建成Mortar I；原右下按钮发波，运行35秒 | 0 |
| combat | 原GAME_buildTower命令建立两座Kilowatt Laser，原发波入口启动战斗，运行35秒 | 0 |

input：原初始50金币/0分/10羊；轮盘建塔后40金币；最终42金币/2分/10羊，gameStarted=true，仿真34950ms。没有直接调用建塔/发波API代替该最终input按钮路径。

combat：原初始50金币/0分/10羊；两塔扣款后30金币；最终46金币/16分/10羊，gameStarted=true，仿真34950ms。该项通过原命令API验证原激光战斗，不能描述为鼠标操作。

统计通过原PLAYER_getMoney/getScore/getLife读取；原GAME_update驱动仿真。每500ms记录状态，PNG从原渲染的GL framebuffer读取。初始化截图的HUD随原500ms时间淡入，原轮盘及选中时的镜头变化也由原代码产生。

固定证据目录：`agent/codex/x86-reuse/verified-20261014-144159/`。包括3组result、真实退出码run.json、native日志、开局/原类别与塔轮盘/建塔/战斗PNG及宿主摘要。运行日志分别为input-20261014-143746、scene-20261014-144116、combat-20261014-144118。此目录保留本次证据，后续普通运行不会覆盖。

## 保留与适配内容

保留原资源JSON、地图/科技/波次解析、模型/VBO、JSON-C、MT随机、游戏对象、战斗与动画、PGL绘制和原触摸/轮盘逻辑。完整DLL导入表只含Windows OPENGL32/USER32/GDI32/ole32/KERNEL32；Capstone、Ghidra和Java仅在准备/分析阶段使用。

宿主映射23个包内ELF，处理五类实际出现的重定位。平台入口只在私有内存映射中重定向；原APK及解包库不写回。源码入口`tools/x86-reuse/host.cpp`，复现及设计见`docs/X86_CORE_REUSE.md`。

需要适配的真实差异：

- Bionic的ctype指针对象/257项表、Win32 C调用和锁布局。
- 3117条经Capstone解码的绝对GS引用转向实验主线程存储；两条寄存器索引候选未盲目修改。没有构成全线程TLS支持。
- 原GL ES 1固定管线改接桌面兼容OpenGL；cdecl/stdcall堆栈桥、float投影入口和Windows PNG上传。
- 原VFS以Windows文件读取保留原返回结构与堆所有权；使用已有标准化PNG处理CgBI等平台图片格式，其他配置/模型直接读取原包。
- 最小NSSet/UITouch/view对象，只实现实际需要的六个选择器；x86结构体返回保留ret4。原EAGLView外层按钮函数也执行原机器码，未重写按钮命中逻辑。
- 单调时钟/Unix timeval、POSIX rand48、默认文本及明确静音。未知函数调用会记录并停止，未知ObjC消息不会泛化返回成功。

初期native故障和缺失接口均保留在过程日志；最终通过结果不依赖忽略错误。Ghidra首次2GB分析发生Java heap space，独立8GB重试成功；部分objc_msgSend误标noreturn导致截断，定向修正分析内存的元数据后导出完整触摸逻辑，原数据库修改未保存。

## 未完成与真实阻碍

1. 没有启动完整Apportable/Objective-C应用和全部库构造器；只沿独立C游戏核心启动。完整Foundation、Java/JNI、外层生命周期与原主菜单兼容仍未证明。
2. MENU/关卡选择/设置/保存尚未移植。MENU按钮给出未接入说明；不将临时外层对象描述为完整菜单。原型固定480×800、静音、不保存，现有用户profile不读取/写入。
3. 原选塔建塔和发波已实际验证；升级/出售、倍速按钮、拖动、全部武器/关卡、完整胜负及密集长局尚未验。原按钮倍速入口存在不等于已验收。
4. 单线程GS/锁、部分C ABI及未用导入还需扩展；完整框架执行可能需要真实线程/TLS、Bionic stdio、其他平台对象和调用约定。
5. HUD密度与字体尺寸、透明状态、驱动/宿主数学库与跨ARM/x86差异尚未逐项对照；不声称逐像素或逐位等价。截图编码最初RGBA/BGRA错误已在捕获器纠正，不修改原游戏画面来伪装一致。
6. 原加载器报告robosteal_walk0..19模型缺失，相关回退和敌人表现未验。购买验证/广告不纳入实验。
7. 仅本机Windows/RX9070XT运行，原生兼容OpenGL4.6；跨机器、干净OS、DPI和长时稳定未验。本次短局不是原作全关通关验收。

## 下一步建议

保留当前重建发行版，优先推进原核心宿主：补齐原启动/菜单与保存、明确平台接口合同，再验证多类武器、完整首关胜负、倍速/暂停及长局。达到这些门槛后再决定替换主路线。现有资产转换、规则证据和重建程序仍可用于对照。

本次新增独立实验源码/脚本及说明；不修改产品src/CMake/release，不新建分支/worktree，不改TODO。双击`tools/x86-reuse/play.cmd`可运行可交互原型；原APK SHA256仍为7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd。
