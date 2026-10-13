<!-- Static ARM tower draw, animation state, model identity, and remaining fidelity limits. -->
# Tower visuals

This specification is static evidence from the original ARM `libverde.so`, exported read-only from the preserved Ghidra database. Addresses below are Ghidra addresses (ELF values + `0x10000`). Evidence is under `agent/codex/p8-tower-visuals`: root = draw/update/sell; `state` = smoothing/models/boost update; `setup` = allocation/build/upgrade/type parsing; `special` = boost/bait; `selection*` = selection rendering/state. Each function export has pseudocode and addressed ARM instructions; `*-literals.tsv` resolves PC-relative names and constants. Original ARM menu/PlainPlane/selection/early combat captures now exist; see [ANDROID_REFERENCE.md](ANDROID_REFERENCE.md). These exports remain static evidence, not proof of dynamic tower equivalence.

## State and aiming

`TOWERS_smoothTurn 002b4928` owns distinct float-degree fields: tower `+0x18` visual yaw, `+0x1c` target yaw, `+0x20` visual elevation, `+0x24` target elevation. `+0x34` is last smoothing time; `game+0x28` is integer simulation milliseconds, not realtime.

With float32 operations, let `dt=float(now-last)/1000`, then store `last=now`. Native logic is:

```text
d = targetYaw - visualYaw
if abs(d) > .01:
    if abs(d) > 180: d += 360
    visualYaw += dt * (12.5 * d)
    if visualYaw > 360: visualYaw -= 360
e = targetElevation - visualElevation
visualElevation = visualElevation + dt*(12.5*e) if abs(e)>.01 else targetElevation
return d
```

The unconditional `+360` when `abs(d)>180` is present at `002b49d4–002b49e4`; do not replace it with symmetric shortest-arc normalization and call that exact recovery. Yaw below epsilon does not snap, and there is no interpolation clamp or negative wrap. Constant `.01` is literal `002b4ae4`.

`TOWERS_update 002b71f4` calls smoothing before search/aim, then again after the aim callback at the same time (second `dt=0`); the returned angular difference gates firing at `abs(d)<5`. Thus visual state participates in original firing timing. During build/sell it still smooths. Before game start (`game+0x18==0`), update first assigns `last=now`. Idle towers with no target and >2000ms since activity add one degree to target yaw each update (single subtraction of 360 if >360); `isbait` and `ismine` skip this drift. `TOWER_alloc 002dca14` consumes `GAME_randIntMinMax(0,359)`, initializes both yaw fields to that integer, and both elevation fields to zero. Replacing the random stream or caller ordering is an explicit adaptation.

`lightningAimFunc 002d5e04`, `boostAimFunc 002d1360`, `mineAimFunc 002d73e0` and `baitAimFunc 002cf668` are each exactly one `bx lr` instruction: they write neither target heading nor elevation. Their initialized elevation remains zero, including ElectroShock, Doom Capacitor and Tesla family upgrades (`tryUpgrade` resets target elevation to zero). Do not infer pitch from enemy height for these kinds. Conversely `slowAimFunc 002db95c` calls `WEAPON_directAimWithZ`, so ElectroSlow/Muscle Melter do aim geometrically. Evidence: `aim/{pseudocode.c,instructions.tsv}`.

`lightningFireFunc 002d5e08` calls `TOWERS_muzzleOffsetTransform 002b4ae8`, which reads **visual** yaw/elevation (`+0x18/+0x20`). At zero elevation the lightning family's `[0,0,z]` muzzleoffset puts its origin at `(x+1,y+1,.25+z)` independent of yaw: ElectroShock/Doom use Z=1.467 and Tesla uses Z=1.76. Fire separately calculates a beam from that origin to the randomized target endpoint; it does not rotate the tower toward the target. A desktop muzzle displaced sideways by geometrically pitching these towers is unsupported by native evidence. The helper's use of visual angle also matters for other tower families.

## Construction, upgrade and sale

