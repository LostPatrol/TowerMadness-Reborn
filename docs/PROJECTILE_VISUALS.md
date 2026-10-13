<!-- Static ARM projectile visual formulas and evidence for native rendering. -->
# Projectile visuals

This closes the missile, flak and trail formulas left open in [COMBAT_VISUALS.md](COMBAT_VISUALS.md). Evidence is the original ARM `libverde.so`; addresses below are Ghidra addresses (ELF virtual address + `0x10000`). These formulas are static recovery. Unmodified ARM gameplay is now available through [ANDROID_REFERENCE.md](ANDROID_REFERENCE.md), but these projectile effects have not completed dynamic equivalence checks. Use the original meshes, materials and textures.

Primary exports: `agent/codex/p8-native-effects/{effects,support,impact-params}/{instructions.tsv,pseudocode.c,strings.tsv}` and `literals.tsv`. Projectile sampling evidence: `agent/codex/p5-evidence/{instructions.tsv,pseudocode.c,strings.tsv,projectile-literals.json}`; creation defaults: `agent/codex/projectile-evidence`. ARM register arguments and ELF literals were checked where soft-float pseudocode omits arguments. Supplemental checked bytes are retained in `agent/codex/p8-projectile-visuals/checked-literals.json`.

## Common conventions

`age=now-born` uses realtime double seconds. `f(x)` means rounding to float32; suffix `f` means a float32 literal. Camera position is `game[+0xa00]+(0x108,0x10c,0x110)`. `d` is float32 Euclidean distance from the effect position to that camera.

Missile and flak point sizes below are framebuffer pixels sent to `PGL_addPointSprite`, ultimately `glPointSize`. Neither function reads `game[+0xbf8]` nor multiplies by screen width/320. Do not apply the plasma/flame screen multiplier to these effects. `PGL_addPointSprite 0028e6b4` stores the size unchanged and converts RGBA to bytes with unsigned truncation of `255f*component`; `PGL_flushPointSprites 0028e894` applies those bytes and the stored size directly.

`T`, `S`, `Rz` denote the original OpenGL matrix calls. Uniform scale commutes with yaw; preserve the original geoset transforms. `blast.model` uses `rect`, `smokepuff.model` and `debris.model` use `3plane`, and `rangecircle.model` uses the original `rangecircle` mesh. These are model geometry, not billboards. Point sprites below use materials directly. All named missile/flak materials use ordinary alpha blend and disable depth writes; draw colors replace material RGBA. Point sprite submission order is recorded below; the original renderer queues point sprites separately.

`GAME_randFloatExt` and `GAME_randIntMinMaxExt` are cosmetic `lrand48` calls. They are separate from combat random state. An independent deterministic visual hash is a desktop approximation, not an equivalent original sequence.

## Homing missile impact

`homingProjectileHit 002d3c24`, especially `002d3c70–002d3d58`, damages at the unmodified projectile XY, then creates exactly `EffectMissileExplosion` at:

```text
p = projectile.xyz + .25f * (Rx-.5f, Ry-.5f, Rz-.5f)
```

The three external visual samples are drawn in X, Y, Z order; each offset lies in `[-.125,.125)`. Impact Z is preserved. It passes `projectile.splash(+0xa0)` and the pulse flag at `+0x60`; it does not create a ground crater or ordinary ground explosion.

`EffectMissileExplosion_init 002c921c` samples, in order:

```text
j = f(.75f + f(.5f*randFloatExt()))
yaw = f(360f*randFloatExt())
variant = randIntMinMaxExt(0,3)
B = f(3f*splash)
expiry = born + 1.5 + double(f(3f*randFloatExt()))
```

Variant selects `flash`, `flash2`, or `flash3`; a nonzero pulse flag substitutes `blueflash`, `blueflash2`, or `blueflash3`. These are three separately loaded materials, not an animated atlas. The random factor `j` affects only the ring scale in draw, not every layer.

`EffectMissileExplosion_draw 002c93ec` uses **fixed** `u=age/1.5`, regardless of randomized expiry. Define:

```text
c = double(0x3fe3333340000000) = 0.6000000238418579
S = f(f(f(f(f(u+c)*.6f)+.4f)*B)*.3f)
H = f(f(.75f*j) * f((1-u)*double(f(.3f*B)) + u*double(B)))
F = f(.5 + 9*u*double(B))
```

The approximate real arithmetic is `S=.3*B*(.4+.6*(u+.6))` and `H=.75*j*B*(.3+.7*u)`. The unusual `u+.6` is present in the ARM instructions; it is not an easing curve or a missing subtraction.

