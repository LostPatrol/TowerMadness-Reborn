<!-- Ordinary original UI tower identity, price deductions, and corrected screenshot labels. -->
# Original tower identity

API17 ARM original APK ordinary UI verification, 2026-10-13. The APK, money, sheep, waves and services were not modified. The unique target window was `TM_Original_API17_ARM_clean:5592`.

## Confirmed identity and prices

| Tower | Ordinary build / upgrade funds | Visible upgrade price | Identity evidence |
| --- | --- | --- | --- |
| Mortar I → Mortar II | 50 → 40 → 15 | 25 | Green military cannon; original tutorial explicitly names Mortar; matches old build-choice model |
| Kilowatt Laser → Megawatt Laser | 40 → 30 → 10 | 20 | Gold disc and barrel; original tutorial names Laser; energy submenu top option and upgraded gold model |

The Laser run began at 50, first built an unupgraded Mortar for 10, then built Laser for 10 and upgraded it for 20. No wave was started; score remained 0 and sheep remained 10. A normal sell preview was cancelled, so no refund contributed to this sequence.

The initial family wheel animates with camera zoom. Wait for the stable wheel before selecting it. Its upper green icon opens ballistic towers (Mortar is the right available option), right orange icon opens energy towers (Laser is the top option and 5-coin Plasmatron is the right option), and left star opens shop-only special towers. The tutorial banner obscures the top energy option's price; the actual build deduction confirms 10.

## Evidence

Evidence directory: `agent/codex/p8-original-tower-identity/`.

- `tutorial-mortar-level1.png`: explicit Mortar name and military cannon model.
- `mortar-selected-upgrade25-money40.png`, `mortar-upgraded-money15.png`: ordinary Mortar upgrade sequence.
- `tutorial-laser-name-model.png`: explicit Laser name and disc-shaped model.
- `energy-laser-top-plasma-right.png`: energy submenu with Laser at top and Plasmatron at right.
- `actual-laser-built-money30.png`, `actual-laser-selected-upgrade20-money30.png`, `actual-laser-upgraded-money10.png`: actual Laser model, upgrade wheel and deduction.

The pre-existing `runtime/logs/TM_Original_API17_ARM_clean/plainplane-build-choice.png` shows the same green military Mortar model. Its `plainplane-selected-range.png` shows a green military upgrade model and 25-coin upgrade. These must be identified as Mortar, not Laser. Any claimed Laser firing evidence derived from that selected tower remains unverified.

Some early filenames in the new evidence directory were assigned before identity was established: `laser-build10-model.png`, `laser-built-money40.png`, `laser-family-wheel.png`, `laser-family-wheel-stable.png` depict the second run's Mortar or family selection, not a built Laser. Use the `actual-laser-*` sequence for Laser verification.
