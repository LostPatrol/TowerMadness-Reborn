<!-- Read-only campaign review and isolated original MENU late-combat save/restore acceptance. -->
# Late battle restore acceptance

## Read-only campaign review

Reviewed the main `campaignMadnessDefense` and `campaignall` driver against the independently successful dynamic-stage strategy. Opening and expansion cells, original upgrade targets, Gauss cap, support cost reservation and busy-tower checks match. Support readiness is recomputed from this GAME every frame; the only strategy static is the immutable cell array. Madness sends no immediate opening Space: first request is scheduled25 host seconds after entry, then every25 seconds. Continuation click counters reset for each later campaign. No concrete duplicate-upgrade, duplicate-wave or cross-GAME strategy-state fault was found. MENU starts building and toggles4x immediately on Madness entry, whereas direct trials used >=1s and >=0.7s; both retain the25-second pre-wave window.

## Isolated test setup

All custom source/build/state/evidence is under `agent/codex/x86-continue-20261016/medium-strategy/latebattle*`. Main host, current build, original ELF/assets and real profile were not modified. Source starts from candidate22's frozen input (SHA256 `B6840CF4D6945B2B4A87889B2991D23CF93C63F39BE09E6BEDA7B52FCDAF9637`). The original MENU four-campaign chain's fresh Madness F5 file was copied read-only into isolated state, together with its platform store. Original3812-byte save file SHA256 `7C1B5E51C81856ABF9B7738D7DAB479D4F5678628A27B060B93AAF849A7DE2A4`. It restores at original10 sheep,100 coins,gameStarted=false and simulation0 through MENU continuation. Runs are Silent; system volume was untouched.

The acceptance driver uses original commands only. A late F5 produces exact original GAME_save OBS bytes; F9 queues replacement at the next existing frame boundary, detaches/frees the old GAME using GAME_free, then reconstructs through GAME_fromIBS and original MENU resume/install logic. No snapshot fields are copied into the new GAME and no coins/lives/win state are written. The driver requires simulation advancement eight host seconds after the late restore, followed by natural original victory and visible original settlement.

## First attempt

`latebattle/evidence/menuauto-20261016-130111-127/` naturally reached the original victory with5 sheep, but never met the11-tower plus >=2 tower-field25-reference save gate. It exited3 with `Victory did not cover late restoration`, so this is a failed restore acceptance, not a pass. GL error count0, build binding passed. The tower's field25 is not the total original projectile container; using it to gate total projectile coverage was an incorrect assumption.

## Persistent-projectile variant

`latebattle-projectiles` retains the proven9-Gauss/full-slow/full-boost layout until all9 Gauss towers finish, then legally buys two original Mortar I towers at `(6,24),(14,24)` with original costs. It requires13 towers and >=2 actual nonempty tower projectile references at simulation>=800000 before F5/F9. A dedicated isolated runner checks the custom continuation, original free/restore, activity evidence, post-restore advance and natural victory markers, instead of the unrelated short tutorial/pause scenario requirements.

`latebattle-projectiles/evidence/menuauto-20261016-130751-041/` also naturally reached victory (8 sheep), but the same incorrect tower-field25 gate prevented a late save. It exited3 with `Victory did not cover late restoration`; acceptance failed, build binding passed and GL errors0. Its firing/visual resources do not replace active-container evidence.

## Correct original active-container variant

Existing original pseudocode establishes the actual serialized ownership: `GAME_save` at `analysis/menu/pseudocode.c:4515` calls `ENEMIES_toOBS(obs,GAME[3])` and `PROJECTILES_toOBS(obs,GAME[5])`. `ENEMIES_fromIBS` rebuilds the active list at container[0x405], traversing enemy[0x1c]; `PROJECTILES_fromIBS` at line8634 rebuilds container[1], traversing projectile+0xbc. `latebattle-active` uses these verified lists for read-only activity counts, with a generous8192-node bound solely to prevent an invalid diagnostic traversal from hanging.

This third variant returns to the exact successful11-tower Gauss/slow/boost strategy, without additional mortar purchases. It waits for simulation>=800000,11 towers,>=2 actual PROJECTILES and>=1 actual ENEMIES. After F5/F9, it compares the reconstructed active entity counts and original player evaluators immediately before any resumed update, then requires further clock advancement and natural original victory. Values are compared as evidence only, never written back into GAME. Outcome is pending and will be replaced with the actual terminal result.
