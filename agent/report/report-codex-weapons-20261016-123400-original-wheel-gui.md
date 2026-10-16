<!-- Original wheel GUI coverage, purchase-boundary correction, and frozen verification inputs. -->
# Original weapon wheel GUI

All 12 base towers completed original wheel category selection → build → first upgrade → two-tap sale confirmation. The isolated accepted run used 97 scripted Windows mouse-click/wait steps with explicit original type, completed-construction, money, tower-removal, and menu-kind assertions. No construction/upgrade/sell API was directly invoked by this driver. It only dispatched WM_LBUTTONDOWN/UP; the original EAGLView/GAME/PIEMENU code generated gameplay commands. Original API readers inspected the results.

Shipped Peach started with2500 money,10 lives and no wave; each tower's real costs were deducted and original pre-wave sale returned2500. Final state: no tower,2500 money,10 lives,0 score,gameStarted=false,simulation94775ms. Original assets/ELF and gameplay fields were untouched. Native exit0, glErrorCount0.

Accepted evidence: `agent/codex/x86-continue-20261016/weapons/gui/evidence-entitlements/upgrade-20261016-123039-063/`. It contains gui-result.json, the exact97-step gui-script.json, all97 screenshots, native log, runner, frozen input copies, and manifest.

| Tower | Native base → upgrade type | Money after build → upgrade |
| --- | --- | --- |
| Plasmatron | 57 → 58 | 2495 → 2485 |
| Kilowatt Laser | 25 → 32 | 2490 → 2470 |
| Railgun | 16 → 7 | 2440 → 2350 |
| Mortar | 52 → 26 | 2490 → 2465 |
| Guided Missile | 49 → 18 | 2475 → 2425 |
| Flak | 9 → 17 | 2450 → 2400 |
| ElectroSlow | 22 → 54 | 2475 → 2450 |
| ElectroShock | 48 → 62 | 2425 → 2325 |
| ElectroBoost | 10 → 47 | 2420 → 2340 |
| Flamethrower | 13 → 29 | 2200 → 1454 |
| Flash Bait | 51 → 36 | 2200 → 2100 |
| Proximity Mine | 28 → 11 | 2490 → 2440 |

## Purchase boundary

Before adaptation, special category displayed3 SHOP locks and all3 original selectors defaultedfalse in the adapter. A click on lockedFlamethrower correctly failed the driver's build assertion; that native3 run is retained in evidence-assert. A separate9-tower+locked-special observation run completed native0 in evidence-final. These earlier runs do not count as complete12-tower coverage.

Focused read-only Ghidra export proves the3 flags are StoreManager purchase entitlements, not earned GameAccount progress. `entitlements-store/entitlements.c` records:

- PIEMENU_isLocked(ELF0x260b53) calls STORE_hasFlamethrower/Flashbait/ProximityMine based on native tower flags.
- STORE_hasFlamethrower(ELF0x2b7185), STORE_hasFlashbait(0x2b7216), STORE_hasProximityMine(0x2b72a7) dispatch to OBJC_CLASS_$_StoreManager with exactly unlockedFlamethrower,unlockedFlashbait,unlockedProximityMine.
- PIEMENU_handleTap's locked branch calls corresponding STORE_show* functions; the real wheel visibly labels those options SHOP.

The isolated host minimally adds those3 exact selectors to its existing shipped-content set, retaining the StoreManager receiver requirement. It writes no account keys and changes no campaign unlock flag. This follows the project's explicit exclusion of purchase validation. After adaptation the original special wheel shows actual models/prices, and all3 complete real GUI construction/upgrading/sale. Special options, Flash Bait upgrade, and Mine upgrade screenshots were visually inspected.

## Frozen inputs and integration

EXE SHA256: CCAE951714C8CC61B1E784FA1B31E5310E5C1EE5ABAECDB4DC07BAD3C7006B3F.

host.cpp SHA256: F43B33703871F1D674715AC2939264F8EC42687CBABED40FDDD88F1C12B42216.

all-clicks-confirm.json SHA256: 5FC2DE23EBBEEAE4A7CABA1829D836AB855558F0E8DAF2BFE1B274D1D27306E1.

Other input hashes: gui/build-entitlements/build-manifest.json. Main tools/CMake were not edited.

Merge helper: gui/gui-weapons-probe.inc. It reads TOWERMADNESS_GUI_SCRIPT, freezes the JSON beside evidence, dispatches clicks, records read-only original state, and checks declared expectations. Call after originalScene installs activeGame and hostMenu slots; a dedicated guiweapons:Peach operation is recommended. Pass gui/all-clicks-confirm.json. Existing isolated host calls it for upgrade:Peach. The separate platform fix is the3-selector set addition described above.

This covers all4 original wheel categories and their12 base/first-upgrade choices, plus default sale confirmation. It does not cover every higher upgrade, nuke GUI chain, combat effects/targeting/damage, special detonation or support influence. The preceding API report independently covers the legal full mortar→Nuke→H Bomb chain. Root owns final candidate retest and centralized MEMORY.md maintenance.