`TOWERS_draw 002b5c34`, principally `002b5e70–002b601c`, uses no color fade. Let `p=float(now-start)/float(duration)`. Body transform is `T(x+1,y+1,s-.75)*S(s,s,s)` before orientation and packaged model transforms.

| State | Scale `s` | Body/turret model |
|---|---:|---|
| Normal | 1 | current type |
| New build (`previousType==-1`) | p | current type |
| Upgrade, p<.5 | 1-2p | previous type |
| Upgrade, p>=.5 | 2p-1 | current type |
| Sale | 1-p | current type |

Tower offsets: building `+0x40`, selling `+0x44`, build start/duration `+0x48/+0x4c`, sell start/duration `+0x50/+0x54`, previous type `+0x58`. `TOWERS_tryBuild 002b6754` and `tryUpgrade 002b6b38` use 500ms before first wave, otherwise type `buildtime`; sale uses 500ms before first wave, otherwise 3000ms. Update completes only after elapsed > duration. Zero-duration upgrades exist in assets: draw has an unguarded division, so a safe desktop zero-duration endpoint is an adaptation, not a recovered curve.

The independently rendered `turretbase` is omitted for `isbait` or `ismine`. It stays at `(x+1,y+1,0)` during upgrades and normal operation, moves to Z=`p-1` for new construction, or Z=`-p` during sale. It is not uniformly scaled. There is no separate base yaw.

Body yaw is visual yaw except exact names `Nuke`, `H Bomb`, `Doom Capacitor`, prefix checks `strncmp(name,"ElectroShock",12)` and `strncmp(name,"ElectroBoost",12)`, or nonzero `istesla`. Those skip yaw. If there is no articulated turret, body rotates around negative X by visual elevation, except exact `Nuke` and `H Bomb`. With a turret, body stays level and turret uses `T(rotationoffset)*Rx(-visualElevation)*T(-rotationoffset)`. During the first upgrade half only body/turret model handles switch to the old type; orientation conditions and rotationoffset still come from the new type. Native yaw already matches the model's local negative-Y convention; converting desktop combat headings requires the existing +90-degree mapping.

## Contact shadow and boost feedback

`TOWERS_alloc 002b477c` loads **directionalshadow**, not shadowblob, for manager `+0x10`. Literal resolution is `model-literals.tsv`. All non-mine towers, including bait, render it unless `game+0xbe4 != 0`:

```text
T(x+1.18,y+1.18,.01) * S(2.2,2.2,1) * Rz(-90) * S(k,k,k)
k = 1-p during sale, p during new construction, otherwise 1
```

Use the original model/material unchanged; the packaged model adds Z=.001. This pass has no native opacity override. Its literals are at `002b6730–002b673c`. Graphics setting field meaning beyond the observed zero/nonzero branch was not re-proven here.

The last tower draw pass is a **rangecircle** model for boosted towers (`tower+0x5c != 1`) that are not selling:

```text
T(x+1,y+1,.02) * S(1,1,boost) * Rz(-90)
RGBA = (64/255,150/255,238/255,1)
```

Keep the unusual Z-only boost scale as observed. This is not a general tower-body tint. `EffectBoost_init/draw 002c8b3c/002c8b94` also loads **rangecircle**. `boostFireFunc 002d1364` creates it at the boost tower center with `range`; the global boost update runs when more than 3000 simulation milliseconds passed. Birth/expiry and drawing use `get_current_realtime()` seconds. With double `u=realAge/.875`:

```text
alpha = 0 if u<.25 else 2*float(1-u)
M = T(centerX,centerY,.2) * S(float(u*range),float(u*range),1)
RGB = (64/255,150/255,238/255)
```

It renders only for alpha>0 and expires at .875s; material renderer clamps alpha above one. The asset `boostshimmer.model.json` exists, but neither recovered boost path references it. Calling a boostshimmer mesh or an approximate tint restored would be unsupported.

## Special towers

