<!-- Static native enemy animation formulas, state ownership, and implementation boundaries. -->
# Enemy animation

The ARM original selects geometry frames from game time, not distance travelled. These rules are statically recovered from `libverde.so`; they have not yet been checked against a running original. Ghidra addresses below include its `0x10000` image-base adjustment.

## Frame selection

`ENEMIES_draw` at `00297d4c` uses float32 operations and signed integer truncation in this order:

```cpp
// Preserve the reciprocal/multiply and both integer truncations.
int baseMs = int((1.0f / waveSpeed) * 150.0f);
int periodMs = int(walkAnimFactor * float(baseMs));
int frameMs = nativeEnemySlot + periodMs;
int sampleMs = dead ? deathFrameTimeMs
                   : stunRemainingMs > 0 ? stunFrameTimeMs : gameTimeMs;
int frame = (sampleMs / frameMs) % selectedFrames.size();
```

`150.0f` is immediate `0x43160000` at `00298214–0029821c`; the two conversions are at `00298220` and `00298270`. Do not collapse this to a double-precision expression or multiply animation frequency by `walkanimfactor`: that factor multiplies frame duration. The slot is added to duration, not time or phase. This surprising behavior is explicit in ARM instructions at `0029828c–00298294`, `002982b0–002982c4`, and `002982e8–002982fc`.

Use `subwalk` only when generation at enemy offset `0x5c` equals **1** and the native subwalk-model pointer is non-null (`00298234–00298268`); otherwise use `walk`. The selected list provides its own frame count. Generation greater than one does not satisfy the condition. Wave speed and walk factor are unmodified configuration values; slow strength and speed-up factor do not enter this frame formula.

The normal branch samples game offset `0x28`. Slow enemies sample that same time and use the status tint. Stunned living enemies sample enemy offset `0x14` and use the status tint. `ENEMIES_stunEnemy` at `0029a290` sets remaining duration to `max(old, requested)` and **always overwrites** offset `0x14` with current game time, including weaker/repeated hits. Thus a repeat hit can change the frozen frame. When the countdown crosses zero, the next draw resumes global-time sampling; there is no accumulated pause compensation.

On death, `ENEMIES_damageEnemy` at `0029997c` saves offset `0x10` as the current game time, or as offset `0x14` if still stunned. The corpse keeps that geometry frame while its death transform runs. Split children inherit slow/stun duration and slow strength, but initialize their stun-frame timestamp to **zero**, rather than inheriting the parent's timestamp. No birth timestamp or travelled-distance accumulator is needed for original walk-frame selection.

## Native slot lifecycle

Enemy offset `0x00` is its lookup-table slot, confirmed by spawn, damage lookup, save/load, and removal. It cannot be replaced by the desktop shared tower/enemy ID.

`ENEMIES_alloc` (`002964a0`) initializes all 1024 slots and its counter at `0x1094` to zero, so the first allocated slot is zero. `get_free_enemy_id` (`002975a0`) searches `slot = (counter + attempt) % 1024` for attempts 0 through 1023, chooses the first empty slot, stores the enemy pointer, and sets `counter = slot + 1`. Counter 1024 is valid and wraps on the next search. Failure sets the enemy ID to -1 and leaves the counter unchanged; callers handle the failed spawn. Literal mask `0xfffffc00` at `0029762c` and the 1024-attempt bound confirm the modulo.

Ordinary enemies obtain slots when actually released by `ENEMIES_spawn` (`00297c24`), not while queued aboard the UFO. Custom enemies obtain slots at their immediate spawn. Split children obtain their two slots in creation order while their dead parent still occupies its slot. `ENEMIES_removeEnemyID` (`00296878`) clears the slot before unlinking and freeing the enemy.

Leaks remove their enemy immediately in `ENEMIES_update`. Death does not immediately clear the slot. Each dead-enemy update performs:

```cpp
// Check before subtraction; retain original float32 countdown behavior.
if (deathRemainingSeconds > 0.0f && generation >= waveSpawnDepth)
    deathRemainingSeconds -= float(deltaGameMs) / 1000.0f;
else
    removeEnemyAndReleaseSlot();
```

Death remaining time starts at `3.0f` on allocation. A parent with further split generations is removed on its next dead-enemy update; a non-splitting or last-generation corpse persists through its countdown and is removed on a later update. `deathAt + 3000` is not an exact substitute: subtraction uses float32 and removal is tested before decrement. If game scheduling visits a newly dead enemy again in the current update cycle, that visit is the relevant next update. This note does not establish every caller's same-tick ordering.

