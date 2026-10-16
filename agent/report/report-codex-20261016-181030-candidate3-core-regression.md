<!-- Fresh behavioral acceptance on the final candidate, without source changes. -->
# Candidate3 core regression

All four newly requested bounded runs passed the existing `tools/x86-reuse/run.ps1` acceptance: speed at 60 FPS, speed at 120 FPS, normal menuauto, then cross-process continue using the same isolated output/state. Every run exited 0, reported GL error count 0, and used the frozen candidate3 executable SHA256 `618E8CD65A49580F06224027A2FE12580E040F8F7A21AE88C149E7171A04FB48`. Source hash was `6E05E21E47D83906A606A0954A21A1F938A9EFA3B1888546D42C83545E6C4184`; runner hash was `C4E517391277473CC450DB2AEB5420665B653754BF7093F4EBF300D8C19129E1`.

The original speed buttons measured simulation/host ratios 1/2/4/1 at the 60 FPS cap and 0.989446/2/4/1 at 120. The 120 FPS run's first sample measured 3000 simulation ms over 3032 host ms; all other windows measured the exact intended ratios.

The menu test paused the original simulation at 34475 ms and observed the same value at both 1500 and 3000 ms into the pause. Original Resume returned showing=0. The original 6076-byte save and IBS restore both recorded 34950 simulation ms, after which the first process advanced to 39925 ms. A separate process read the same original 34950 ms checkpoint and advanced to 73075 ms.

Fresh evidence only: `agent/codex/fps-resize-20261016/regression/core-20261016-180515`; machine-readable summary: `regression/core-summary-20261016-180515.json`. This count excludes prior candidate1/candidate3 FPS probes and historical baseline runs.

The separately requested campaignall run is still active under its original scripted 30-minute duration. At the approximately five-minute observation it had naturally won PlainPlane with 10 sheep and final score 1564, followed the original settlement/second-map chain, and completed a real second-map save/restore with clock advance. DividedAttention was still in progress with 10 sheep. This report does not claim full campaignall acceptance; its eventual result belongs in a separate report. The process was not terminated at the four-minute observation boundary.

No production source, original game assets, user state, system settings or user process was changed. All probes were silent and used isolated state. No commit was created.