Only `istesla` (`type+0x98`) enables the two accessory models in this draw function. In the shipped Easy tech this is Tesla Coil with `electroshock_d_ring1` and `electroshock_d_ring2`. Relative to the body transform, ring one is `Tz(.25*sinf(.5*float(nowMs)))*Rz(.5*float(nowMs))`; ring two is `Tz(-.25*sinf(.5*float(nowMs)))*Rz(-.5*float(nowMs))`. The sine argument is radians and the rotation argument is degrees; no `/1000` occurs. ARM constants/calls are at `002b61c0–002b628c`. Accessories are skipped while building and elapsed < half duration, including new builds. They retain the current type's accessory handles during upgrades.

No autonomous mine/bait pulse, body bob, flashing tint, or animation-frame selection exists in the recovered `TOWERS_draw` and no-op `mineAimFunc 002d73e0` / `baitAimFunc 002cf668`. Their initial random yaw remains; idle drift is skipped. Packaged `landmineA/B/C` and `baitA/B/C` model transforms already include Z=-.25, cancelling the ordinary body +.25. This bounded negative does not claim every original UI path was searched.

`baitFireFunc 002cf66c` starts a distinct trigger effect: `EffectTapAction` receives the bait center and speed512, records realtime birth, uses the **bionicaura** model, projects the center with `GAME_orientedWorldToScreen2`, scales its screen-space XY by `512*realAgeSeconds`, and uses RGBA `(1,0,132/255,(1-age)^3)` for one second. Evidence is `selection` plus `selection-literals.tsv`; this is a screen-space expanding model, not a world-unit ring. After configured `chargedelay` elapsed on the simulation clock, bait unconditionally creates `EffectNukeExplosion2D`, applies slow/stun and removes itself. Native screen projection and exact device pixel sizing remain a desktop adaptation point.

## Selection highlight

`GAME_alloc 0029b8e0` loads **selection** (rect geometry + selection texture), independently of tower meshes. `GAME_render 0029efa0` draws it at `T(selectedCenterX,selectedCenterY,.1)*S(2,2,1)` after `PGL_setColor(selectionRGBA)`. `GAME_updatePieMenu 002f220c` sets green `(0,1,0,1)` for buildable, `(195/255,217/255,1,1)` for an existing tower, red for blocked, and `(1,0,132/255,1)` for return-code3. Exact code3 gameplay meaning was not re-proven here. The invalid flag switches to explicit red override with alpha=`1-elapsedSimulationMs/500`, drawing only alpha>0 and elapsed<=500. Body color is not overridden by selection.

Current range uses `rangecircle` at center Z=.15, XY scale=`range`, with a visible pie menu. Default range RGBA is `(205/255,235/255,139/255,1)`. Upgrade preview uses the same model at Z=.2 and proposed range, default `(1,116/255,0,1)`; for the first .75 realtime seconds, alpha=`1-(1-age/.75)^6`, then saved RGBA. This also scales the selected menu option by `1+alpha`. Literal evidence: `selection-model-literals.tsv`, `selection-colors.tsv`, `selection-literals.tsv`. Exact color inheritance through ordinary `PGL_renderModel` should follow the existing material renderer; these calls are not evidence for tinting all tower geosets.

## Integration limits

Windows now draws the recovered build/upgrade/sale body curves and model swap, separate bases, directionalshadow, Tesla accessories, boost rings and realtime pulse. Selling uses the current model even immediately after an upgrade; selling does not suppress Tesla accessories during its first half. The original selection rect and selected range use their native models/colors/heights. `--smoke-test` captures legal 50-gold build/upgrade/sale samples and verifies appearance save/legacy fallback through production rendering.

Full native yaw/elevation state, initial combat-RNG yaw, idle drift and firing-gate ordering are still absent. Bait's screen aura, invalid-selection fade and upgrade range fade remain open. Zero-duration endpoint and desktop selection controls remain adaptations. Static formulas and Windows screenshots require original runtime comparison before final visual-fidelity acceptance. No advertisement or purchase-verification behavior was investigated.
