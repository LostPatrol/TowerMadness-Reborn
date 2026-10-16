<!-- Isolated original-x86 camera input verification and minimal adaptation findings. -->
# 原 x86 摄像机输入补查

基线真实缺陷：当前 EXE `77D58C7C52F998FD3398DA026E0D88F63729EFB3F0D3274F32B5387475274EB1` 在 `play:Peach` 接收 Windows down(240,400)→move(270,400) 后，报 `UNIMPLEMENTED_OBJC_STRUCT_SELECTOR previousLocationInView:`，进程退出码 32。宿主缺少上一个触点坐标，单指拖动尚不可用。

原 GAME_touchesMoved 已包含单指平移、双指距离比例缩放和 GAME_makeSureCameraIsGood 限位。无需重写摄像机。隔离 copy 只补 UITouch 前坐标、两触点 NSSet 枚举和 WM_MOUSEWHEEL→双指 pinch；保留原 ELF/APK。

隔离验证：`agent/codex/x86-continue-20261016/camera/evidence/play-20261016-124845-340`，冻结 EXE `587B6C5306DFE3245478553020E50DD3F08EEE2F7C452E062E0F501F0068801F`。仅 SendMessage 输入、读取 CAMERA 矩阵和玩家状态、截图，未改金币/tech/生命。

- 80px 左右拖动：原矩阵平移 20→24.162807→20.000013；不再退出。
- 滚轮 +120/-120：原矩阵深度 -52.13995→-50.80099→-52.13995。
- 连续各 100 次近/远滚轮：相机保持有限并回到原远限位，native exit 0、GL errors 0；近限位截图符合原近距离羊群视角。
- 14 次截图，玩家 money=2500、lives=10、score=0 始终不变；未发波，故不扩大为战斗验收。

`host-camera.cpp` 含隔离验证仪表；`host-input-adapter.cpp` 已去掉仪表；`input-adapter.diff` 仅保留输入适配 hunks，主代理应将这些 hunks 合并到其最新主源码，不能整文件覆盖并行主线改动。

尚未验证：真实菜单暂停时滚轮禁用、教程期间操作、缩放后建塔点击、右键/触控屏、更多地图。未修改主源码或 MEMORY，由主代理合并后统一维护真实基线。
