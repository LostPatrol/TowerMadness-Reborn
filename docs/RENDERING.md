<!-- Native renderer design, asset interface, and remaining fidelity limits. -->
# Rendering

`src/renderer.cpp` owns one SDL3 OpenGL 3.3 core context. It uses shader programs, vertex array objects, and vertex buffers; it has no fixed-function fallback. The application owns the SDL window and input. Audio is never initialized by this renderer.

`Renderer::initialize(window, assetsRoot, mapFile)` loads the original map, material dictionaries, atlas terrain, and model descriptors. `SceneView` supplies simulation objects, world-space lines, selected cells, labels, buttons, and images. `draw` also presents the window. `capture` reads the presented framebuffer into a PNG for visual validation. `mapPreview` draws the same terrain, trees, and doodads to a 512×288 framebuffer, restores live map/camera/GL state, and caches at most 16 thumbnails.

## Resource interface

- `assets/meshes/<stem>.mesh`: little-endian `TMMS`, version 1, vertex count, index count; 20-byte XYZ/UV float vertices followed by 16-bit triangle indices. Original triangles are retained; flat normals are reconstructed because the source VBO has no normal stream.
- `assets/data/<stem>.model.json`: original geosets, VBO/material references, and row-major local matrices. Matrices are transposed into column-major storage, then composed with the instance transform.
- `assets/data/*.material.json`: original texture reference, UV offset/scale, RGBA tint, blend, and depth-mask settings. Alpha and the two additive blend modes are supported. Transparent parts are rendered after opaque parts, sorted by instance distance, without depth writes.
- `assets/textures/<stem>.pvr.png`: original PNG pixels decoded with Windows WIC. Original mesh UVs target top-down image rows and are retained. Atlas region positions instead use an image-bottom origin, so terrain V coordinates are converted when generating terrain vertices. The real atlas, UFO, barn, and crop circles verified this distinction in rendered screenshots.

Native `PGL_init` creates the built-in `default` material with `checkerboard` texture and UV scale 8 before packaged materials load. The renderer follows that order. Materials without a `tex` field use white texture samples multiplied by their original RGBA tint, matching the native untextured material path.

`PGL_loadTexture` at Ghidra 0028b3c8 defaults to NEAREST min/mag and REPEAT S/T, then applies descriptor min/mag/wraps/wrapt. The renderer retains the descriptor reference as its cache key, resolves its file alias, and applies these settings. Desktop PNG controls/text retain their own linear/clamped sampler. This distinction matters for the skybox's out-of-range UVs and moon texture scale 20. Focused pseudocode/assembly is in agent/codex/p8-render-evidence.

The packaged skybox mesh is drawn around the camera before terrain without depth writes, matching the camera translation observed in `GAME_drawSkybox` at 0029e51c. Maps can choose skybox_moon. The recovered camera basis is implemented; background appearance still requires dynamic comparison.

Terrain has no standalone packaged VBO. The renderer builds two-unit quads from the original terrain array and the map's `basetileAtlas` (default `basetiles`), using the native `MAP_alloc` mapping `0=mud`, `1=grass`, `2=forest`. Within each atlas region, world Y=0 uses `(atlasHeight-positionY-sizeY)/atlasHeight`; world Y=2 uses `(atlasHeight-positionY)/atlasHeight`, as recovered from `MAP_prepareRendering`.

`transitionAtlas` defaults to `transitions`. Higher terrain adds the original left/lefttop/lefttopright/surround regions onto lower terrain, with native UV rotations and separate opposing-edge quads. Diagonal corner regions are omitted when either adjoining edge covers them. The native renderer disables depth testing for these ground overlays; the desktop renderer lifts them by 0.002 units per terrain layer to keep depth testing for scene geometry.

Every forest tile receives one original grove model in column-major order. `treeModels` defaults to `grove1` through `grove5`; an explicit empty list disables trees. Native `sync_initRand` is MT19937 seeded with zero: each tree consumes one model-selection value followed by X/Y jitter values, translated to tile center plus `(float(random)/2^32-0.5)*0.47`. Original geoset transforms are retained. These rules come from `MAP_alloc`, `MAP_prepareTrees`, and `MAP_renderTrees`; focused static exports are in `agent/codex/rules-evidence/map-visuals*`.

Map doodad matrices are composed with model geoset matrices rather than replacing their scale.

## Coordinates and desktop input

World X/Y is the navigation grid; Z points upward. PlainPlane is 22×32 world units, corresponding to its 11×16 terrain array. Moving entities use navigation-cell centers (`x+0.5`, `y+0.5`); tower placement follows its two-cell footprint. The camera looks from positive Y, so negative world X is screen right. Eye is target+(0,5,height); height50 uses vertical FOV45°, and lower heights widen it by the recovered native formula. See [CAMERA.md](CAMERA.md).

