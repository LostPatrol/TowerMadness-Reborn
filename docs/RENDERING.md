<!-- Native renderer design, asset interface, and remaining fidelity limits. -->
# Rendering

`src/renderer.cpp` owns one SDL3 OpenGL 3.3 core context. It uses shader programs, vertex array objects, and vertex buffers; it has no fixed-function fallback. The application owns the SDL window and input. Audio is never initialized by this renderer.

`Renderer::initialize(window, assetsRoot, mapFile)` loads the original map, material dictionaries, atlas terrain, and model descriptors. `SceneView` supplies simulation objects, world-space lines, selected cells, labels, and buttons. `draw` also presents the window. `capture` reads the presented framebuffer into a PNG for visual validation.

## Resource interface

- `assets/meshes/<stem>.mesh`: little-endian `TMMS`, version 1, vertex count, index count; 20-byte XYZ/UV float vertices followed by 16-bit triangle indices. Original triangles are retained; flat normals are reconstructed because the source VBO has no normal stream.
- `assets/data/<stem>.model.json`: original geosets, VBO/material references, and row-major local matrices. Matrices are transposed into column-major storage, then composed with the instance transform.
- `assets/data/*.material.json`: original texture reference, UV offset/scale, RGBA tint, blend, and depth-mask settings. Alpha and the two additive blend modes are supported. Transparent parts are rendered after opaque parts, sorted by instance distance, without depth writes.
- `assets/textures/<stem>.pvr.png`: original PNG pixels decoded with Windows WIC. Original mesh UVs target top-down image rows and are retained. Atlas region positions instead use an image-bottom origin, so terrain V coordinates are converted when generating terrain vertices. The real atlas, UFO, barn, and crop circles verified this distinction in rendered screenshots.

Native `PGL_init` creates the built-in `default` material with `checkerboard` texture and UV scale 8 before packaged materials load. The renderer follows that order. Materials without a `tex` field use white texture samples multiplied by their original RGBA tint, matching the native untextured material path.

Terrain has no standalone packaged VBO. The renderer builds two-unit quads from the original terrain array and the map's `basetileAtlas` (default `basetiles`), using the native `MAP_alloc` mapping `0=mud`, `1=grass`, `2=forest`. Within each atlas region, world Y=0 uses `(atlasHeight-positionY-sizeY)/atlasHeight`; world Y=2 uses `(atlasHeight-positionY)/atlasHeight`, as recovered from `MAP_prepareRendering`.

`transitionAtlas` defaults to `transitions`. Higher terrain adds the original left/lefttop/lefttopright/surround regions onto lower terrain, with native UV rotations and separate opposing-edge quads. Diagonal corner regions are omitted when either adjoining edge covers them. The native renderer disables depth testing for these ground overlays; the desktop renderer lifts them by 0.002 units per terrain layer to keep depth testing for scene geometry.

Every forest tile receives one original grove model in column-major order. `treeModels` defaults to `grove1` through `grove5`; an explicit empty list disables trees. Native `sync_initRand` is MT19937 seeded with zero: each tree consumes one model-selection value followed by X/Y jitter values, translated to tile center plus `(float(random)/2^32-0.5)*0.47`. Original geoset transforms are retained. These rules come from `MAP_alloc`, `MAP_prepareTrees`, and `MAP_renderTrees`; focused static exports are in `agent/codex/rules-evidence/map-visuals*`.

Map doodad matrices are composed with model geoset matrices rather than replacing their scale.

## Coordinates and desktop input

World X/Y is the navigation grid; Z points upward. PlainPlane is 22×32 world units, corresponding to its 11×16 terrain array. Moving entities use navigation-cell centers (`x+0.5`, `y+0.5`); tower placement follows its two-cell footprint. The camera looks at ground Z=0 from a fixed tilted direction.

SDL mouse and HUD coordinates use logical window pixels. The GL viewport and screenshot use `SDL_GetWindowSizeInPixels`. Both `pick` and `project` use the same camera and logical aspect ratio, so resizing or DPI scaling does not introduce a second coordinate conversion. `pan`, `zoom`, and `resetCamera` are exposed for application input.

HUD labels are UTF-8 strings rasterized with Windows GDI into alpha textures at the current pixel density. This is a desktop control surface; the original mobile menu layout has not been reconstructed.

## Fidelity limits

The current shader adds simple ambient/directional lighting. It does not reconstruct the original shadow renderer, skybox, or particle lifecycle. Geoset local transforms are retained, but runtime articulated hierarchy has not been recovered. Original geometry animation frames can be rendered by supplying the chosen frame model name; frame selection and timing belong to the simulation/application. Transparent sorting is per instance rather than per triangle, so intersecting translucent geometry can still differ. Transition shape selection and tree placement have static native evidence; no Android device comparison has yet verified their appearance.

Face culling is disabled until native winding and material-cull behavior have been visually verified. No original model is replaced by a cube or other placeholder. Missing model/material/PNG errors are logged and exposed through `Renderer::error`; remaining scene objects continue rendering.
