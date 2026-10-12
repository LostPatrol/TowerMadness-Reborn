<!-- Resource preparation, runtime formats, evidence, and current coverage limits. -->
# 游戏资源

原始资源来自 TowerMadness Zero 1.22 APK。APK SHA-256 为 `0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b`。不要修改原 APK；资源准备只读取解包目录。

## 准备与文件布局

在项目根目录执行：

```powershell
node tools/prepare-assets.mjs
# 可选：指定已解包资源目录和输出目录
node tools/prepare-assets.mjs <source-assets> <output-assets>
```

默认输入 `agent/codex/apk/assets`，输出 `assets`。工具无需第三方 Node 包，不执行游戏代码，也不播放声音。

| 路径 | 用途 |
| --- | --- |
| `assets/manifest.json` | 原文件大小、SHA-256、转换结果、异常与关卡依赖 |
| `assets/data/<原文件名>.json` | 标准 JSON，保留原字段与数值 |
| `assets/meshes/<原VBO名>.mesh` | 规范化三角形网格 |
| `assets/textures/<原PNG名>` | 原 PNG 字节副本 |
| `assets/audio/<原CAF名>.ogg` | 原 Ogg 字节副本，仅更正扩展名 |
| `assets/samples/plain-plane.json` | PlainPlane 地图、26 波、Easy 科技和模型样本 |

运行时将整个 `assets` 目录放在 EXE 旁。生成物可重新准备，原 VBO 的证据保留在输入目录；清单给出逐文件哈希。工具按稳定顺序生成，不包含生成时间。它不会清空已有输出目录，原资源删除或改名后需要人工清理对应旧生成物。

## 网格与模型接口

`.mesh` 使用小端字节序：

| 偏移 | 类型 | 内容 |
| --- | --- | --- |
| 0 | ASCII 4 字节 | `TMMS` |
| 4 | uint32 | 版本 `1` |
| 8 | uint32 | 顶点数量 |
| 12 | uint32 | 索引数量 |
| 16 | float32 × 5 × 顶点数 | 每顶点 `x, y, z, u, v` |
| 顶点区之后 | uint16 × 索引数 | 三角形索引 |

没有法线或切线。原 UV 与顶点数值不变；`rect.vbo` 的 XY 顶点补 Z=0，并将 triangle strip 展开成 triangles。原 VBO 的 24 字节尾部含异常浮点值，不能用作可信包围盒；转换工具从有效顶点重新计算包围盒，并保留尾部十六进制。

模型 JSON 为 `geosets` 字典。每个 geoset 包含 `vbo`、`material`、`transformation`；模型变换保留原行序 4×4 数组，平移在索引 3、7、11。`vbo: "potatogun_a"` 对应 `meshes/potatogun_a.mesh`。多个 geoset 要全部渲染。`waves.walk` 和 `subwalk` 是独立模型名称序列，动画帧切换模型集合。

材质名称是 `.material.json` 容器内的键，不一定有同名材质文件。材质 `tex` 引用 `.texture.json`；例如 `type:"pvr", file:"grey"` 对应 `textures/grey.pvr.png`，实际是 PNG。`color` 是 RGBA；`blend`、`depthmask`、`cull`、`toffset`、`tscale` 和过滤参数均保留。UV 方向与透明排序需由实际渲染核验，准备工具不擅自翻转 UV。

## 最小关卡基线

PlainPlane 是唯一使用 `Easy.tech` 的 gamemode，初始金币 50、羊 10、26 个有限波次。它不依赖购买或广告流程。

地图 terrain 为行序 11×16 数组。原生 `MAP_load` / 寻路证据确认每个 terrain 格对应 2×2 导航格，世界导航范围 22×32；原模型 Z 轴为高度。terrain=1 可走可建，0 可走不可建，2 阻挡。`pads:[[6,2],[12,2]]` 是 UFO 出生位置，`entrance:[[8,27]]` 是羊圈入口/寻路终点，`enclosure` 是羊活动区域。不得把 pads 当建塔位置，也不得把这些坐标直接当作 11×16 terrain 索引。地表 atlas 包含 grass/forest/mud 等区域，具体地表代码与区域的对应尚未由此资源工具证明。

基础 beam 塔选 `Kilowatt Laser`：cost=10、buildtime=1000、reload=1000、range=6，lightdamage=0.5、heavydamage=1.5、bionicdamage=1.25，武器 damage=3.75；模型 `potatogun_i` 使用 `potatogun_a.mesh` 且 geoset 自带 2 倍缩放。muzzleoffset 原值 `[0,1.15,0.258]` 保留，其轴转换应遵循原生规则，不能凭模型轴方向推定。

首波普通 grey：health=10、count=10、gold=2、frequency=1200、speed=1、size=0.5、offset=0.125，非飞行、armor=0。四个 walk 模型为 `grey_anim_walk0..3`。grey 原网格有 300 顶点、234 三角形，Z 范围 0..1.5369。时间、伤害、缩放及命中规则由玩法模块解释，这里不修改数值。

## 验证与范围

202/202 VBO 的载荷长度、属性布局、有限顶点、索引范围均通过；201 个 XYZUV/u16 三角形，1 个 XYUV/u8 strip。321 model、122 material、148 atlas、118 map、116 gamemode、89 waves、4 tech、2 sound 均可转换成标准 JSON。工具只修复尾逗号与原 `font.atlas` 缺少的数组闭合符，并逐文件记录修复。

420/422 PNG 的签名、chunk 边界、CRC、IDAT 解压通过。`vg_button_mute_plain.png`、`vg_button_mute_selected.png` 是广告 SDK 使用的 Apple CgBI 非标准 PNG，明确列为异常，标准资源转换不支持其 raw-deflate/BGRA 像素。36/36 `.caf` 实为 Ogg Vorbis，33 个44100Hz、3 个22050Hz；原生音频模块已离线解码并验证PCM，未打开播放设备。全部资源加载的运行入口为`--resource-test`，实际声音／手机画面对照仍未验证。

116/116 gamemode 的显式 JSON 引用，加已知通用地表、羊、UFO、beam、skybox、塔座资源，均依赖闭合。清单按被引用的材质/音效键遍历，避免把容器内未使用条目混入某关卡。静态闭合不等于全部原生动态加载路径已恢复。

静态清单保留6个包内缺失边；其中cube的`default`已确认是`PGL_init`内置`checkerboard`材质、UV缩放8，运行加载器按该原始定义实现。另外5个未引用旧边为`moon_skybox.texture`、`blaster.texture`、`tomato.texture`、`walnutgun.texture`、`mapmenumodes.texture`，不伪造缺失文件。没有`tex`的材质按`PGL_setMaterial`作为纯色；纹理引用须继续读取`texture.file`，例如woodmapboxshadow实际使用uialphaelements。证据见`agent/codex/p7-material-evidence`及`p7-material-parser`。

单独的 `Grape.pvr`、界面 XIB 和 HTML 等仅列入原文件清单，不转换、不作为原生单人运行入口。
