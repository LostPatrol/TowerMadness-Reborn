// Adapt the original OpenAL boundary to native Windows voices and original Vorbis assets.
// The game retains its sound registry, source pools, effect settings and source selection.
// Music preserves per-tune positions with clock-timed fades; effects retain original pools.
#pragma once
#include "native-audio.hpp"

namespace original_audio {
// One game thread owns all platform audio state; XAudio2 owns only mixer execution.
inline NativeAudio backend;
inline bool enabled = false;
inline std::filesystem::path assets;
inline void (*trace)(const std::string&) = nullptr;
inline std::map<unsigned, int> buffers;
struct Source {
    int voice = -1;
    unsigned buffer = 0;
    bool loop = false;
    float gain = 1, x = 0, y = 0, z = 0, rolloff = 1;
};
// Source and buffer handles are distinct, as in the original OpenAL contract.
inline std::map<unsigned, Source> sources;
inline unsigned nextSource = 1, nextBuffer = 1;
inline int deviceToken = 0, contextToken = 0;
inline unsigned effectStarts = 0, musicStarts = 0;
inline std::string lastWarning;

/** Forward optional audio failures without stopping the original game. */
inline void warnings() {
    if (trace && !backend.warning().empty() && backend.warning() != lastWarning) {
        lastWarning = backend.warning();
        trace("AUDIO_WARNING " + lastWarning);
    }
}

/** Resolve original resource names; the .caf payloads contain Vorbis bytes. */
inline std::filesystem::path resource(const std::string& name) {
    auto path = assets / name;
    if (!path.has_extension()) path += ".caf";
    return path;
}

/** Register host-owned PCM; the guest shutdown therefore owns no PCM pointer. */
inline void* __cdecl loadSound(const char* name, unsigned buffer) {
    if (enabled && name) {
        buffers[buffer] = backend.load(resource(name));
        if (trace) trace("AUDIO_BUFFER " + std::string(name) + " clip=" + std::to_string(buffers[buffer]));
        warnings();
    }
    return nullptr;
}

/** Allocate the original registry's stable buffer handles. */
inline void __cdecl genBuffers(int count, unsigned* values) {
    for (int i = 0; i < count; ++i) { values[i] = nextBuffer++; buffers[values[i]] = -1; }
}

/** Forget guest handles; backend PCM remains cached until process shutdown. */
inline void __cdecl deleteBuffers(int count, const unsigned* values) {
    for (int i = 0; i < count; ++i) buffers.erase(values[i]);
}

/** Allocate source handles for the original 3/3/10/5 voice pools. */
inline void __cdecl genSources(int count, unsigned* values) {
    for (int i = 0; i < count; ++i) { values[i] = nextSource++; sources.emplace(values[i], Source{}); }
}

/** Stop one source while preserving its reusable original handle. */
inline void __cdecl stop(unsigned id) {
    if (auto found = sources.find(id); found != sources.end()) {
        backend.stop(found->second.voice); found->second.voice = -1;
    }
}

/** Release source handles after stopping their native playback. */
inline void __cdecl deleteSources(int count, const unsigned* values) {
    for (int i = 0; i < count; ++i) { stop(values[i]); sources.erase(values[i]); }
}

/** Store original buffer and looping attributes before source playback. */
inline void __cdecl sourceInt(unsigned id, int parameter, int value) {
    auto found = sources.find(id); if (found == sources.end()) return;
    if (parameter == 0x1009) found->second.buffer = static_cast<unsigned>(value); // AL_BUFFER.
    if (parameter == 0x1007) found->second.loop = value != 0; // AL_LOOPING.
}

/** Apply original source gain and rolloff to both pending and active playback. */
inline void __cdecl sourceFloat(unsigned id, int parameter, float value) {
    auto found = sources.find(id); if (found == sources.end()) return;
    auto& source = found->second;
    if (parameter == 0x100a) { source.gain = value; backend.setVoiceGain(source.voice, value); }
    if (parameter == 0x1021) { source.rolloff = value; backend.setVoiceRolloff(source.voice, value); }
    // AL_REFERENCE_DISTANCE is always 10 in the original and matches the backend constant.
}

/** Track original OpenAL coordinates, which already include the original x inversion. */
inline void __cdecl sourcePosition(unsigned id, int parameter, float x, float y, float z) {
    auto found = sources.find(id); if (found == sources.end() || parameter != 0x1004) return;
    auto& source = found->second; source.x = x; source.y = y; source.z = z;
    backend.setVoicePosition(source.voice, x, y, z, source.rolloff);
}

/** Start the source selected by the unchanged original pool allocator. */
inline void __cdecl play(unsigned id) {
    if (trace) trace("AUDIO_SOURCE_PLAY id=" + std::to_string(id));
    auto found = sources.find(id); if (!enabled || found == sources.end()) return;
    auto& source = found->second;
    auto buffer = buffers.find(source.buffer); if (buffer == buffers.end()) return;
    stop(id);
    source.voice = backend.play(buffer->second, source.gain, source.loop, source.x, source.y, source.z, source.rolloff);
    if (source.voice >= 0) {
        ++effectStarts;
        if (trace) trace("AUDIO_EFFECT source=" + std::to_string(id) + " clip=" + std::to_string(buffer->second));
    }
    warnings();
}

/** Report actual mixer completion so original pools can reuse stopped voices. */
inline void __cdecl getSource(unsigned id, int parameter, int* value) {
    if (!value) return;
    auto found = sources.find(id);
    *value = parameter == 0x1010 && found != sources.end() && backend.isPlaying(found->second.voice)
        ? 0x1012 : 0x1014; // AL_SOURCE_STATE: PLAYING or STOPPED.
}

/** Preserve the original effects level and apply its listener gain in Windows. */
inline void __cdecl listenerFloat(int parameter, float value) {
    if (parameter != 0x100a) return; // AL_GAIN.
    backend.setEffectsGain(value);
    if (trace) trace("PLATFORM_EFFECTS gain=" + std::to_string(value));
}

/** Update the listener without altering the original game's coordinate globals. */
inline void __cdecl listenerPosition(int parameter, float x, float y, float z) {
    if (parameter == 0x1004) backend.setListener(x, y, z); // AL_POSITION.
}

/** Return a stable device token; the real device opens lazily at playback. */
inline void* __cdecl openDevice(const char*) { return &deviceToken; }
/** Return a stable single-thread context token. */
inline void* __cdecl createContext(void*, const int*) { return &contextToken; }
/** Accept the original context selection on its one owning game thread. */
inline int __cdecl makeCurrent(void*) { return 1; }
/** Finish the logical device lifetime; backend RAII releases Windows resources. */
inline int __cdecl closeDevice(void*) { return 1; }
/** Accept unsupported platform context operations without invoking libv TLS. */
inline void __cdecl contextOperation(void*) {}
/** Windows owns mixer rate selection; the original helper takes one double. */
inline void __cdecl mixerRate(double) {}
/** The backend implements the original inverse-distance model directly. */
inline void __cdecl distanceModel(int) {}
/** Replace the iOS audio-session initialization boundary. */
inline int __cdecl sessionInit(void*, void*, void*, void*) { return 0; }
/** Replace the iOS category property boundary. */
inline int __cdecl sessionProperty(unsigned, unsigned, const void*) { return 0; }
/** Windows audio remains independently available across game pause/resume. */
inline int __cdecl sessionActive(int) { return 0; }

// MusicManager state belongs to the platform adapter, separate from game progress.
inline std::string tune, pendingTune;
inline std::map<std::string, double> positions;
inline bool tuneLoop = true, pendingLoop = true, musicPlaying = false;
// A failed tune waits for a different request; unrelated effect playback stays available.
inline bool musicFailed = false;
inline float baseVolume = 0.5f, temporaryVolume = 1, fade = 1, fadeTarget = 1;
// A full transition lasts one second regardless of host frame rate or a stalled frame.
inline constexpr ULONGLONG FadeDurationMs = 1000;
// Each retarget starts at its current level, so interrupted transitions remain continuous.
inline float fadeOrigin = 1, appliedFadeTarget = 1;
inline ULONGLONG musicTick = 0;

/** Evaluate an eased envelope without a mixer or wall-clock dependency. */
inline float fadeLevelAt(float origin, float target, ULONGLONG elapsedMs) {
    if (elapsedMs >= FadeDurationMs) return target;
    const float progress = static_cast<float>((std::min)(elapsedMs, FadeDurationMs)) / FadeDurationMs;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    return origin + (target - origin) * eased;
}

/** Sample the current envelope; accept legacy callers that assign fadeTarget directly. */
inline void advanceFade(ULONGLONG now) {
    if (fadeTarget != appliedFadeTarget) {
        fadeOrigin = fade;
        appliedFadeTarget = fadeTarget = std::isfinite(fadeTarget) ? (std::clamp)(fadeTarget, 0.0f, 1.0f) : 0.0f;
        musicTick = now;
    }
    fade = fadeLevelAt(fadeOrigin, appliedFadeTarget, now - musicTick);
}

/** Retarget from the sampled current volume rather than resetting the fade. */
inline void setFadeTarget(float target) {
    const auto now = GetTickCount64();
    advanceFade(now);
    target = std::isfinite(target) ? (std::clamp)(target, 0.0f, 1.0f) : 0.0f;
    if (target == appliedFadeTarget) return;
    fadeOrigin = fade;
    appliedFadeTarget = fadeTarget = target;
    musicTick = now;
}

/** Remember the active tune before releasing its native player. */
inline void rememberMusic() {
    if (musicPlaying && !tune.empty()) positions[tune] = backend.musicPosition();
}

/** Apply mute/unmute and recreate the remembered tune when volume becomes positive. */
inline void refreshMusic() {
    const float volume = fade * baseVolume * temporaryVolume;
    backend.setMusicGain(volume);
    if (volume <= 0 && musicPlaying) {
        rememberMusic(); backend.stopMusic(); musicPlaying = false;
    } else if (volume > 0 && !musicPlaying && !musicFailed && !tune.empty() && enabled) {
        musicPlaying = backend.playMusic(resource(tune), tuneLoop, positions[tune]);
        if (musicPlaying) {
            ++musicStarts;
            if (trace) trace("AUDIO_MUSIC tune=" + tune + " offset=" + std::to_string(positions[tune]));
        }
        // Device/resource failure must not trigger a full music decode every frame.
        if (!musicPlaying) musicFailed = true;
        warnings();
    }
}

/** Select the original requested tune, fading before switching distinct resources. */
inline void selectMusic(const std::string& name, bool loop) {
    if (trace) trace("AUDIO_MUSIC_REQUEST tune=" + name + " path=" + resource(name).string());
    // The original cancels an old pending URL even when the new request is the active tune.
    if (name == tune) { pendingTune.clear(); setFadeTarget(1); refreshMusic(); return; }
    if (tune.empty()) {
        tune = name; tuneLoop = loop; fade = fadeOrigin = 0;
        fadeTarget = appliedFadeTarget = 1; musicTick = GetTickCount64(); musicFailed = false;
    } else { pendingTune = name; pendingLoop = loop; setFadeTarget(0); }
    refreshMusic();
}

/** Advance platform music fading on the host clock and retire finished effect voices. */
inline void update() {
    backend.update();
    const auto now = GetTickCount64();
    advanceFade(now);
    refreshMusic();
    if (fade == 0 && !pendingTune.empty()) {
        tune = pendingTune; pendingTune.clear(); tuneLoop = pendingLoop;
        fadeOrigin = 0; fadeTarget = appliedFadeTarget = 1; musicTick = now; musicFailed = false;
    }
    warnings();
}
}
