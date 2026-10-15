<!-- Active original-x86 architecture, commands, current evidence, and immediate continuation work. -->
# 原 x86 开发基线

主线源码在 `tools/x86-reuse/`。Win32宿主映射原ELF32，CPU直接执行GAME/MENU/PGL；适配Windows C ABI、兼容OpenGL、输入、单线程GS和最小平台对象。原APK/ELF不改。固定480×800、静音，不依赖Android或CPU模拟器。完整移植尚未完成。

## 构建与验证

```powershell
& .\tools\x86-reuse\build.ps1 -SkipGlSignatures
& .\tools\x86-reuse\run.ps1 -Mode menu
```

默认构建在 `agent/codex/x86-reuse/current`；`play.cmd`进入同一原菜单。鼠标操作原轮盘，空格发波，Esc返回，F5/F9保存和读取。

构建先冻结源码和塔修正头文件，默认clean-first，记录JSON头文件/CMake/EXE哈希。运行复制构建清单、输入和验收脚本，启动前后检查EXE。只有功能所需步骤真实发生才验收通过。

```powershell
& .\tools\x86-reuse\run.ps1 -Mode menuauto -OutputDirectory "$PWD/agent/codex/verify/evidence"
& .\tools\x86-reuse\run.ps1 -Mode menuauto -Map continue -OutputDirectory "$PWD/agent/codex/verify/evidence"
& .\tools\x86-reuse\run.ps1 -Mode menuauto -Map restorestress -OutputDirectory "$PWD/agent/codex/stress/evidence"
```

前两条顺序执行；continue读真实前次保存。restorestress要求13次真实F9恢复。缺存档、缺步骤或恢复后时钟不前进均失败。`scene -Map <gamemode>`检查实际地图载入/绘制，`levels`仅枚举资源，`upgrade`是原API建升售。`campaign`由原菜单/账户决定地图；continue/restorestress都是测试场景名，不是地图选择。历史map空操作已删除。

## 必需目录

| 路径 | 用途 |
| --- | --- |
| 根目录两份APK | 原始输入；身份和资源等价见[APK_BASELINES.md](APK_BASELINES.md) |
| `agent/codex/x86-reuse/package` | 唯一原x86解包，23库和原资产 |
| 同级GS/GL JSON、libverde-elf.json | 运行适配与原符号 |
| `assets`、`third_party` | 标准化资源、JSON与GL头文件；资源准备脚本共享上述package/assets |
| `agent/codex/x86-reuse/current` | 当前可执行文件、冻结输入和构建清单 |
| `agent/codex/x86-reuse/analysis`、`ghidra` | 原MENU/生命周期静态导出及x86数据库 |
| `tools/x86-reuse/ghidra` | 可复用的只读导出脚本 |
| `agent/codex/x86-reuse/verification` | 精简验收事实与历史Victory图片 |
| `agent/codex/tools` | 已注册REA/Ghidra/JADX运行时，保持路径不变 |

原APK、工具、数据库和运行输入未入Git；仅clone仓库不足以运行。旧过程副本、会话、下载缓存及历史报告已删除，不再查找agent/dsh、agent/claude或旧correction目录。旧重建与规则规格只在需要时查[reference](reference/README.md)。

`CorrectionProbe.java`导出恢复/分配函数，参数为输出目录、libverde-elf.json；`NativeMenuProbe.java`导出菜单与调用关系，参数为输出目录、符号JSON、0x10000。两者使用现有NativeX86数据库和本地Ghidra headless的`-readOnly -noanalysis`，输出到新目录。`inspect-elf.mjs`接收原库路径，向stdout输出符号JSON，替代已删除的过程目录版本。

## 核心修复与接口合同

D2原因是原TOWERS_fromIBS只为104字节同步前缀分配104字节，随后写+0x68/+0x6c；正常TOWER_alloc需要112字节。`tower-restore.hpp`只修正塔读取分配，保留原数据和校验。原指令在analysis/restore.txt及analysis/menu，禁止改成全局malloc扩容或跳过GAME_free。

恢复请求统一排队到帧边界：先释放旧GAME，再走原GAME_fromIBS安装替代对象。原IBS读者复制数据，OBS/IBS包装对象正常释放。同步菜单操作后重新取得GAME，终局不绘制已释放对象。

`MENU_setResolution`接收float，`GAME_setResolution`接收int。首关导航22×32，不能折半。`GAME_upgradeTower(game,instanceId,targetTechId)`；`TOWERS_getIdForCell`接收TOWERS*（GAME+0x10）。Ghidra地址=ELF VA+0x10000，ARM规格中的地址不能直接套用。

installHook改私有映射导出入口，之后findExport可能已指向宿主。objc汇编dispatcher可能为STT_NOTYPE/size0，不能过滤掉fpret/stret。未初始化完整Objective-C运行库，不盲调objc_getClass或所有构造器；未知导入/selector仍明确失败。

## 当前证据与下一步

修复基线d5122b7已经普通CRT验证：两次含塔活动局恢复/暂停/继续、新进程继续、13次F9及14次旧GAME释放、四图scene、原激光50→40→20→50。精简原始事实在verification/baseline.json；本次清理后的复验单列在verification目录。旧失败日志、重复截图和二进制不再保留。

历史PlainPlane原Victory为433075ms、8羊、1417分，随后preferredLanguages缺失退出。历史116图载入绘制已核对，但没有逐次源码/EXE绑定。这两项不能扩大为当前完整战役或116关通关。

接续优先：原胜利继续与进入第二关完整流程 → 四战役及解锁 → 全GUI与武器覆盖 → 实际倍速、跨地图/长时保存 → 独立包。SCORES日期纪元混用及robosteal_walk0..19缺失仍需处理。每一阶段由主代理对同一冻结候选亲自验收，避免把不同版本结果拼成进度。
