<!-- Durable correction conclusions; obsolete investigation logs and reports were deliberately pruned. -->
# 基线修正摘要

任务03建立的修复基线为 `d5122b7`。当前事实与命令见 [X86_CORE_REUSE.md](X86_CORE_REUSE.md)，精简验收数据在 `agent/codex/x86-reuse/verification/baseline.json`。

- D2已定位：原TOWERS_fromIBS按104字节同步前缀分配，却写入需要112字节的运行字段。独立围栏检测与原指令一致；tower-restore.hpp修正该对象分配，原存档字节、恢复和校验逻辑保留。原指令证据已集中到analysis/restore.txt及analysis/menu。
- F9与脚本恢复在帧边界先释放旧GAME，再建立替代对象；OBS/IBS包装对象正常释放。菜单/终局渲染重新读取当前对象，避免使用旧指针。
- 原生退出码0不足以验收。当前构建冻结输入，运行绑定EXE/源码/脚本，并检查实际恢复、暂停冻结与恢复后的时钟前进；map空操作和未经测量的4倍速宣称已移除。
- 原报告中“所有测试都是空局”“重复地址就是共享/双重释放”“分配够大就完全排除索引越界”等概括或推断不能继续引用。
- 历史116图仅证明载入绘制；首关Victory后继续仍因NSLocale接口缺失退出。两者不代表完整游戏通过。

深度清理后，不再保存DSH/Claude会话、失败候选、旧调试日志、重复包或过时报告。必要结论已收敛到当前文档和verification摘要；需要追溯旧源码/已提交文档时使用Git历史，不再恢复旧自动任务。
