<!-- Recovered original camera, direct matrix evidence, and desktop adaptation boundaries. -->
# Camera

The original gameplay camera is a near-overhead perspective view from positive world Y. World negative X points right on screen; world positive Y points down. PlainPlane's crop circles at Y=4 are above its barn at Y=28.2. Do not flip terrain or model coordinates to change this view: rendering, picking, projection and panning must share the same camera basis.

## Native default

All addresses below are Ghidra addresses in the preserved ARM `libverde.so` database (ELF values are 0x10000 lower). Focused read-only exports are under `agent/codex/p8-camera/{initial,projection,resolution}`; `literals.tsv` retains referenced literal bytes. Pseudocode has imperfect ARM soft-float prototypes, so the eye formula was checked against instructions at 0029b6e8–0029b740.

`GAME_alloc` (0029b8e0) sets target `(integer(W/2), integer(H/2), 0)`, eye height 50, lateral offset -5 (`game+0xa6c`) and yaw 0 (`game+0xa9c`). W/H are navigation/world dimensions, twice the map's terrain dimensions. `GAME_makeSureCameraIsGood` (0029b554) enforces:

```text
h = clamp(camera.position.z, 0.8, 50)
theta = clamp(game.yaw, -pi/2, +pi/2)
target = (clamp(target.x, 0, W), clamp(target.y, 0, H), 0)
eye = (target.x - 5*sin(theta), target.y + 5*cos(theta), h)
verticalFovDegrees = 45 + 25*(50-h)/49.2
up = (0, 0, 1)
```

The -5 offset is fixed, not proportional to height. The yaw gradually returns to zero outside its active interaction. These facts describe the ordinary gameplay camera; tutorial/free-camera state can bypass the constraint. Native pan/pinch gesture reconstruction is outside this entry's scope.

`CAMERA_alloc` (00290344) supplies near=1, far=100 and zero roll. `buildProjMatrix` (00291b18) uses the ordinary OpenGL perspective matrix: `f=1/tan(fov*pi/360)`, diagonal X=`f/aspect`, Y=`f`, Z=`(near+far)/(near-far)`, Z translation=`2*near*far/(near-far)`, W from `-viewZ`. `GAME_setResolution` (0029fad8) passes the viewport dimensions to `CAMERA_setWindowSize` (00290538), whose aspect is width/height. `buildViewMatrix` (00290674) constructs lookAt with `right=normalize(cross(forward,up))`. Device orientation changes roll and screen coordinate handling through `GAME_setOrientation` (0029fb40), `GAME_updateCameraRoll` (0029e058), and `GAME_orientedWorldToScreen2` (0029fce8).

PlainPlane is W=22, H=32. Its default eye is `(11,21,50)`, target `(11,16,0)`, eye-to-target distance `sqrt(2525)=50.249378`; elevation above the ground is 84.289407 degrees. At portrait 480×800, aspect=0.6, the following matrices are written as mathematical rows multiplying column vectors, rounded to nine decimal places:

```text
V = [-1, 0,           0,           11          ]
    [ 0,-0.995037190, 0.099503719, 15.920595043]
    [ 0, 0.099503719, 0.995037190,-51.841437610]
    [ 0, 0,           0,            1          ]

P = [4.023689271, 0,           0,            0          ]
    [0,           2.414213562, 0,            0          ]
    [0,           0,          -1.020202020, -2.020202020]
    [0,           0,          -1,            0          ]
```

## Captured comparison

The unmodified ARM game's 480×800 `agent/codex/android-reference/runtime/logs/TM_Original_API17_ARM_clean/plainplane-tutorial.png` establishes the original rendered orientation. The static matrices above predict these screen positions, measured from the upper-left; they are calculations, not instrumented runtime camera reads:

