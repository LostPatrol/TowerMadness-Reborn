<!-- Frontend implementation handoff and explicitly pending integrated verification. -->
# P8.5 外围流程实现简报

执行代理：codex-frontend_flow。范围：既有 `src/main.cpp` 桌面菜单、输入、结算及既有验证入口，`src/profile.cpp` 保存提示，`docs/FRONTEND.md` 开发入口说明。

## 实现

- 选关列表只切换独立预览，保留当前防御局；新增明确的开始按钮，锁定关卡可浏览但不能开始。
- 预览展示实际科技、初始金币/羊数、有限波次数/无限和存档最佳成绩。接入主代理提供的真实地图离屏缩略图接口，不生成虚构图片。
- 保存继续卡显示实际关卡、波次和状态；继续先恢复副本再切换当前局。失败提示在菜单/暂停/结算中可见，默认静音不变。
- 真实胜利记录最佳成绩，首次完成才显示新解锁；下一战役按钮开始新的原规则关卡。附加关卡仍为桌面资源库，四无限/沙盒维持完成 DividedAttention 后开放。
- 新增必要生产输入检查：浏览保留当前局、明确开始、真实暂停存档卡及继续、门槛一致、900×700菜单；在既有自然胜利重放后验证下一战役和重复胜利不重复宣称新解锁。

## 验证与交接

已阅读当前 MEMORY、根 AGENTS、P8 计划和真实源码，核对 `docs/MODES.md` 的原无限门槛。`git diff --check` 已通过。按主代理分工未构建、未运行新增检查、未提交；因此本简报不宣称新增GUI运行验收通过，也不宣称原作保真或P8.5全部完成。

主代理须提供 `HudImage` / `SceneView.images`、`Renderer::mapPreview`，并在生产主循环接入 `desktopCompletion(game,desktop,profile)` 替换旧单纯 `Profile::complete` 逻辑。集成后运行既有 self/desktop/input 检查并审阅真实菜单与胜利解锁截图。

未修改 MEMORY、README、计划、TODO 或 agent/task；这些由主代理按最终验收结果维护。