The original serializes the entire enemy record (`ENEMY_toOBS` at `0029aa1c`) and the allocator counter (`ENEMIES_toOBS` at `0029aadc`), then rebuilds its slot table on load. A faithful desktop visual-slot implementation therefore needs a persistent slot counter, each live enemy's slot, and retained occupancy for corpses even if combat entities are erased immediately. Persist countdown state rather than deriving it from elapsed wall time. Reconstructing old desktop saves cannot recover historical original slots exactly.

## Heading update

Original enemy heading at offset `0x34` is a float32 angle in degrees. A normal or custom spawn initializes it to zero; a split child gets `GAME_randFloat(game) * 360.0f` from the combat RNG. `ENEMIES_update` computes direction from the current navigation target and old position, then updates heading only in its non-stunned movement branch:

```cpp
// All intermediates are float32; atan2 receives normalized Y then X.
float target = atan2f(dy / distance, dx / distance) * 57.295784f;
float difference = target - headingDegrees;
if (difference > 180.0f) difference -= 360.0f;
else if (difference < -180.0f) difference += 360.0f;
if (fabsf(difference) > 2.0f)
    headingDegrees += (float(deltaGameMs) * 0.005f) * difference;
```

The exact angle multiplier has bits `0x42652ee2` at `0029759c`; the update-rate coefficient has bits `0x3ba3d70a` at `00297588`. ELF PT_LOAD mapping places their bytes at file offsets `0x28759c` and `0x287588`. At a 25 ms update, the coefficient is 0.125. This is a per-update approach with a 2-degree dead zone, with no clamp on the coefficient and no final heading normalization. Slow reduces translation but does not change the turn coefficient. Stun freezes both position and heading for the update where it was active at entry, even if its countdown crosses zero during that update.

`ENEMIES_draw` directly supplies this heading to the Z rotation (`00298194–002981a4`), with no additional -90-degree offset in that draw path. `PGL_rotatef`, `PGL_translatef`, and `PGL_scalef` only forward to their standard OpenGL counterparts (`0028e5ac–0028e5d8`). Thus native body rotations use degrees and ordinary OpenGL postmultiplication. Desktop conversion between radians/degrees and imported model conventions must retain that complete transform; the body path contains no extra correction corresponding to a blanket desktop `ModelHeadingOffset`.

## Death geometry and shadow specification

This section specifies the original draw calls, independently of the current Windows implementation below. All scalar calculations use float32. Let `r` be the retained death countdown in seconds, `q=r/3.0f`, `q3=q*(q*q)`, `t=1.0f-q`, `s=wave.size`, `h=wave.height`, and `a=enemy.deathAngleDegrees` (native offset `0x38`). Do not clamp `q` or `t` in an exact transcription: removal checks before countdown subtraction, so a last draw can see a slightly negative countdown. The geometry frame remains the saved death-frame sample described above.

`ENEMIES_damageEnemy` stores `a=float(GAME_randIntMinMax(game,-45,45))` on every kill, after split creation and kill accounting. The helper at `0029ce6c` uses one combat random integer and signed remainder with range width 90, giving ordinary results -45 through +44. An implementation using independent visual randomness preserves the visual range but is an explicit approximation of direction and random sequencing. Do not add a new combat RNG draw merely to approximate this field without recovering the full consumption order.

Before every dead-body branch, the native call sequence is `T(x,y,z)`, `S(s)`, `Rz(headingDegrees)`, followed by the additional transform `D` below, then the chosen frame's original model/geoset transform. In column-vector notation, `M = T * S * Rz * D * modelLocal`. `Tz`, `S`, and `Rx/Ry/Rz` below mean local translation, uniform scale, and degree rotation in **listed call order**. Local offsets are therefore affected by the preceding scale; do not interpret them as world-space displacement.

For `t < 0.3f`, define `e=t/0.3f` and `e2=e*e`. For `t >= 0.3f`, define `v=(t-0.3f)/0.7f`. The first phase lasts approximately 0.9 seconds of the three-second countdown. Armor type 4 takes precedence over flying and speed tests.

