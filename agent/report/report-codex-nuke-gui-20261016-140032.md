<!-- Final-candidate original GUI acceptance for the complete Mortar-to-H-Bomb chain. -->
# Original x86 Nuke GUI acceptance

Passed all 19 scripted steps on the frozen candidate26 executable, SHA256 `677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`. Native exit 0; observed GL error count 0; explicit `GUI_WEAPONS_DONE steps=19`. The script SHA256 is `75608D276B9D1080340260C7D65840BC916C31BFAE26CE1A2145D4D68F34AD58`.

The original Windows mouse-message path opened the category wheel, selected Mortar, built Mortar I, then reopened the original tower wheel for each upgrade. No build/upgrade/sell export was called by the script. Each stage asserted the completed original tower type and balance:

| Original stage | Type | Money |
| --- | ---: | ---: |
| Opening | none | 2500 |
| Mortar I | 52 | 2490 |
| Mortar II | 26 | 2465 |
| Mortar III | 37 | 2412 |
| Mortar IV | 43 | 2281 |
| Mortar V | 24 | 2062 |
| Nuke | 1 | 1537 |
| H Bomb | 27 | 37 |
| First sell confirmation | 27 | 37 |
| Second sell confirmation | none | 2500 |

All recorded states retained lives 10, score 0 and gameStarted false. All original upgrade construction states completed. Visually inspected `gui-Nuke-upgraded.png` and `gui-H-Bomb-upgraded.png`: both show the original tower on Peach, with displayed balances 1537 and 37 respectively. This acceptance covers the legal GUI upgrade and full pre-wave refund; it does not claim nuclear combat/launch effects or every alternative upgrade branch.

Evidence directory: `agent/codex/x86-continue-20261016/nuke-gui-final26/runs/20261016-135954-817/`. Contains the frozen input clicks, copied runner, native/stdout/stderr logs, all 19 PNGs, per-step `gui-result.json`, and `acceptance.json`. Independent state resolves to `agent/codex/x86-continue-20261016/nuke-gui-final26/state`. The process used `TOWERMADNESS_X86_AUDIO=0` and Hidden startup; system audio settings were untouched.

Added reusable `tools/x86-reuse/nuke-clicks.json` and `verify-nuke-gui.ps1`. The wrapper defaults to `agent/codex/x86-reuse/current/Release/TowerMadnessX86Probe.exe` and `agent/codex/x86-reuse/nuke-gui`, accepts absolute or project-relative paths, verifies the selected build's manifest executable/input hashes when present, and accepts an optional strict `ExpectedExecutableSha256`. It freezes config and runner, copies the build manifest, records actual executable/config/manifest hashes, rechecks executable/config after exit, and independently checks completion, GL diagnostics, evidence count, and unchanged pre-wave player state.

The reusable wrapper was rerun successfully against promoted `current` with absolute path overrides and the same strict `677A…` executable hash: another 19/19 steps, native exit 0, GL errors 0. Evidence: `agent/codex/x86-continue-20261016/nuke-gui-final26/runs/20261016-140305-317/`. Manifest SHA256 `B857A74D059EE6054D1A069AE36F0FD0BEA8B236BB7F196AFF58E6702B71DFBA`; config hash remained unchanged. Exact command:

```powershell
& tools/x86-reuse/verify-nuke-gui.ps1 -Executable 'E:\TowerMadness-Reborn\agent\codex\x86-reuse\current\Release\TowerMadnessX86Probe.exe' -OutputDirectory 'E:\TowerMadness-Reborn\agent\codex\x86-continue-20261016\nuke-gui-final26' -ExpectedExecutableSha256 677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A
```

For ordinary current-build reuse, run `& tools/x86-reuse/verify-nuke-gui.ps1` from the project root.

No host/main source, existing agent file, shared documentation, MEMORY, APK, ELF, asset, user save, system setting, money/life/unlock field was modified. No task metric failed. No commit was made. Central MEMORY maintenance remains with the parent agent as requested.
