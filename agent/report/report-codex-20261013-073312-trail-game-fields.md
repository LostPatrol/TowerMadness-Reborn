<!-- Focused static follow-up identifying original trail control fields. -->
# Trail control fields

Closed both previously unnamed fields in `docs/PROJECTILE_VISUALS.md` using existing GAME_alloc/update evidence and a new bounded read-only export from the preserved Ghidra database. New evidence is under `agent/codex/p8-projectile-visuals/game-flags`; no source or shared memory files were changed.

`game+0xa68` is inverse playback speed, initially float1. Original toggle cycles 1, .5, .25 with Normal/2x/4x strings; replay slider writes its reciprocal when above .1, otherwise zero. Original trail draw uses this current value as its alpha multiplier: full/half/quarter alpha for those playback speeds. It is not generic scene opacity. This also establishes an original 1/2/4 playback-speed mechanism separately from the temporary opponent-induced speed-up mechanic; whether each UI context exposes it was not investigated.

`game+0xbe4` is graphics mode, proven by `GAME_setGraphicsMode` and MAP_render usage. Allocation memset initializes it to zero; mode0 renders trees and allows trails, nonzero mode suppresses trails, and mode3 also skips doodads. A normal-graphics-only recreation can use zero without inventing a user option. Full saved preference/UI selection flow was not traced.

Validation: checked ARM stores, branches, literal `.1f`, original mode strings and memset bounds. Ghidra ran with `-readOnly -noanalysis` and exited normally. No combat, RNG, UI or runtime changes were made. Original visual timing, inherited depth state and runtime appearance remain unverified.