| Submission order | Geometry/material | Transform or point size | White alpha | Condition |
| --- | --- | --- | --- | --- |
| 1 | blast model | `T(p)*S(2*S)*Rz(yaw)` | `f(.5-1.25*u)` | `u<=.4` |
| 2 | smokepuff model | `T(p)*S(S)*Rz(yaw)` | `f(1-2*u)` | `u<.5` |
| 3 | rangecircle model | `T(p)*S(H,H,1)` | `f(1-2*u)` | `u<.5` |
| 4 | debris model | `T(p)*S(S)*Rz(yaw)` | `f(1-2*u)` | `u<.5` |
| 5 | selected flash material point sprite | at `p`, `sizePx=f(f(200f*F)/d)` | `f(1-2.5*u)` | `u<=.4` |

All model translations use exactly `p`, with no extra ground or Z offset. The ring has no random yaw. Visible drawing ends at age `.75s`, although the record remains alive for `[1.5,4.5)` seconds. Do not stretch the visible animation across that randomized lifetime.

Key instruction ranges: alpha `002c943c–002c95ec`; sizes `002c95f0–002c9730`; model transforms `002c97e4–002c999c`; point sprite `002c99a0–002c9a48`. Constants are at `002c9a54–002c9a68`.

## Flak impact and early blast

`flakFireFunc 002d1ac0` consumes three combat random values at the beginning. Effect position uses the enemy XYZ read after damage plus `(R1-.5,R2-.5,R3-.25)`; there is no WAVES offset or Z clamp. The third sample already belongs to combat's consumption order and must be reused, not sampled again for rendering. Input radius is weapon splash without boost. See `002d214c–002d21a8` and the earlier damage-position reads.

`EffectFlakExplosion_init 002c9a6c` stores `s=f(f(.5f+randFloatExt())*splash)`, chooses `flakburst1/2` with one external integer sample, loads **blast material**, and expires at `born+2`.

`EffectFlakExplosion_draw 002c9b20` computes `u=age/2`, `alpha=f(1f-f(u*u*u))`:

| Period | Point sprite submissions |
| --- | --- |
| `u<double(.1f)` | selected flakburst at `p`, then blast at `(p.x,p.y,f(p.z+.05f))`; both white, same alpha, same `sizePx=f(2560*u*double(s)/double(d))` |
| Otherwise | selected flakburst at `p`, white alpha, `sizePx=f((256+128*u)*double(s)/double(d))` |

The early blast is **not** `blast.model`. Both early sprites use the distance measured to `p`, even though the blast is submitted at Z+.05. The later formula intentionally jumps from about `256*s/d` to `268.8*s/d` at the boundary. See `002c9c54–002c9e64`; literals `002c9e68/002c9e6c`.

## Trail geometry

`EffectTrail_init 002c9e70` receives `(effect,game,Ax,Ay,Az,Bx,By,Bz,widthParameter)`, stores both endpoints unchanged and sets expiry `born+.25`. `A` is the current projectile point; `B` is the last sampled point, not necessarily the preceding simulation position.

`EffectTrail_draw 002c9ed4` constructs this exact world-space quad:

```text
q = f(1-4*age)
w = min(f(f(widthParameter*q)*.02f), .2f)
vertices = [A, B, B+(w,w,w), A+(w,w,w)]
indices = [0,1,2, 0,2,3]
RGBA = (1,1,1,f(q*game[+0xa68]))
```

This is a positive diagonal XYZ offset, not a camera-facing ribbon, centered width, cross product, smoke texture, or chain of smoke sprites. Width shrinks with age. `002ca038–002ca060` explicitly enables blend, sets `GL_SRC_ALPHA,GL_ONE`, disables texture coordinates/textures and uses client-side vertices. It draws two triangles, then enables textures again. The draw routine does not explicitly change the depth-write mask; that depends on surrounding render state. The alpha multiplier `game[+0xa68]` is inverse playback speed; the suppression flag `game[+0xbe4]` is graphics mode, as detailed below.

The first four indices are loaded from Ghidra `00369018` (ELF/file offset `00359018`) and the last two are the immediate halfword `0x0302`. Width literals at `002ca0a0/002ca0a4` are `.02f/.2f`; vertex additions are explicit at `002c9f58–002c9fd8`.

## Trail creation cadence

`PROJECTILES_create 002ad78c` zeroes the next-sample threshold `+0x5c`. Homing and mortar fire routines initialize sample XYZ `+0x28/+0x2c/+0x30` to the actual launch point. Therefore the first positive movement update can emit immediately; do not wait for one full interval before the first segment.

`PROJECTILES_update 002ad9ec` checks the threshold **after movement**, using strict `progress>threshold`. It emits at most one segment per movement update, not a loop filling intervening samples. After a eligible sample, it stores the current position and sets `threshold=progress+interval`; it does not add interval to the old threshold.