| Ground landmark | World X/Y | Predicted screen X/Y |
| --- | --- | --- |
| Top-right map corner | 0, 0 | 444.90, 103.44 |
| Top-left map corner | 22, 0 | 35.10, 103.44 |
| Bottom-right map corner | 0, 32 | 458.31, 715.97 |
| Bottom-left map corner | 22, 32 | 21.69, 715.97 |
| First crop circle origin | 8, 4 | 296.32, 175.86 |
| Second crop circle origin | 14, 4 | 183.68, 175.86 |
| Barn ground origin | 13.5, 28.2 | 190.77, 639.07 |

Visible ground edges in the captured image agree with approximately Y=103 and Y=716 and the slight near-edge widening. Tree geometry extends outside the ground rectangle, and the tutorial overlay covers landmarks. This establishes orientation and default framing agreement; it is not an automated pixel-equivalence result. `agent/codex/p8-camera/check-default-camera.mjs` reproduces the numbers.

## Menus and Windows adaptation

The main menu uses a separate `Menu` map and low camera. `MENUWORLD_alloc` (0030cfbc) sets eye `(0.7W,H+2,2)`, target `(W/1.5,H/2,1)`, FOV=60 and default up/clip. For the 12×18 Menu world these are `(8.4,20,2)` and `(8,9,1)`. The captured `menu.png` in the same evidence directory shows the expected low barn/sheep view. This is not the gameplay default.

`MAPMENU_render` (002f9318) uses an animated perspective camera with near=10/far=200 and default FOV=45. Its map-selection pose is on the positive-Y side; transition completion interpolates to the exact gameplay pose `(W/2,H/2+5,50)` looking at `(W/2,H/2,0)`. The original rotating/sliding map-selection animation is distinct from desktop cached thumbnails.

Native default creation uses fixed height 50; it does not fit arbitrary maps or aspect ratios. Windows reserves a landscape board rectangle, so fitting map corners and treetops is a desktop adaptation. Preserve positive-Y viewing and negative-X screen right, use the same view/projection for drawing and picking, and choose the fit height against the actual board rectangle. Extending above native height 50 at FOV45 is a reasonable desktop adaptation; do not call it native zoom behavior. Keeping the lateral offset at 5 preserves the recovered native relationship; a constant normalized tilt preserves only the initial angle. Renderer entry points are `Impl::eye`, `Impl::viewProjection`, `Renderer::resetCamera`, `pick`, `project`, `pan` and `mapPreview` in `src/renderer.cpp`.

Windows uses this pose/FOV relationship for rendering, picking and billboard sizes. `resetCamera(false)` selects native height 50; ordinary reset retains desktop viewport fitting. `--smoke-test` creates an actual 480×800 drawable, captures the production world with desktop controls omitted, and compares four projected corners with the independent values above (observed maximum error .006 logical pixels). This verifies matrix implementation and visual orientation, not automated pixel equality with Android. Ordinary 1280×900 and 1000×700 picking roundtrips remain covered.

A direct RGB comparison against `plainplane-initial.png` uses two fixed unobscured static regions: upper terrain `(60,120,360,180)` and lower grass `(80,500,320,40)`. Mean absolute channel differences are 1.7415 and .8231 on a 0–255 scale; exact RGB pixel fractions are .1744 and .1382. These samples support close static scene color/framing, while expressly showing non-identical pixels. They exclude mobile HUD/tutorial/moving sheep and cannot establish full scene or animation fidelity. Reproduction and measurements are under `agent/codex/p8-presentation-verified/compare-static-regions.ps1` and `static-region-comparison.json`.

Original PGL uses XYZ/UV vertices and texture multiplied by RGBA. The bounded direct enable-call audit found no fixed-function lighting initialization; `PGL_init`, material and geoset rendering supply no lights or normal arrays. Focused exports and findings are under `agent/codex/p8-camera/lighting*`. Original model instances therefore default to unlit in Windows; the former added ambient/directional shading is removed. This static audit does not claim every possible indirect runtime call was traced.
