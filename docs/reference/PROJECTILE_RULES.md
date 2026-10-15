<!-- Recovered projectile and shared area-damage rules for combat reconstruction. -->
# Projectile rules

> 旧 x64 重建与原作静态规格参考；当前主线见 [X86_CORE_REUSE.md](../X86_CORE_REUSE.md)。原 ARM 地址不能直接用于 x86。旧运行过程已清理，保留的静态导出在 `agent/codex/reference/arm/`。

This is static recovery from the original ARM `libverde.so`, not a phone run. Primary evidence is 历史过程记录（已清理）; supplemental functions are in `agent/codex/reference/arm/projectile-evidence`. Exact literal bytes and ELF address mapping are in 历史过程记录（已清理）. Ghidra addresses below include its verified `+0x10000` relocation. Purchase verification and advertisements are outside scope.

## Data and units

Read weapon parameters from the selected `assets/data/*.tech.json`, never from a substitute weapon. The five relevant families' numeric weapon parameters are identical across Easy, Medium, Hard, Insane. Tower range, reload, elevation, muzzle offsets, armor multipliers, upgrades and enabled state still come from each tower definition.

| Family | Weapon fields | Moving projectile |
| --- | --- | --- |
| plasma | damage, speed, stun, duration (ms) | Tracking point, type 1 |
| railgun | damage, radius | Instant forward strip |
| homing | damage, speed, splash, volley, spread | Random waypoint type 4, then type 1 |
| mortar | damage, splash, speedmultiplier | Computed ballistic velocity, type 0 |
| flak | damage, splash | Instant random XY radius |

Keep projectile position, previous position, origin, endpoint, waypoint, planar ballistic direction, elapsed seconds, traveled distance, launch speed, speed multiplier, elevation radians, target ID, source tower, damage integer, splash radius and state. `PROJECTILES_create` (`002ad78c`) initializes speed and speed multiplier to one; elapsed and traveled distance to zero; target and source IDs to −1; newly created entries go at the list front.

## Shared damage shapes

`ENEMIES_damageEnemiesInRadius` (`0029a400`) checks original tower/enemy eligibility, then strict planar `distance² < radius²`. Each raw hit is `int(float(rawDamage) * (1 - distance / radius))`. Boundary hits are excluded; there is no constant-damage inner zone, Z test, or collision-radius expansion. Generic enemy damage then applies the source tower's armor/heat handling.

`ENEMIES_damageEnemiesInLine` (`002996a8`) uses origin `O`, aim `T`, direction `D=T-O`, and enemy point `E`:

```text
projection = dot(E-O,D)
perpendicularSquared = cross2(E-O,D)^2 / dot(D,D)
forwardSquared = projection^2 / dot(D,D)
hit iff eligible && perpendicularSquared < weapon.radius
                 && projection > 0 && forwardSquared < length^2
```

The `radius` field is compared directly with squared perpendicular distance. Do not square it again. Railgun length is `1.5 * tower.range`; its origin is the tower center `(tileX+1,tileY+1)`, not the muzzle. Railgun raw damage is `int(weapon.damage * currentTowerDamageBoost)`.

## Muzzle and aim

`TOWERS_muzzleOffsetTransform` (`002b4ae8`) and its dual version (`002b4da4`) use **negative** degree-to-radian conversion (`bc8efa35`). This differs from ballistic physics' positive conversion.

```text
A = -(originalTowerYawDegrees+180) * 0.0174532924
B = -originalTowerElevationDegrees * 0.0174532924
m = selected muzzleoffset; r = rotationoffset (zero if absent)
x = tileX+1+r.x + m.x*cos(A)+m.y*sin(A)*cos(B)+m.z*sin(A)*sin(B)
y = tileY+1+r.y - m.x*sin(A)+m.y*cos(A)*cos(B)+m.z*cos(A)*sin(B)
z = 0.25+r.z - m.y*sin(B)+m.z*cos(B)
```

With the desktop `heading=atan2(targetY-centerY,targetX-centerX)`, use `A=π/2-heading`. The two muzzle transforms share their rotation and apply distinct offsets. `WEAPON_directAimWithZ` (`002dd9c8`) aims elevation at `atan2(enemy.height+wave.offset-0.25, horizontalCenterDistance)`, using 90° when distance <0.001. The original tower smooths pitch as well as yaw; the desktop currently derives pitch immediately in its muzzle helper.

## Tracking update

`PROJECTILES_update` (`002ad9ec`) updates types 1, 2, 4 and 5 as point trackers. For live target type 1/2, endpoint is `(enemy.x,enemy.y,enemy.z+wave.offset+0.25)`; type 4 updates a separate final endpoint while following its fixed waypoint. A missing target changes to type 5 and keeps its last endpoint; type 4 first copies its final endpoint over the waypoint.

Before movement, distance <0.35 hits. Nuclear descending tracker type 2 instead hits at distance <1; after target loss type 5 uses 0.35. Reaching a type-4 waypoint switches to type 1 and reprocesses the same projectile in the same tick. There is no segment collision or distance clamping.

