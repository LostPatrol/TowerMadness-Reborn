<!-- Project purpose, player guide, and local development for the Windows port. -->
# TowerMadness Reborn

[English](README.md) | [简体中文](README.zh-CN.md)

This is an unofficial personal nostalgia project, made to replay a childhood favorite, **TowerMadness Zero**, on Windows. It preserves the original game code, assets, and gameplay in a native Windows host, without an Android or CPU emulator.

**Please respect the original game's copyright.** TowerMadness and its original code, artwork, music, and other content belong to Limbic Software and their respective rights holders. Original-game content made available in this repository is provided solely to facilitate learning and the sharing and discussion of programming knowledge. **Please delete downloaded original-game content within 24 hours of downloading it.** This project is not affiliated with or endorsed by the original creators.

## For players

### Start playing

On Windows 10/11, extract the complete portable package into a writable folder and double-click **TowerMadness.exe**. Keep the `runtime` folder beside the executable. Do not run the game inside the ZIP preview; development tools are not needed to play.

Choose **New Game**, select a map, and press **Enter**. Click an empty cell to open the tower wheel; click an existing tower to upgrade or sell it. Protect the sheep from the invading enemies.

Use the arrow keys, A/D, or the mouse wheel to browse maps. Wheel up selects the previous map; wheel down selects the next. **Map overview** provides thumbnails and filters.

### Default controls

| Input | Action |
| --- | --- |
| Left click | Select menus, build, upgrade, or sell through the tower wheel |
| Drag / mouse wheel | Pan / zoom the camera |
| Right click | Close the tower wheel |
| Space / Esc | Pause or resume / return to the menu |
| N | Send the next wave early |
| W | Upgrade the selected tower; choose a branch in the wheel when needed |
| Q, E / middle-button drag | Rotate the camera |
| R | Reset the camera view |
| F5 / F9 | Save / restore a checkpoint; restoring replaces the current game |
| F1 / F10 | Open the field guide / PC settings |
| F11 or Alt+Enter | Toggle borderless fullscreen |
| F6 | Switch between 60 FPS, 120 FPS, and uncapped |

**PC SETTINGS** offers configurable keys, camera sensitivity, and UI size. Window and PC settings are remembered. Adjust music and sound effects in the original **Options** menu. The default frame cap is 60 FPS.

### Saves and troubleshooting

- Press **F5** before leaving; select **Resume** after restarting to continue. Manual checkpoints retain five older backups.
- Closing the window during a game offers **Yes** to save and exit, **No** to exit without saving, and **Cancel** to return.
- Separate autosave is off by default. Enable it in **PC SETTINGS** to save every two minutes; **Restore separate autosave** restores it without overwriting the manual checkpoint.
- Progress and settings live in `data`. To migrate, close both game copies, back up their data, then copy the old `data` folder into the new package. Avoid overwriting newer progress.
- If startup fails, check that the package is fully extracted and its folder is writable. For bug reports, include the map, steps, and a screenshot. Preserve `logs/game.log` before restarting, or use **PC SETTINGS → Export diagnostics**. Back up saves separately.

Online leaderboards and Game Center are unavailable; achievements show local campaign progress. Initial resource loading and checkpoint restoration may briefly pause the game.

## For developers

### Local setup and build

The maintained code is in `tools/x86-reuse/`: a C++20 Win32 host loads the original Android x86 ELF libraries and adapts platform calls, OpenGL, input, audio, and storage. Use a **Win32/x86** build, including on 64-bit Windows.

You need Visual Studio's Desktop development with C++ workload, a Windows SDK, CMake, and Node.js. Review the machine-specific CMake and Node paths in `build.ps1` and `run.ps1` before using them on another PC.

Original APKs, extracted libraries, local dependencies, and generated runtime metadata are excluded from Git. **Cloning this repository alone is not enough to build and run the game.** Supply your own original x86 APK (`Madness_Zero-1.22.apk`). Initial GS metadata preparation also needs a local Capstone 5 x64 DLL: `prepare-gs.ps1` uses `agent/codex/x86-reuse/capstone.dll`, or extracts it from the Ghidra wheel at the path configured in that script. REA and a Ghidra analysis database are not required for normal builds.

Run from the repository root in PowerShell after checking those local paths:

```powershell
# Download the pinned development headers and audio decoder.
& .\tools\setup-dependencies.ps1
# Extract your x86 APK and generate GS metadata using the local Capstone DLL.
& .\tools\x86-reuse\prepare.ps1 -Apk .\Madness_Zero-1.22.apk
# Prepare textures, then generate OpenGL metadata and build the Win32 host.
node .\tools\prepare-assets.mjs
& .\tools\x86-reuse\build.ps1
& .\tools\x86-reuse\run.ps1 -Mode menu
```

Once OpenGL metadata exists, `build.ps1 -SkipGlSignatures` skips regenerating it. The default build directory is `agent/codex/x86-reuse/current`; `run.ps1 -Mode menu -Silent` runs without audio. `tools/x86-reuse/play.cmd` starts an existing development build.

### Development and packaging

`host.cpp` implements the native loader and platform bridge. The `desktop-*.hpp` files handle desktop controls, menus, settings, saves, and the field guide; `native-audio.hpp` and `original-audio.hpp` handle Windows audio. `assets/textures/` and the shared resource preparation script remain part of the maintained route. Keep new build dependencies in the build manifest and relevant validation scripts.

Create a new local portable directory with:

```powershell
& .\tools\x86-reuse\package.ps1
```

The output goes under `release/` and includes the executable, runtime files, and player instructions. Local research, agent files, and the retired reconstruction route are excluded from the repository.
