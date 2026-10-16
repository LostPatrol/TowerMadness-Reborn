<!-- Unsuccessful legal GUI combat-funding exploration, retaining honest terminal coverage limits. -->
# Legal GUI earned-funds exploration

Two original-GUI combat attempts did **not** earn enough money to construct Tesla Coil before defeat. No additional terminal tower is accepted. Fusion Cannon, Railgun VI, Tesla Coil and Flamethrower III remain outside successful GUI construction coverage; the prior 59/63 construction result is unchanged.

Both attempts used the frozen promoted executable SHA256 `677A091E1D35BA3124521BB43C6482A1971090AAE9730EEBD95E1BCBC951AC1A`, unmodified Peach/Insane original resources, original 2500 starting money, original click-only ElectroShock I→II→III→IV→Doom Capacitor chain costing650, and original GUI speed/wave buttons at (460,56)/(455,774). Actual screenshots confirm the original 4× indicator and progressing original waves. No direct build/upgrade/sell export or money/life/speed/unlock field write was used.

| Attempt | Original combat observations | Result |
| --- | --- | --- |
| Existing display position, cell(12,10) | Doom completed at1850/score0/lives10; after second sampled wave:1870/score20/lives7; after third:lives0 | Native3, GL0, strict active acceptance failed |
| New legal position nearer the initial entry, selected with original click(120,200) | Doom completed at1850/score0/lives10; first sampled wave1870/score20/lives10; second1890/score40/lives10; third:lives0 | Native3, GL0, strict active acceptance failed |

Each actual money increase matched actual score gain from combat:20 and40 respectively. Neither reached the2000 required for Tesla Coil. They were defeats, not map clears, and the requested final Tesla type38 was never accepted as built. Subsequent scheduled clicks found the defeated game and failed the original expected-menu assertion; no failure was reclassified as success.

Evidence, all under `agent/codex/x86-continue-20261016/gui-upgrades-final26/earned/`:

- `tesla/runs/20261016-143106-025/`: first strict failed run, script SHA256 `694964D9C109334F751B771018E564A157ADB29A0C5E18A39B2834FA062036E7`.
- `tesla-near-entry/runs/20261016-143603-402/`: second strict failed run, script SHA256 `C979607F0EB1854A852AA8935E61870BC09D8C10C14B1779E95C0EDFF692B369`.
- `placement/runs/20261016-143453-839/`: four-step exploratory positioning failure because the original camera moved the wheel to a different screen center. The screenshot was inspected and subsequent new-position clicks were shifted by the observed(-64,-17) pixels. No gameplay correction was applied.
- `active-verifier-experimental.ps1`: process-only copy of the temporary opt-in active-game acceptance logic; each run also retains its actual copied runner, original screenshots, native log, config, per-step state and failed acceptance JSON.

As instructed, stopped after the two simple strategies failed rather than expanding into a complex combat strategy. The temporary `-AllowActiveGame` addition was removed from `tools/x86-reuse/verify-upgrade-gui.ps1` using apply_patch after checking that its entire diff consisted solely of this agent's temporary addition. `git diff --exit-code -- tools/x86-reuse/verify-upgrade-gui.ps1` returned0, confirming it exactly matches committed HEAD again. No experimental active-case config was added to tools or tracked production files. Default 157-step/pre-wave and Nuke19 behavior is unchanged.

No host/EXE/gameplay/resource/shared documentation/MEMORY/user save/system audio change, staging or commit was performed. All game processes were started Hidden with audio disabled. Work concluded at14:38, before the requested14:50 cutoff.
