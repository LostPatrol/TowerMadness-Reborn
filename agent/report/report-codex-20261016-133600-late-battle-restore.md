<!-- Original-x86 late-combat restoration evidence and opt-in verification integration. -->
# Late battle save/restore

## Result

The original active battle restores exact original player evaluator values and active enemy/projectile counts, then continues to natural original victory. The earlier loss was caused by the verification driver's host-time wave cadence after the original restore reset play speed, not an established gameplay restoration defect.

`GAME_fromIBS` calls `GAME_alloc`; original `GAME_alloc` initializes `GAME+0xa68` to 1.0. The original speed button changes this period multiplier through 1.0, 0.5 and 0.25. The driver previously sent a wave every 25 host seconds at 4x before F9, but kept the same host cadence at 1x afterward. The isolated correction clicks the original speed button twice after exact-state comparison and resets the next wave's host deadline. It does not write speed or gameplay state.

## Actual evidence

- `medium-strategy/latebattle-active/evidence/menuauto-20261016-131619-301-run.json`: exit 3, acceptance false, GL errors 0. At simulation 800800, 11 towers, 7 enemies and 2 projectiles were saved and restored with identical original player state. Simulation advanced to 805700, but the driver later lost at approximately 953000. No victory claim is made for this run.
- `medium-strategy/latebattle-speed/evidence/menuauto-20261016-132808-572-run.json`: exit 0, acceptance true, GL errors 0. Begins from the first run's authentic late checkpoint, saves/restores the same 11-tower/7-enemy/2-projectile state at simulation 800800, advances to 820175 and reaches natural victory at 1233975 with 9 sheep and score 4588.
- `medium-strategy/latebattle-speed-fresh/evidence/menuauto-20261016-132856-452-run.json`: exit 0, acceptance true, GL errors 0. Begins from `allcampaign21/state/menu-save-hotkey.bin`, the original MENU campaign chain's fresh Madness checkpoint. Legally builds the full strategy, saves/restores at simulation 800800 with 11 towers, 7 enemies, 2 projectiles, 8 sheep, 48 coins and score 2228. Exact player/count comparison passes before resumed updates. Simulation advances to 820425 and reaches original victory at 1234050 with 8 sheep and score 4576.

The two corrected isolated runs use EXE SHA256 `2AD698B8486B306BCC73AD7B0DBCCC49592950D5C7367DAFFDA0CA20188D4AEB`, source SHA256 `1026393C8EB36914DD6706D15B4D138947CABC756DDE78353D18A8B06E54A7A7`. Their original randomized combat is not claimed to be an identical-seed or frame-identical comparison.

## Integrated scope

`tools/x86-reuse/late-battle-probe.hpp` contains the independent opt-in driver and read-only original linked-list counting. The main host only selects it for `menuauto:latebattle`, loads the original F5 checkpoint through existing continuation, compares restoration before the first update, and advances the scenario through original commands and pointer events. Normal menu/play modes do not call it.

`build.ps1` freezes/hashes the header alongside existing inputs. `run.ps1` retains existing continuation, restore stress and new-game conditions; only the explicit latebattle branch requires continuation, real old-GAME release, F9 restore, mature active-state evidence, exact restoration, subsequent advancement and original victory. Its general restored/final simulation advancement requirement still applies.

Original ELF/assets and actual user data were not modified. Silent combat runs did not change system volume. No coins, lives, enemies, victory or unlock state were written by the driver. Main integration build validation follows below when complete.

## Integrated candidate 25

Frozen main-source build completed at `2026-10-16T05:38:41Z` in `agent/codex/x86-continue-20261016/candidate25`. EXE SHA256 `AFD3FE3D56D6360DEF6AACA91E729D5D458E0DDDC8611ABE7A8EFA2A39CBB7C9`; host SHA256 `0175E13C46482B5B440172200B750309D7F2713BD37BAFC163D1FF443D89C76B`; late header SHA256 `9E5DC81942B6DD8A3250EB547DE9BE87A43B15CFB97D6C198F653DF3805EE474`. Build includes the coordinated latest original music and weapon verification headers.

`audio25/evidence/audio-20261016-133934-874-run.json` passed with exit 0 and GL errors 0 using real audio, without changing system volume. Original audio completion reports `effects=5 music=2 musicIsolation=1 pendingCancellation=1`.

`latebattle25/evidence/menuauto-20261016-133947-753-run.json` passed: exit 0, acceptance true, build-bound true, GL errors 0. It began from the authentic fresh Madness checkpoint (3812 bytes, SHA256 `7C1B5E51C81856ABF9B7738D7DAB479D4F5678628A27B060B93AAF849A7DE2A4`). At simulation 800050 it saved and restored 11 towers, 7 enemies, 2 projectiles, 10 sheep, 46 coins and score 2216 with exact original evaluator/count agreement. After eight host seconds it advanced to 819650 with 10 sheep; natural original victory followed at 1255275 with 10 sheep and score 4588.

The coordinated current EXE `A3939D925CDA589B41248111590955621E78E86A771D432084A51DA111CC3C82` also passed independently in `latebattle-final/evidence/menuauto-20261016-134504-754-run.json` from another isolated copy of the same fresh checkpoint: exit 0, acceptance true, build-bound true, GL errors 0. At simulation 800500 it saved/restored the mature 11-tower, 7-enemy, 2-projectile state with 7 sheep, 32 coins and score 2222 exactly. It advanced to 819800 and naturally won at 1238975 with 6 sheep and score 4561. This run remains valid evidence for that precise EXE; a later coordinated current rebuild for a separate new-game verification timeout will need its own final late-battle run.

Current was built by the coordinated final validator; this subtask does not rebuild current or commit changes.

## Final coordinated EXE

`latebattle-final2/evidence/menuauto-20261016-135312-359-run.json` passed the final coordinated EXE independently: exit 0, acceptance true, build-bound true, GL errors 0. EXE SHA256 `677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`; host SHA256 `4C52456CECFDCDC66DC4D4F4B2201605119938FF1C1F79194A0F9F925B4EA870`. The test bound `candidate26`, whose exact frozen executable/inputs are designated for current without relinking.

It starts from another isolated copy of the authentic fresh Madness F5 checkpoint. At simulation 800675, 11 towers, 7 enemies, 2 projectiles, 5 sheep, 6 coins and score 2186 were saved and restored exactly before resumed updates. Eight host seconds later the original simulation had advanced to 820350 with 5 sheep. Original natural victory completed at simulation 1235475 with 5 sheep, score 4558 and finalScore 4809. The actual log is `latebattle-final2/evidence/menuauto-20261016-135312-359/menuauto-20261016-135312-359.log`. Completed by 2026-10-16 05:58:39 UTC. No further source edits were needed.
