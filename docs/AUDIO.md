<!-- Optional original Vorbis audio, muted by default, with offline validation. -->
# Audio

`src/audio.cpp` reads the original Ogg assets through pinned `stb_vorbis`. SDL3 performs format conversion and playback. Audio errors appear through `Audio::warning()` and do not stop gameplay.

The manager begins with both gains at zero and never initializes SDL audio from construction, muted events, or offline verification. The game must apply positive gains only after the user changes the volume controls. Setting both gains to zero closes the manager's streams and audio subsystem. Saved positive values must not be applied automatically at startup.

`play("menu")`, `play("build")`, and `play("game")` select the original looping music. `game` is the initial selection. Other names resolve the original `file` entries in `assets/data/game.sound.json` and `ui.sound.json`; the original disabled `comment_file` entries remain silent. Effects retain their original sample rates and have at most 32 active voices. Flamethrower calls are finite bursts; tower lifecycle loops are not inferred from shot events.

Call `update()` each rendered frame. Music keeps compressed bytes plus a decoder and a small PCM chunk; pending PCM is bounded to half a second plus one decode chunk. Music does not decode whole tracks into memory during playback. Muting one category stops its existing streams immediately.

`verifyAll(std::ostream&)` decodes every packaged Ogg, checks nonempty finite PCM, and requires all 36 files. It never initializes SDL audio or opens a device. Device playback has not been tested because the user explicitly prohibited opening computer speakers. The muted API and all 36 offline decodes were verified on Windows x64.