```text
desired = normalize(endpoint-position)
old = position-previous
if length(old) >= 0.0001:
    maximumTurnRadians = traveledDistance*10*0.0174532924
    if dot(normalize(old),desired) < cos(maximumTurnRadians):
        rotate normalize(old) toward desired around normalize(old×desired)
        by maximumTurnRadians
previous = position
step = speed * gameStepMs * 0.001
traveledDistance += step
position += desiredOrCappedDirection * step
```

The turn cap depends on accumulated distance, not just this tick's movement. Type-5 impact still invokes the weapon callback: plasma damage to the vanished target no-ops; radius weapons can damage other eligible enemies near the final point. The original has no recovered projectile lifetime limit.

## Weapon-specific launch and hit

Plasma (`plasmaFireFunc 002d98fc`, `plasmaProjectileHit 002d9818`) launches one type-1 point at the muzzle. Damage is truncated once from weapon damage × tower boost. On hit it damages the target first, then, if stun chance >0, consumes a synchronized random float and stuns when random < chance. Stun duration merges by maximum. Cosmetic brightness is 164+0.2×integer damage below damage 450, otherwise 254.

Flak (`flakFireFunc 002d1ac0`) damages immediately at `(targetX+R1-0.5,targetY+R2-0.5)`. A third synchronized random float changes explosion height only. Raw damage is `int(weapon.damage)` **without tower damage boost**. Dual muzzles create dual tracers but only one area-damage call.

Homing (`homingFireFunc 002d3e18`, `homingProjectileHit 002d3c24`) creates `volley` shells per muzzle. `homingLoadProjModel` (`002d588c`, supplemental `projectile-model-evidence`) chooses `grenade` for ordinary guided missiles and `pulseburst` when the tower's `ispulsemissile` flag is set. Each shell has damage `int(weapon.damage*boost/(muzzleCount*volley))`, configured speed/splash, and launch XYZ jitter `spread*(R-0.5)` independently per axis. With `D=targetXY-launchXY` and `d=length(D)`:

```text
forward = 0.2+0.25*R; side=0.4*(R-0.5)
waypointXY = launchXY + forward*D + side*(-D.y,D.x)
test = 0.5*d*R
waypointZ = test>=4.5 ? 5 : 0.5+0.5*d*newRandom()
```

This height rule takes a new sample in the low branch; it is not `min(5,test+0.5)`. The single-muzzle branch samples side before forward; the first dual muzzle does likewise, the second dual muzzle samples side then forward. Impact damage uses current projectile XY and linear radius falloff; visual hit jitter uses external cosmetic RNG. No lead prediction occurs for homing.

## Ballistics and nuclear variants

`PROJECTILES_computeBallisticVelocity` (`002aebec`) takes current horizontal target distance `d`, actual muzzle height `h`, and positive elevation radians `θ`:

```text
V = sqrt(4.9*d*d / (cos(θ)^2*h + sin(θ)*cos(θ)*d))
```

Its 4.9 coefficient is a double literal (`9999999a 40139999`), converted back to float after multiplication. `mortarAimFunc` (`002d8348`) computes V before lead; `WEAPON_ballisticAim` (`002ddafc`) calls `ENEMIES_predictEnemyPosition` (`0029a63c`):

```text
flightTime = d/(V*cos(θ))
predictedXY = currentXY + (currentXY-previousXY)*flightTime*gameStepMs/speedmultiplier
```

The original explicitly multiplies the millisecond step without a 0.001 conversion; do not substitute a path planner or a conventional velocity predictor. It stores predicted XY in the enemy record `+0x2c/+0x30`; mortar captures that aim at fire time.

`mortarFireFunc` (`002d854c`) starts origin/position at muzzle, planar direction at normalized(predictedXY−muzzleXY), and follows the analytic arc. Its parser caches model `grenade` for Mortar I, `mortarshell` for other ordinary mortars, `nukemissile` for Nuke and `hbombmissile` for H Bomb:

```text
stepSeconds = gameStepMs*0.001*speedmultiplier
t += stepSeconds
xy += planarDirection*cos(θ)*V*stepSeconds
z = origin.z+sin(θ)*V*t-4.9*t*t
if z<0: invoke hit callback at current position
```

Nuke and H Bomb are recognized by weapon name in `towertype_mortar_parse` (`002d92fc`), launch as type 3, and switch to type 2 when descent begins. The descending seeker follows the moving target with the common turn rule. Original record offset `+0x58` aliases elapsed seconds and traveled distance: descent retains elapsed seconds as the initial turn-cap distance, then adds tracking movement. H Bomb applies +2 heat to all enemies strictly inside splash **before** radius damage; heat clamps to two. Nuke does not heat. Both preserve configured ballistic speed multiplier during ascent; tracking uses V×stepSeconds without that multiplier. Ordinary mortars remain ground-impact arcs.

## Limits for implementation

The recovered arithmetic and branches are static observations. Phone runtime comparison remains unavailable. Original smooth pitch timing, degenerate tracking cross products and exact ARM floating-point equivalence remain unverified. The current desktop implementation keeps degenerate vectors finite and derives muzzle pitch immediately. Review those limits separately from the recovered weapon rules; do not describe a successful desktop smoke run as original runtime equivalence.
