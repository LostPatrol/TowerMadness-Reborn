<!-- Independent pacing implementation and measurement evidence for the parent task. -->
# Native frame pacing

Implemented `tools/x86-reuse/frame-pacing.hpp`; the parent agent owns host/build integration and final delivery. No original ELF/assets, simulation clock, system timer resolution, volume setting, or user process was changed by this subtask.

The header provides default 60 FPS pacing, `TOWERMADNESS_FPS=0` for unlimited, arbitrary nonnegative caps, and runtime `configure(int)`. It uses absolute QPC deadlines with a process-owned high-resolution waitable timer, ordinary timer/Sleep-yield fallback, five-second frame statistics, final partial-window reporting, and optional `TOWERMADNESS_FRAME_TRACE` CSV. Percentiles cover the reported window; counts and average also include cumulative values. This is host-loop timing, not display/compositor measurement.

Win32 MSVC C++17 `/O2 /W4` compilation succeeded. A 1 ms QPC workload measured 59.75 / 118.48 / 890.68 FPS for targets 60 / 120 / unlimited. Evidence: `agent/codex/fps-resize-20261016/frame-analysis/pacer-check.log`. This harness does not measure the game.

The unchanged old EXE, SHA256 `677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`, measured approximately 31.82 FPS over 636 frames / 19.99 seconds using its existing campaign frame heartbeat. Its loop unconditionally sleeps 16 ms after rendering and swapping. The measurement polled at about 30 ms resolution. Evidence: `frame-analysis/baseline/result.json`. The initial shutdown helper also addressed this probe's console window, so this run does not establish graceful final flushing; the complete sampled heartbeat intervals remain valid. The user's process was not addressed.

Candidate1 SHA256 `27AEDAEB9F05B206FBCDAC68EDE799A163148A65FDD943E9765C63E5F35DDF44` was measured in independent silent hidden-menu campaign probes. The corrected helper closed only each probe's TowerMadness window; both exited with code 0 and flushed their final statistics. Evidence: `frame-analysis/candidate1-result-20261016-175054.json`.

| Target | Complete five-second windows | Cumulative FPS | Long frames > 50 ms |
| --- | --- | --- | --- |
| 60 | 59.396, 59.799 | 59.655 | 0 |
| Unlimited | 8133.79, 6561.87 | 7418.86 | 0 |

The unlimited run's original simulation clock advanced 100→5100 ms between its two five-second heartbeats despite tens of thousands of render iterations. This supports clock independence, but does not replace the parent task's original speed-control acceptance. These hidden menu/tutorial samples do not establish visible presentation rate or busy battle performance. A 60 FPS cap cannot guarantee an average at least 60 under scheduling/loading delays; higher caps or unlimited permit headroom.

Earlier candidate trials under heavier concurrent load measured lower rates and are preserved in `frame-analysis/candidate1-result.json`. Their closing helper truncated final buffered output; only complete five-second statistics windows are usable. Do not present those trials as normal-exit acceptance.

No commit was created. Shared MEMORY maintenance remains with the parent agent to preserve one current baseline after integration.
