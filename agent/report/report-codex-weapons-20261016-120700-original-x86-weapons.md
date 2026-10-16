<!-- Isolated original-machine-code weapon API acceptance and integration notes. -->
# Original x86 weapon API probe

The isolated `weapons/build-chain` executable completed 12 representative original build → upgrade → sell cases on shipped Peach.gamemode. Original starting money remained 2500, lives 10, score 0, and gameStarted false throughout; no APK, ELF, assets, money, lives, or wave state was edited. All cells were selected by original `TOWERS_canBuildHere(towers,x,y,0,type[34]) == 0`, giving cell (12,10). Tower existence/type/completed construction and actual disappearance after sale were checked through original exports.

Evidence: `agent/codex/x86-continue-20261016/weapons/evidence-chain/upgrade-20261016-120534-420/weapons-result.json`, native log, runner manifest, and PNG captures. Native exit 0 and explicit WEAPONS_DONE count=12. Frozen executable SHA256 BB10E887467A0EDD51EBD7044F57841D04362B6D1710B93B6632B84CC6E69E5F; frozen host.cpp SHA256 40852ACFD1241667B4B0ABCD9AD49D6F2A51922BE42E82291965FB64B304DA14. All remaining frozen input hashes are in build-chain/build-manifest.json.

| Family | Original type IDs | Money: before / built / upgraded / sold |
| --- | --- | --- |
| Plasma | 57 → 58 | 2500 / 2495 / 2485 / 2500 |
| Laser | 25 → 32 | 2500 / 2490 / 2470 / 2500 |
| Mortar | 52 → 26 | 2500 / 2490 / 2465 / 2500 |
| Flak | 9 → 17 | 2500 / 2450 / 2400 / 2500 |
| Guided missile | 49 → 18 | 2500 / 2475 / 2425 / 2500 |
| ElectroSlow | 22 → 54 | 2500 / 2475 / 2450 / 2500 |
| ElectroBoost | 10 → 47 | 2500 / 2420 / 2340 / 2500 |
| ElectroShock | 48 → 62 | 2500 / 2425 / 2325 / 2500 |
| Railgun | 16 → 7 | 2500 / 2440 / 2350 / 2500 |
| Proximity mine | 28 → 11 | 2500 / 2490 / 2440 / 2500 |
| Flamethrower | 13 → 29 | 2500 / 2200 / 1454 / 2500 |
| Nuke/H Bomb | 52 → 26 → 37 → 43 → 24 → 1 → 27 | 2500 / 2490 / 37 / 2500 |

Nuke coverage followed every native catalog edge from Mortar I: cost 10+25+53+131+219+525+1500=2463. Intermediate balances were 2490,2465,2412,2281,2062,1537,37; native pre-wave sale refunded2463. This uses legal prerequisites and never receives additional funds.

An earlier isolated exploratory run directly built Nuke and received the original cumulative-tech sale value, yielding an extra438 (Mortar I–V prerequisites not purchased). That run is rejected as player-legal Nuke coverage; its separate evidence/build directories are retained only to explain the finding. It also chose cell(2,2), which did not visibly show towers. The accepted chain run chose interior(12,10); H Bomb capture was visually inspected and shows the original tower and money37. The repeated Mortar I base capture uses the same filename, so there are23 distinct accepted PNGs rather than24.

No missing platform selectors or adapter bugs appeared. This verifies original API construction, catalog-chain upgrade, refunds/removal, and scene rendering. It does not verify GUI wheel traversal, every upgrade tier, targeting, combat damage/effects, mine detonation, nuke launch, support influence, or gameplay balance.

Integration: `agent/codex/x86-continue-20261016/weapons/weapons-probe.inc` contains the standalone originalWeaponsProbe function. Insert it after nativeTowerId helpers. Call `originalWeaponsProbe(game,mode,output); return;` after originalScene installs activeGame/hostMenu slots for a dedicated acceptance operation, or preserve the isolated `operation == "upgrade" && scenario.modeName == "Peach"` invocation. The current isolated host uses the latter without touching shared tools/CMake. Root should maintain MEMORY.md centrally because it owns concurrent main-source work.
