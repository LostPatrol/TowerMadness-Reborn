<!-- Independent final-candidate presentation and original-clock measurements. -->
# Candidate3 FPS verification

Executable: `agent/codex/fps-resize-20261016/candidate3/Release/TowerMadnessX86Probe.exe`.

SHA256: `618E8CD65A49580F06224027A2FE12580E040F8F7A21AE88C149E7171A04FB48`, checked before and after the complete series.

Three independent silent, hidden, default 480×800 `menuauto:campaign` runs used isolated state/log/CSV directories. Each ran approximately 12 seconds and was closed through WM_CLOSE addressed only to that probe's TowerMadness window. All three exited with code 0 and flushed their final partial frame-statistics window. No project source or user process was changed. Process scripts remain under `agent/codex/fps-resize-20261016/frame-analysis`.

| Target FPS | Complete five-second window FPS | Final partial-window FPS | Cumulative FPS | Long frames >50 ms |
| --- | --- | --- | --- | --- |
| 60 | 59.3961, 59.7987 | 60.0089 | 59.6543 | 0 |
| 120 | 117.590, 119.397 | 120.005 | 118.706 | 0 |
| Unlimited | 9474.52, 9697.15 | 9800.77 | 9615.29 | 0 |

All runs had zero HOST_ERROR/NATIVE_FAULT/GL_ERROR log entries. The two complete windows include original menu/map/game transitions, resource loads and screenshot captures. The final partial windows cover 1.59–1.64 seconds, so they are shorter samples.

For every cap, the original GAME simulation clock advanced 100→5100 ms between consecutive five-second campaign heartbeats. Render-frame differences over those same heartbeat intervals were 299, 597 and 48398 respectively. Candidate3's `nativeRealtime()` remains the same GetTickCount64-based implementation as the frozen old current source. Thus the measurements support rendering-speed independence; they do not replace the original 1×/2×/4× button acceptance.

Evidence: `agent/codex/fps-resize-20261016/frame-analysis/candidate3-result-20261016-175521.json`, with per-cap `evidence/run/candidate.log` and `frames.csv` paths in the result.

These are hidden native host-loop rates in the menu/first-level tutorial. They do not establish visible monitor presentation rates, heavy battle performance, or a strict ≥60 FPS guarantee for a 60 FPS cap. The 120 FPS and unlimited options provide headroom for the user's requested higher frame rate.

No commit was created; final project MEMORY and release-baseline maintenance remains with the parent task.