| Enemy branch | `D` when `t < 0.3f` | `D` when `t >= 0.3f` |
| --- | --- | --- |
| Armor type 4 | `S(q3*q3)` | `S(q3*q3)` |
| Flying, armor != 4 | `S(q3) * Tz(-(4*s+h)*(e2*0.5f)) * Rz((e2*a)*4) * Ry(e2*90)` | `Tz(-v-(4*s+h)) * Rz(a) * Ry(90)` |
| Ground, speed <= 1, armor != 4 | `Rz(e2*a) * Ry(e2*-90)` | `Tz(-v) * Rz(a) * Ry(-90)` |
| Ground, speed > 1, armor != 4, a >= 0 | `Tz(0.2f*s + sinf(f*3.141f)*0.5f) * Rx(f*180) * Ry(f*10)`, where `f=min(e+e,1.0f)` | `Tz(0.2f*s-v) * Rx(180) * Ry(10)` |
| Ground, speed > 1, armor != 4, a < 0 | `Tz(0.2f*s + sinf(f*3.141f)*0.5f) * Rx(f*-180) * Ry(f*-20)`, where `f=min(e+e,1.0f)` | `Tz(0.2f*s-v) * Rx(-180)` |

The flying branch really omits its additional `S(q3)` in the late phase, and the negative-angle fast-ground branch really omits its late Y rotation. These discontinuities are visible in ARM control flow (`002985d8–002986b4`, `0029859c–002985d4`); do not silently smooth or regularize them. The fast-ground hop uses **3.141f**, not a substituted mathematical pi (`00298464–00298480`). At death start `t=0`, fast ground bodies already receive the local `0.2f*s` lift.

Every corpse receives color `(q,q,q,1)`, copied from the ordinary white color then replacing RGB (`00298318–00298368`). It darkens rather than alpha-fades; slow/stun tint is not retained after death. `PGL_renderGeosets` (`0028dfa0`) binds each material and then calls `glColor4f` with this supplied color, so it **overrides material RGBA**, rather than multiplying material color by corpse tint. Texture sampling and the material's blending/depth settings still apply. Normal living tint is `(1,1,1,1)`; living slow/stun tint is the original float32 `(0.7647059f,0.8509804f,1,1)` initialized by `ENEMIES_alloc`.

There is no special split-parent draw transform. A parent that can still split retains its initial `r=3.0f` until the next dead-enemy update removes it. If drawn in that interval, it uses the same table at `q=1,t=0` and its frozen frame. Giving this parent a separate three-second death animation would change the original lifetime. Its children are independent bodies, with generation-one subwalk selection as above.

All non-flying visible enemies, including corpses, draw the packaged `directionalshadow` model in a separate pass. The complete shadow instance transform is:

```text
T(x, y, 0.01f) * S((2*s / float(generation+1)) * (dead ? q3 : 1)) * Rz(-90)
```

The shadow has no heading rotation, body death translation, or death color override. Its material in `assets/data/effects.material.json` retains RGBA `(1,1,1,0.75)`, alpha blending, disabled depth writes, and no culling; its packaged geoset additionally translates local Z by 0.001. Reuse the complete model rather than a resized `shadowblob` sprite. Flying enemies skip this pass. Living-only armor aura models (`bionicaura`/`lightaura`/`heavyaura` for armor 1/2/3) disappear on death immediately and are not corpse shadows; their separate pass does not test flying status.

The original visibility test uses the untransformed enemy location, XY half-size `1.2f*s`, and Z range `[0,10]`, shared by body and shadow passes. Corpse transform excursions do not recompute that box.

Constant bytes, read through the original ELF PT_LOAD mapping (file offset = listed Ghidra address minus `0x10000` in this segment):

| Ghidra address | Float32 bits | Meaning |
| --- | --- | --- |
| `002989e4` | `3c23d70a` | ground overlay height 0.01 |
| `002989ec` | `3f99999a` | visibility half-size factor 1.2 |
| `002989f0` | `3e99999a` | first-phase fraction 0.3 |
| `002989f4` | `be99999a` | late-phase subtraction -0.3 |
| `002989f8` | `3f333333` | late-phase divisor 0.7 |
| `002989fc` | `3e4ccccd` | fast-ground lift factor 0.2 |
| `00298a00` | `40490625` | hop sine multiplier 3.141 |
| `0029685c` / `00296860` | `3f43c3c4` / `3f59d9da` | living status red/green |

A corpse record therefore needs its original XYZ, smoothed heading, wave/generation/frame identity (or an already resolved frame model), original size/height/speed/armor/flying properties, random death angle, and float32 countdown, in addition to slot occupancy. Keep death-frame and heading state frozen while countdown drives the table. The formulas and call order are statically established; exact combat random direction, same-tick visibility lifetime, and original runtime appearance still require validation. Additional evidence is in `agent/codex/p8-animation-timing/death-transform` and `death-material`; all exports used the existing read-only database.

## Current Windows implementation