| Update path | Progress | Interval | Width parameter |
| --- | --- | --- | --- |
| Tracking types 1,2,4,5 | traveled distance `+0x58` | `.2f` world units | `5f` if current type 2, otherwise `3f` |
| Ballistic nuclear ascent type 3 | elapsed seconds `+0x58` | `.2f` seconds | `5f` |
| Ordinary ballistic | elapsed seconds `+0x58` | `.1f` seconds | `2.5f` |

Tracking skips trail processing for weapon-name prefixes `Plasmatron` (10 bytes), `Gauss` (5), `Fusion` (6), `Flame` (5), `Energy` (5). This is the native weapon record name (`game[0] -> +0x1c -> +0x44 -> array[projectile.weaponId] -> +4`), not a material name. In the current Medium tech every tower name equals its configured weapon name, but implementations should use that configured weapon identifier. Energy Pulse Missile consequently has no trail. This includes keeping excluded weapons' sample threshold/endpoints untouched by that block. In eligible paths, `game[+0xbe4]!=0` suppresses allocation/drawing but still advances endpoints and threshold. Trail creation is not in `PROJECTILES_draw`, so render-frame sampling is incorrect.

Nuclear ascent-to-descent transition `002aea30–002aea40` changes only type 3 to type 2. It retains progress `+0x58` (elapsed seconds become initial traveled-distance value), next threshold `+0x5c`, and last sampled XYZ. Do not reset the visual sample state during that transition.

Hit paths skip trail creation. Tracking checks distance before movement: `<.35f`, or `<1f` for type2, branches to `002ade50`; ordinary hit callback/removal at `002ade6c–002adea4` bypasses movement and sampling. Type4 at its waypoint instead becomes type1 and reprocesses the same projectile; that transition itself creates no segment. Ballistics move first, then `z>=0` branches at `002adc8c` to the sampling path; `z<0` invokes hit/removal at `002adc90–002adcd0` and skips sampling even when its threshold is due. Do not append a separate final trail segment to an impact point.

Calls are `002ae0fc` (type3), `002ae7e0` (tracking), `002ae884` (ordinary ballistic). Tracking prefix tests are `002ae6b0–002ae744`; threshold update `002aeab4–002aeac8`. Ballistic threshold updates are `002ae8cc–002ae92c`. The projectile hits/removal and waypoint transition remain governed by the existing combat code; do not add visual movement or combat random consumption.

## Playback speed and graphics mode

Focused read-only exports are in `agent/codex/p8-projectile-visuals/game-flags`. `GAME_alloc 0029b8e0` initializes `+0xa68` to float1 (`0029b998–0029b9a4`). `GAME_togglePlaySpeed 0029f9a4` cycles that field `1 -> .5 -> .25 -> 1`, with the corresponding original strings `Normal Speed`, `2x Speed`, `4x Speed`. `GAME_update 0029e184` multiplies the fixed game-step milliseconds by this field to obtain the wall-clock step interval. It is therefore an inverse playback-speed factor, not a general scene-opacity parameter.

`GAME_updatePlaySpeed 0029e48c` reads the replay slider. For finite slider values `v>.1f`, it writes `1f/v`; otherwise it writes zero. The threshold is literal `0x3dcccccd` at `0029e4ec`. With zero, `GAME_update` holds the simulation clock and stops looping sounds. Since trail draw multiplies by the current field rather than a value captured at creation, normal/2x/4x playback yields `alpha=q`, `.5*q`, `.25*q`; replay pause gives zero. Width remains the same age-based formula. A desktop implementation that keeps `alpha=q` at accelerated speeds is an explicit approximation, although it matches the normal-speed baseline.

`GAME_setGraphicsMode 0029fad0` directly stores its mode argument at `game+0xbe4`. `GAME_alloc` zeros bytes `+0xbb4..+0xbe7` via the memset at `0029bcd0–0029bce4`, establishing graphics mode zero as the native allocation default. `GAME_render` forwards it to `MAP_render`; that function renders trees only for mode0 and omits doodads in mode3. Thus suppressing trails for any nonzero mode is part of the original reduced-graphics path. A native recreation that provides only normal graphics can keep mode0 and need not invent an extra graphics option. Saved user preferences may override the allocation default; this focused check did not trace the entire options UI.

## Remaining limits

Interactive Effect/Beam lifetimes now use independent monotonic realtime; offline checks default to deterministic game seconds. Projectile movement and trail sampling retain game time, while emitted trail records age in realtime. Point-sprite colors now use unsigned-byte truncation before billboard rendering. The original positive-Y camera basis and unlit material default are implemented; desktop fitting and screen-relative size remain adaptations.

Exact appearance still needs dynamic comparison: pixel calibration, material sorting, inherited trail depth state, original external random seed and consumption sequence, and pause/speed behavior. The independent visual hash does not reproduce the original cosmetic sequence. This specification does not claim dynamic equivalence. No combat rules need to change to implement these formulas.
