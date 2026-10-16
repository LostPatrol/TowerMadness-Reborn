<!-- Frozen original-x86 GUI coverage for all non-Mortar weapon upgrade families. -->
# Original GUI high-tier upgrade acceptance

Final tool configuration passed 157/157 original Windows-click steps on promoted `current`, executable SHA256 `677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`. Native exit 0, GL diagnostic errors 0, explicit `GUI_WEAPONS_DONE steps=157`. The complete run retains 157 original captures and per-step state/type/build-completion assertions. Every family was sold through two original GUI confirmation clicks and returned to 2500 money with no tower remaining. All recorded states retained lives 10, score 0, gameStarted false, ended false, won false.

Final evidence: `agent/codex/x86-continue-20261016/gui-upgrades-final26/runs/20261016-141854-518/`. Config SHA256 `566B71221315A5AEC34515D2FDF879D3BB2069092DF3C8B076EE1A6CDDBA555E`; build manifest SHA256 `B857A74D059EE6054D1A069AE36F0FD0BEA8B236BB7F196AFF58E6702B71DFBA`. `highest-towers-contact-sheet.png` was visually inspected and shows the original rendered highest legally purchased tower for all eleven families, with matching displayed balance and life values.

## Coverage and actual limits

Peach.gamemode selects **Insane.tech** with original starting money 2500. Final expectations derive from the shipped Insane catalog, SHA256 `E4642E038A4C2C34CA25B600A80E658DCB729B1C8BEA4679612E17FF12FEB07F`. Initial exploration read Easy.tech; a complete comparison confirmed all 63 entries have identical order/name, cost and upgrade edges between Easy and Insane. The final generator reads Insane explicitly; original runtime type/money assertions remain the acceptance evidence.

| Family | Successfully completed original type chain | Final purchased balance | Terminal coverage |
| --- | --- | ---: | --- |
| Plasmatron | 57→58→12→14→56→21 | 2145 | Gauss Cannon; Fusion Cannon rejected for insufficient money |
| Laser | 25→32→5→19→35 | 2031 | Complete through Gatling Laser |
| Railgun | 16→7→39→8→44 | 1515 | Railgun V; Railgun VI rejected for insufficient money |
| Guided Missile | 49→18→46→45→61→34 | 1125 | Complete through Energy Pulse Missile |
| Flak | 9→17→41→23→6→31 | 1025 | Complete through Donnergrollen |
| ElectroSlow | 22→54→20→59→0 | 2275 | Complete through Muscle Melter |
| ElectroShock | 48→62→4→50→60 | 1850 | Doom Capacitor; Tesla Coil rejected for insufficient money |
| ElectroBoost | 10→47→15→40→3→55 | 1600 | Complete through ElectroBoost VI |
| Flamethrower | 13→29 | 1454 | Flamethrower II; Flamethrower III rejected for insufficient money |
| Flash Bait | 51→36→30 | 1950 | Complete through Flash Bait III |
| Proximity Mine | 28→11→53 | 2190 | Complete through Proximity Mine III |

Seven of these eleven families reached their catalog terminal tier. Four terminal tiers remain **unconstructed in this pre-wave acceptance**, because cumulative legal costs exceed the original 2500: Fusion Cannon 2855, Railgun VI 3235, Tesla Coil 2650, Flamethrower III 3000. Their actual original upgrade buttons were clicked; each preserved the prior tower, completed-build state and balance while retaining the open wheel. No money was added and no expectation was weakened to count a terminal tower as constructed.

This run legally built 52 distinct catalog types and completed 41 upgrades across eleven families; 30 purchased tiers extend beyond their previously covered base/first-upgrade pair. Combined with the independently passed Mortar→Nuke→H Bomb chain (7 types), original GUI construction now covers **59 of 63 catalog types**, with the other four explicitly limited to original insufficient-funds rejection. This does not claim combat behavior, launch effects, or successful construction of those four unaffordable types.

## Reusable tools and retained failures

Added `tools/x86-reuse/upgrade-clicks.json` and the common `verify-upgrade-gui.ps1`. The common verifier accepts project-relative or absolute executable, output and click-script paths; binds optional strict executable SHA256; verifies available build-manifest executable/input hashes; freezes the actual click JSON/runner/manifest; and checks actual process completion, original pre-wave player state, config/executable stability and GL diagnostics.

`verify-nuke-gui.ps1` is now an eight-line parameter-forwarding wrapper over the common verifier, preserving its default executable/output and optional expected-hash parameters and using the unchanged original `nuke-clicks.json`. After consolidation, its actual 19-step run also passed native 0 / GL 0 at `agent/codex/x86-continue-20261016/gui-upgrades-final26/nuke-wrapper/runs/20261016-142114-826/`. Its config hash remains `75608D276B9D1080340260C7D65840BC916C31BFAE26CE1A2145D4D68F34AD58`.

Final 157-step command, from project root:

```powershell
& tools/x86-reuse/verify-upgrade-gui.ps1 -OutputDirectory 'agent/codex/x86-continue-20261016/gui-upgrades-final26' -ExpectedExecutableSha256 677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A
```

Retained experimental failure: `runs/20261016-141426-000/` ended native 3 after 17 recorded steps because the test clicked the selected tower after an unaffordable Fusion upgrade. The original game had correctly left its wheel open; this extra test click closed it, contradicting `expectPie=0`. The driver was corrected to keep the existing open wheel before the original sale confirmations. This was a test-flow error, not a gameplay defect. Corrected experimental run `runs/20261016-141543-521/` passed all 157 steps before freezing the final tool config.

No host, gameplay source, original package/catalog/asset, existing original click assertion, user save, shared documentation, MEMORY or system audio setting was modified. All runs used native audio disabled and Hidden startup. No source patch, money/life/unlock write, direct build/upgrade/sell API call, commit or staging was performed by this agent. Central documentation and MEMORY updates remain with the parent agent.