`Game::walkFrame` in `src/game.cpp` implements the two float32 truncations, slot addition to duration, generation-one `subwalk` selection, and stun/death sample timestamps. Empty frame lists or nonpositive speeds return frame zero; nonpositive computed duration uses one millisecond. These fallbacks leave standard packaged data unchanged. The renderer should select the same frame list before using the returned index.

`Enemy::visualHeadingDegrees` stores the recovered smoothed angle independently of the existing instantaneous radians field `heading`. Ordinary spawns start at zero. Movement updates visual heading only when stun was inactive at tick entry; the existing position, damage, and combat RNG calculations are retained. Applied plasma/bait stuns refresh `stunFrameTimeMs`; split children inherit status duration but start that timestamp at zero. Split initial heading is currently approximated as zero because inserting the original random heading sample without closing the full RNG sequence would change combat outcomes.

The visual allocator owns 1024 cyclic slots separately from `nextId_`, with first slot zero. Exhaustion assigns `nativeSlot = -1` and uses slot-zero frame timing while keeping the combat enemy active. Leaks release occupancy immediately and create no corpse. Kills retain lightweight `EnemyCorpse` records: final/non-splitting generations start at `3.0f` seconds, and splitting parents start at zero. `moveEnemies` checks holds before float32 subtraction. Consequently projectile kills may update/release their hold in the same tick's following enemy phase, while tower kills wait until the next tick; exact equivalence of that caller ordering remains unverified.

`Game::corpses()` exposes these records as a read-only vector. Each new death captures the complete frozen `Enemy`, `frame = walkFrame(enemy)`, and `deathAngleDegrees`; `drawable` is true only for final/non-splitting generations. The frozen enemy carries world position, model, generation, slot, visual heading, and death-frame timestamp. The renderer should use the stored frame while applying its recovered death transform with `remainingSeconds`. Death direction is an explicit approximation: a deterministic unsigned slot/ID hash maps to the original integer interval [-45,44] degrees without consuming combat RNG. Slot-exhausted enemies still receive visual corpses. These records never enter combat, navigation, active enemy counts, or victory detection.

Version-one session core fields still use strict deserialization. Optional per-enemy `animation` objects and the root `animation` counter/holds preserve visual continuity for new saves. Drawable holds append `corpse = {enemy, frame, deathAngleDegrees}`, with the enemy's animation fields nested alongside its strict core record. Earlier holds containing only `slot` and `remainingSeconds` preserve occupancy but stay non-drawable; no pose is invented. Missing or invalid visual extensions rebuild slots in entity order, convert the existing radians heading, and sample the current saved time for frozen frames; the load message states that animation history was reconstructed. Historical slots, corpse poses/occupancy, and exact stun timestamps cannot be recovered from older saves.

main.cpp renders stored corpse frames through deathTransform and the recovered material-color override. enemyShadow uses the original directionalshadow model for living ground enemies and corpses; living armor aura models and native status tint are retained. Death-angle direction and split initial yaw remain approximations described above.

Existing coverage cases check first allocation, staged frame-duration arithmetic, stun freezing, generation selection, split-parent occupancy, frozen corpse capture, corpse save/continuation, older occupancy-only extensions, optional corpse-data degradation, and legacy core-save compatibility. The integrated build passed all 23 coverage cases, natural-match simulation, production input replay, three window layouts, all-map GL drawing and resource loading. Input replay captured death phases at 300/900/1800ms. These checks verify the Windows implementation; original-game dynamic validation remains outstanding.

## Evidence limits

Required state is independent native slot allocation/occupancy, stun-frame timestamp, and smoothed heading; corpse drawing additionally requires death-frame timestamp, float countdown, generation, and frozen transform state. Existing combat slow/stun remaining times are sufficient if their original update order is retained. Keep a separate internal angle if changing the existing heading would affect combat calculations; no render-frame interpolation appears in the examined draw path.

The evidence closes the draw formula, death transforms, original tint values, and the listed field lifecycles. It does not establish cross-ABI equivalence, every same-tick spawn/death caller order, zero-length navigation-vector behavior, or handling of externally malformed assets with zero frame duration/count. Successful desktop rendering alone is not dynamic fidelity validation.

Focused evidence: `agent/codex/p8-animation-evidence/{pseudocode.c,instructions.tsv}` contains draw; `agent/codex/p8-animation-timing/{pseudocode.c,instructions.tsv}` contains update/status; the `lifecycle` and `removal` subdirectories contain creation, allocation, damage, persistence, and release. All were exported with `-readOnly -noanalysis`; original APK and database were not modified.
