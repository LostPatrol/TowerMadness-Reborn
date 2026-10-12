<!-- Static projectile-effect recovery report; no native gameplay changes. -->
# Projectile visual specification

Completed static ARM recovery for missile explosion layers, homing hit position and blue variants, flak early blast, trail geometry and projectile sampling cadence. Added `docs/PROJECTILE_VISUALS.md` and a focused literal/index evidence record under `agent/codex/p8-projectile-visuals`.

Recovered missile fixed normalization and five layers, including visibility ending at .75 seconds despite a randomized 1.5–4.5-second record lifetime. Confirmed missile/flak sprite sizes do not use the plasma/flame viewport multiplier. Flak early blast is a point sprite with the same size/alpha as the main burst, offset +.05 in Z.

Recovered the trail's exact positive-diagonal world-space quad, additive blend, .25-second lifetime and original width formula. Tracking samples after each strict .2-unit traveled threshold; ballistic ordinary/nuclear paths use .1/.2-second thresholds. First positive movement may sample immediately. Nuclear descent retains existing progress, threshold and sample endpoint. Energy Pulse Missile and four other weapon-name prefixes are excluded from tracking trails.

Validation used preserved Ghidra ARM instructions, targeted pseudocode, original asset material/model JSON and direct read-only ELF literal/index bytes with PT_LOAD mapping. Reused existing exports; did not reopen or mutate the Ghidra database. No native source, combat RNG, damage rules, MEMORY, README, TODO or task files changed by this subtask. No commit/push and no runtime test were performed because the deliverable is a static specification.

Remaining unverified: original runtime appearance, inherited trail depth state, semantic names/defaults of game fields +0xa68 and +0xbe4, external visual random seed/sequence, pause/speed realtime behavior and platform pixel calibration. These are explicitly documented; no claim of original runtime equivalence is made. Parent integration owns any implementation, MEMORY update and native verification.