SDL mouse and HUD coordinates use logical window pixels. The GL viewport and screenshot use `SDL_GetWindowSizeInPixels`. Both `pick` and `project` use the same camera and logical aspect ratio, so resizing or DPI scaling does not introduce a second coordinate conversion. `pan`, `zoom`, and `resetCamera` are exposed for application input.

Production desktop play reserves a board viewport between the HUD, palette and tower details. `resetCamera()` fits map corners and a treetop margin against that rectangle; `resetCamera(false)` selects the original centered height50 pose. Desktop fitting may extend above native height50. Developer scenes can retain the full-window default. Projection and picking must use the same reserved rectangle.

HUD labels are UTF-8 strings rasterized with Windows GDI into alpha textures at the current pixel density. This is a desktop control surface; the original mobile menu layout has not been reconstructed.

## Fidelity limits

Original scene materials now default to unlit texture×RGBA, following native PGL; the shader's optional lighting path is not the original default. Non-mine towers and non-flying enemies use recovered directionalshadow transforms and materials. Enemy armor auras, original status tint and frozen death geometry are supplied by main.cpp; animation formulas and limits are in ANIMATION.md. Renderable.animationTransform composes after instance yaw/scale and before packaged geoset transforms; overrideMaterialColor replaces RGBA, clamps it, and switches additive materials to ordinary alpha blend when override alpha is below one, as recovered from PGL_renderGeosets.

Combat effects now use original beam2/railspiral/2plane geometry, atlas rows, tier colors, recovered lifetimes, muzzle flashes, crater, ordinary explosion and four-layer nuclear geometry. Plasma and fireball use recovered point-size parameters converted to desktop billboards. Renderable.textureScale/textureOffset compose effect atlas/animated UVs with packaged UVs. Presented-frame visual hashes choose distinct lightning rows without consuming combat RNG; offscreen previews do not advance this counter. Full formulas and remaining differences are in COMBAT_VISUALS.md.

Homing impact now uses its original five layers and fixed 1.5-second draw divisor, independent of randomized record expiry. Flak includes the early blast point sprite. These two effects use fixed native pixel parameters, with no plasma/flame screen-width multiplier; the early flak sprite retains distance to the primary burst origin. Trails use exact untextured additive RenderQuad vertices, sampled in projectile updates, with alpha divided by current playback speed. Details are in PROJECTILE_VISUALS.md.

Interactive short-lived Effect/Beam records age and expire on independent monotonic realtime, including while paused; offline checks retain deterministic game-time fallback. Moving projectiles, enemy animation and tower construction remain on simulation time. Point-sprite RGBA is truncated to byte channels before billboard drawing. Independent hashes do not reproduce lrand48/drand48 sequences, and plasma/flame point-size mapping uses desktop board width pending native screen-field calibration. Inherited trail depth state, bait screen-space aura, conditional nuclear screen flash and sheep cleanup remain incomplete or approximate. Projectile pitch follows its complete velocity. Selected towers use the original selection texture at Z=.1/XYscale2 and rangecircle at Z=.15/actual range. Build previews use green/red selection highlights but retain a desktop range line; invalid500ms fade and the upgrade-range realtime fade remain open. Wounded health bars remain desktop readability aids.

Ordinary tower meshes face local negative Y: yaw is desktop heading+90 degrees, pitch is negative recovered elevation, and body height is 0.25 above the separate ground base. Articulated turrets pitch around rotationoffset. Game::towerElevation is shared with muzzle geometry; production input replay verifies the original laser mesh tip against the actual shot origin. Enemy bodies use original smoothed yaw directly without this tower offset.

Tower build/sale scale and height, old/new upgrade model halves, Tesla accessory rings and boost rangecircle are implemented; optional saved tower appearance preserves upgrade state. See [TOWER_VISUALS.md](TOWER_VISUALS.md). Full native aim smoothing with its combat gate, initial random yaw and idle drift remain open. Original geometry animation frames are chosen by Game::walkFrame and frozen corpse records. Transparent sorting is per instance rather than per triangle, and world sprites do not provide a full particle depth sort. Unmodified ARM gameplay is now available for comparison, but dynamic equivalence remains unverified.

Face culling is disabled until native winding and material-cull behavior have been visually verified. No original model is replaced by a cube or other placeholder. Missing model/material/PNG errors are logged and exposed through `Renderer::error`; remaining scene objects continue rendering.
