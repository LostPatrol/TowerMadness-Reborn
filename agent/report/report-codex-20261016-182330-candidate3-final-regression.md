<!-- Fresh final-candidate speed, restore, and complete campaign regression evidence. -->
# Candidate3 final regression

All five requested fresh runs passed the existing `tools/x86-reuse/run.ps1` acceptance on one frozen executable, SHA256 `618E8CD65A49580F06224027A2FE12580E040F8F7A21AE88C149E7171A04FB48`: speed at 60 FPS, speed at 120 FPS, menuauto, same-state cross-process continue, and complete campaignall at default 480×800 / 60 FPS. All native exit codes were 0; combined reported GL errors were 0. No production source, original assets, user state, system volume or user process was changed. All runs were silent and used isolated state.

Machine-readable final evidence: `agent/codex/fps-resize-20261016/regression/final-summary-20261016-180515.json`. Its five-run count excludes all historical runs and earlier standalone FPS probes. Bound source SHA256: `6E05E21E47D83906A606A0954A21A1F938A9EFA3B1888546D42C83545E6C4184`; acceptance runner SHA256: `C4E517391277473CC450DB2AEB5420665B653754BF7093F4EBF300D8C19129E1`.

At 60 FPS, the original 1×/2×/4×/1× controls measured 1/2/4/1 simulation/host ratios. At 120 FPS they measured 0.989446/2/4/1; the first 120 FPS sample contained 3000 simulation ms over 3032 host ms. Normal menu pause froze at 34475 ms. The original 6076-byte save/restore agreed at 34950 ms, then advanced to 39925 ms; the separate continue process loaded the same 34950 ms and advanced to 73075 ms. Full bounded-core details are in the separate `report-codex-20261016-181030-candidate3-core-regression.md`.

The campaign script completed naturally within its original 30-minute bound, after approximately 16 minutes 41 seconds of measured host-loop time. It was not terminated at the four-minute observation boundary. All victories, settlement transitions and account unlocks used the existing original game path.

| Original map | Sheep | Final score | Simulation ms |
| --- | --- | --- | --- |
| PlainPlane | 10 | 1564 | 429650 |
| DividedAttention | 10 | 1939 | 688450 |
| MuttonsFolly | 10 | 3483 | 871750 |
| Madness | 8 | 4933 | 1248475 |

Madness combat score was 4573. `unlocked_medium`, `unlocked_hard`, `unlocked_insane`, and `unlocked_endofgame` were all 1. The second-map real F5/F9 restore and subsequent clock advance were required by campaign acceptance and occurred before the later victories.

The full hidden campaign loop recorded 59949 paced frames at cumulative 59.9031 FPS with the default 60 FPS cap. The final 3.90-second window averaged 60.0044 FPS, p95 16.9342 ms, p99 17.0117 ms, maximum 17.1066 ms, and zero frames above 50 ms. These measurements include real combat; they remain host-loop rates rather than monitor/compositor presentation counts. The earlier separate candidate3 120/unlimited probes establish those options in the menu/tutorial, not an uncapped complete campaign.

Campaign evidence directory: `regression/campaign-20261016-180515/campaign/evidence/menuauto-20261016-180515-097`. Core evidence directory: `regression/core-20261016-180515`.

No commit was created. Final MEMORY/release-baseline updates remain with the parent task.
