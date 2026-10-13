<!-- Bounded original render-state audit and its explicit coverage limits. -->
# 原版光照定向核对

复用已有 PGL_init 证据及只读 Ghidra 数据库，定向检查 PGL_drawVBO、setMaterial、renderGeosets、invalidate、flushStates、GL enable/disable 调用点及 float light/normal 入口引用。完整说明位于 `agent/codex/p8-camera/lighting-findings.md`，原始反编译、汇编和交叉引用保存在同目录的 lighting* 文件中。

结论：原版游戏 PGL 仅提交 XYZ、UV、可选颜色，没有法线；材质直接使用 glColor4f，未配置光源。枚举的全部 glEnable 直接调用仅涉及混合、背面剔除、深度、纹理和点精灵，没有 GL_LIGHTING(0xb50) 或 LIGHTn。glDisable 路径也没有显式关闭 LIGHTING，因此原版依赖初始关闭状态。桌面按重建平面法线添加的 .65 环境光+.35 方向光缺乏原版依据，应删除，保留贴图×材质/覆盖 RGBA。

范围限制：这是原游戏直接调用路径的静态核对，不是运行时 GL 状态读取，也不声称穷尽所有框架间接入口。没有对设备操作，没有修改源码、已有文档、MEMORY、APK 或数据库。结果已发主代理落实。
