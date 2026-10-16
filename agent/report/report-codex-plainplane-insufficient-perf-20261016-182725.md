<!-- Original first-map unaffordable-upgrade dispatch and queued-frame timing investigation. -->
# PlainPlane insufficient-funds performance diagnosis

**The reported click stutter remains unresolved: it was not reproduced in these isolated first-map tests.** No evidence-based game/audio optimization was applied, and no production source was modified. This supersedes the earlier Peach-only negative result with the user's actual first map and verified original upgrade requests.

The isolated host copies the frozen candidate26 implementation and adds timing around Windows down/up dispatch and each original GAME_update/GAME_render/SwapBuffers call. Capture/JSON writing is outside the measured frame regions. A one-byte diagnostic INT3/single-step counter at the original GAME_upgradeTower entry counts actual calls without replacing its implementation: after executing the original first instruction it reinstates the counting breakpoint. This is process-local instrumentation in an independent executable, not a production hook or gameplay change. Actual original SOUND_enableEffects(1.0) is used and audio backend enabled, while system volume is untouched.

All cases load original PlainPlane.gamemode/Easy.tech, start with50 money/10 lives, and use original mouse clicks to construct Plasmatron I(type57,cost5), upgradeII(type58,cost10), upgradeIII(type12,cost20). Money reaches15. The original IV option costs40. Every one of20 repeated insufficient-funds option clicks increments the original GAME_upgradeTower counter exactly once, from2 after valid upgrades to22 after failures. The tower always remains completed type12. Therefore these are verified genuine repeated upgrade requests, not accidental ground/menu clicks.

| Case | Repeated-click frames | Max GAME_update | Max GAME_render | Max SwapBuffers | Native result |
| --- | ---: | ---: | ---: | ---: | --- |
| Before first wave | 72 | 38µs | 947µs | 1239µs | exit0 |
| Original wave started, wait3 seconds | 75 | 197µs | 2685µs | 1586µs | exit0 |
| Original wave started, wait20 seconds; real attacks/audio | 76 | 188µs | 393µs | 1299µs | exit0 |

No measured click, original update, render or swap exceeded50ms during these repeated requests. Pre-wave clicks were275–1223µs. The final combat case confirms actual AUDIO_SOURCE_PLAY and AUDIO_EFFECT events during repeats, not merely SOUND_READY: source8 played clips0 and1. Its final state is money27, score12, lives10, gameStarted true, ended false, type12 completed. Original combat earnings remain below the40 upgrade price, so all repeated attempts are genuinely unaffordable throughout. Original type/money snapshots are retained for every step; no money/life/speed/unlock memory field was written.

Evidence root: `agent/codex/insufficient-perf-20261016/`:

- `plain-prewave/run/`: original type/money assertions, all20 original entry counts, exact dispatch/frame timings, captures and native exit0.
- `plain-battle/run/`: original first wave after3seconds, all20 entry counts and timed queued processing.
- `plain-late-battle/run/`: first-wave combat after20seconds, actual audio calls and earnings, all20 entry counts and timings, native exit0.
- `plain-repeat.json`, `plain-battle.json`, `plain-late-battle.json`: exact click-only scripts. Independent host additionally supports a script `space` event solely to send real WM_KEYDOWN/VK_SPACE through the existing Windows route.
- `host.cpp`, copied headers and `build/`: isolated profiling inputs and executable. Root/current/release EXEs were not changed.

The timing covers queued original work in GAME_update as well as immediate click dispatch. Original failure requests therefore were not dismissed solely because enqueue was cheap. Source review found no synchronous host failure sleep/modal dialog. Native audio voice creation/destruction are synchronous boundaries, but the actual combat-audio case produced no slow measured frame and does not justify speculative voice pooling or suppression. Rejected upgrade clicks did not themselves emit a distinct sound event in this case; normal original firing did.

Remaining reproduction limits: these are cold-start direct original-GUI first-map runs, fixed original480×800 viewport, using one Plasma tower and the old candidate26 runtime under an isolated output/state path. A different tower, saved account setting, pause menu state, long-running battle, live resize or user display/driver condition may still matter. The user's intermittent stutter must not be marked fixed by unrelated FPS/resize work or by this negative result. No debounce, ignored click, affordability rewrite, sound workaround, production source edit, staging or commit was performed by this agent.
