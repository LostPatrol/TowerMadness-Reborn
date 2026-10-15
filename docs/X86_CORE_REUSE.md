<!-- Audited original-x86 development baseline, platform boundary, and reproduction contract. -->
# 原 x86 开发基线

当前主线只修改 `tools/x86-reuse/`。Win32 宿主映射原 ELF32，由 CPU 直接执行 GAME/MENU/PGL 机器码，适配 Windows C ABI、兼容 OpenGL、鼠标触摸、单线程 GS 和最小 Foundation 对象。原 APK/ELF 不修改，运行时没有 Android 或 CPU 模拟器。固定 480×800、静音。

这是继续开发的原型基线，尚非完整游戏交付。旧 `src/`、`release/` 为独立 x64 重建参考；其 12 武器、保存等验收不计入本路线。历史 DSH/Claude WIP 不再作为开发入口，审计结论见 [BASELINE_AUDIT.md](BASELINE_AUDIT.md)。

## 构建与运行

本机要求 MSVC Win32/CMake、`third_party/json.hpp`，以及以下未入 Git 的输入：

| 输入 | 位置 |
| --- | --- |
| 原解包与 23 个 x86 库 | `agent/codex/x86-reuse/package/` |
| GS/GL 元数据 | `agent/codex/x86-reuse/{gs-instructions,gl-signatures}.json` |
| 标准 PNG | `assets/textures/` |
| 原 x86 包 | 根目录 `Madness_Zero-1.22.apk` |

```powershell
& .\tools\x86-reuse\build.ps1 -SkipGlSignatures
& .\tools\x86-reuse\run.ps1 -Mode menu
```

默认构建在 `agent/codex/x86-reuse/current`；`play.cmd` 也进入此版本的原菜单。旧手测入口 `agent/codex/user-test/launch-current-x86.cmd` 指向同一个版本。源码仅在 `tools/x86-reuse` 修改。

构建默认 clean-first，先复制实际编译输入，再记录 `build-manifest.json` 中的源文件、头文件和 EXE 哈希。确有需要可加 `-Incremental`；副本分析用 `-HostSource`，不要覆盖产品源码。运行在启动前后检查 EXE 哈希，复制构建清单和输入到证据目录，并记录验收脚本哈希。旧二进制无清单时 `buildBound=false`，不可据此声称对应当前源码。

鼠标使用原菜单和建塔轮盘；空格发波，Esc 返回，F5 保存，F9 读取。F9 与脚本恢复共用帧边界入口，先释放旧 GAME 再创建替代对象。OBS/IBS 外层保持 IBS1 magic + 长度，原字节不改。F5 写 `menu-save-hotkey.bin`，测试继续写/读 `menu-save.bin`。开发状态位于输出目录对应的 `state`，与旧重建用户 profile 隔离；原型不能独立搬走 EXE。

## 验证入口

```powershell
& .\tools\x86-reuse\run.ps1 -Mode menuauto -OutputDirectory "$PWD/agent/codex/verify/evidence"
& .\tools\x86-reuse\run.ps1 -Mode menuauto -Map continue -OutputDirectory "$PWD/agent/codex/verify/evidence"
& .\tools\x86-reuse\run.ps1 -Mode menuauto -Map restorestress -OutputDirectory "$PWD/agent/codex/stress/evidence"
```

同一证据根顺序执行前两条；continue 必须使用真实前次保存，禁止复制热键档冒充测试档。menuauto 验收必须包含原教程完成、含塔活动局保存、F9 与脚本恢复、暂停冻结和恢复后时钟前进。restorestress 要求 13 次真实 F9 恢复（初次加 12 次重复）。缺步骤或无输入时失败，native0 本身不代表通过。

`scene -Map <原 gamemode 名称>` 检查实际载入/截图；`levels` 仅枚举资源。`upgrade` 经原 API 测建造升级出售，不等于 GUI 全链。`map` 这个历史空操作已删除，宿主拒绝未知操作。`campaign` 是长局探针，`-Map` 不决定菜单选中的战役，不能把参数当实际关卡。menuauto 的 continue/restorestress 也是测试场景名，不是地图名。

## 修改位置与关键合同

| 要修改的内容 | 入口或依据 |
| --- | --- |
| ELF 装载、GS、C ABI、原入口、输入、平台对象 | `host.cpp` |
| 塔恢复对象分配 | `tower-restore.hpp`；104 字节同步前缀必须放进 112 字节运行对象 |
| 构建输入与运行验收 | `build.ps1`、`run.ps1` |
| 原菜单、所有权、序列化证据 | `agent/dsh/menu-evidence/`；审计新导出在 `agent/codex/correction/ownership-export.txt` |
| 原 x86 分析数据库 | `agent/codex/x86-reuse/ghidra/NativeX86.gpr` |

`MENU_setResolution` 是 float，`GAME_setResolution` 是 int。导航坐标是 22×32 而非 11×16；`GAME_upgradeTower(game, instanceId, targetTechId)`；`TOWERS_getIdForCell` 接收 TOWERS*，位于 GAME+0x10。Ghidra 地址=ELF VA+0x10000，不能混用。

`installHook` 改私有映射的导出入口；之后 findExport 不保证得到原入口。objc 汇编 dispatcher 可为 STT_NOTYPE/size0，过滤不可跳过 fpret/stret。未运行完整 ObjC 构造器，不能盲调原 objc_getClass/sel_registerName。平台容器只是实际调用面的适配，未知导入/selector 仍明确失败。

## 已验证与尚未完成

任务 03 已重新构建并验证含塔恢复/回收、重复 F9、跨进程继续、四个代表地图绘制和激光建升售；具体 EXE、运行记录及边界见审计。此前首关自然 Victory 及 116 地图绘制仅作为已核对的历史证据，不能合并成当前版本全量验收。

仍待后续任务：胜利页继续链的 NSLocale 接口、四战役及解锁完整通关、所有 GUI 菜单/12 武器、真实倍速、长时无限/多地图恢复、高 DPI/跨机器，以及最终独立包。原 SCORES 的日期纪元混用与 robosteal_walk0..19 模型缺失仍保留为已知问题。购买验证、广告、联网排名不在范围内。不要以分配器放大全部对象、跳过释放或吞异常掩盖问题。
