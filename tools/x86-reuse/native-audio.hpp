// Native Windows audio for original Ogg/Vorbis assets, including .caf containers.
// Include this implementation header in exactly one translation unit.
// Spatial effects use inverse-distance gain and equal-power stereo panning.
#pragma once

#include <windows.h>
#include <xaudio2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef _MSC_VER
// Upstream decoder can emit this optimizer warning after the warning scope ends.
#pragma warning(disable: 4701)
#pragma warning(push, 0)
#endif
#include "../../third_party/stb_vorbis.c"
// The decoder's channel-table macros collide with Windows wide-string macro expansion.
#undef L
#undef R
#undef C
#ifdef _MSC_VER
#pragma warning(pop)
#endif

class NativeAudio {
    // Bound simultaneous effects; music has its own source voice.
    static constexpr std::size_t MaxVoices = 32;
    // Match the original reference distance without claiming its full spatial model.
    static constexpr float ReferenceDistance = 10.0f;
    // Stack decode scratch space, measured in interleaved samples.
    static constexpr int DecodeSamples = 16384;
    // A stereo mastering bus gives positional effects consistent left/right routing.
    static constexpr UINT32 OutputChannels = 2;
    // Quarter-circle angle used by the equal-power stereo law.
    static constexpr double QuarterPi = 0.7853981633974483;

    struct Clip {
        WAVEFORMATEX format{};
        std::vector<float> pcm;
    };
    struct Voice {
        IXAudio2SourceVoice* source = nullptr;
        int id = -1;
        bool loop = false;
        float gain = 1.0f;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float rolloff = 0.0f;
        WORD channels = 1;
    };

    IXAudio2* engine_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    bool comOwned_ = false;
    bool initializationAttempted_ = false;
    std::vector<std::unique_ptr<Clip>> clips_;
    std::map<std::filesystem::path, int> clipIds_;
    std::vector<Voice> voices_;
    // Music shares the decoded clip cache, avoiding synchronous decode on every switch.
    const Clip* musicClip_ = nullptr;
    IXAudio2SourceVoice* music_ = nullptr;
    bool musicLoop_ = true;
    // Frames skipped at the current music start, for host-owned resume bookkeeping.
    UINT32 musicOffsetFrames_ = 0;
    float effectsGain_ = 1.0f;
    float musicGain_ = 1.0f;
    float listenerX_ = 0.0f, listenerY_ = 0.0f, listenerZ_ = 0.0f;
    int nextVoiceId_ = 1;
    std::string warning_;

    // Keep invalid guest values out of the native mixer.
    static float gain(float value) {
        return std::isfinite(value) ? (std::clamp)(value, 0.0f, XAUDIO2_MAX_VOLUME_LEVEL) : 0.0f;
    }

    // Preserve a readable diagnostic while allowing the game to keep running.
    bool failed(const char* operation, HRESULT result) {
        if (SUCCEEDED(result)) return false;
        warning_ = std::string(operation) + " failed (HRESULT " + std::to_string(static_cast<unsigned long>(result)) + ")";
        return true;
    }

    // Open the default device only when playback is requested.
    bool initialize() {
        if (engine_ && master_) return true;
        if (initializationAttempted_) return false;
        initializationAttempted_ = true;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comOwned_ = SUCCEEDED(com);
        if (FAILED(com) && com != RPC_E_CHANGED_MODE) return !failed("Audio COM initialization", com);
        if (failed("XAudio2 creation", XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
        if (failed("Audio device creation", engine_->CreateMasteringVoice(&master_, OutputChannels))) {
            engine_->Release();
            engine_ = nullptr;
            return false;
        }
        return true;
    }

    // Read Unicode filesystem paths and decode original Vorbis bytes without conversion.
    std::unique_ptr<Clip> decode(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) { warning_ = "Cannot read audio asset"; return nullptr; }
        const auto length = file.tellg();
        if (length <= 0 || length > (std::numeric_limits<int>::max)()) {
            warning_ = "Invalid audio asset size";
            return nullptr;
        }
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            warning_ = "Incomplete audio asset read";
            return nullptr;
        }
        int error = 0;
        auto* decoder = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
        if (!decoder) { warning_ = "Cannot decode Vorbis audio (error " + std::to_string(error) + ")"; return nullptr; }
        // Always close the decoder before its compressed backing bytes are released.
        const auto closer = [](stb_vorbis* value) { stb_vorbis_close(value); };
        std::unique_ptr<stb_vorbis, decltype(closer)> owned(decoder, closer);
        const auto info = stb_vorbis_get_info(decoder);
        if (info.channels < 1 || info.channels > 2 || info.sample_rate < XAUDIO2_MIN_SAMPLE_RATE || info.sample_rate > XAUDIO2_MAX_SAMPLE_RATE) {
            warning_ = "Unsupported Vorbis channel count or sample rate";
            return nullptr;
        }
        auto clip = std::make_unique<Clip>();
        clip->format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        clip->format.nChannels = static_cast<WORD>(info.channels);
        clip->format.nSamplesPerSec = info.sample_rate;
        clip->format.wBitsPerSample = 32;
        clip->format.nBlockAlign = static_cast<WORD>(info.channels * sizeof(float));
        clip->format.nAvgBytesPerSec = info.sample_rate * clip->format.nBlockAlign;
        // Original music is several minutes long; reserve once instead of repeatedly copying PCM.
        const auto expectedSamples = static_cast<UINT64>(stb_vorbis_stream_length_in_samples(decoder)) * info.channels;
        if (expectedSamples <= XAUDIO2_MAX_BUFFER_BYTES / sizeof(float))
            clip->pcm.reserve(static_cast<std::size_t>(expectedSamples));
        std::array<float, DecodeSamples> scratch{};
        int frames = 0;
        while ((frames = stb_vorbis_get_samples_float_interleaved(decoder, info.channels, scratch.data(), DecodeSamples)) > 0) {
            const auto samples = static_cast<std::size_t>(frames * info.channels);
            if (clip->pcm.size() + samples > XAUDIO2_MAX_BUFFER_BYTES / sizeof(float)) {
                warning_ = "Decoded audio exceeds the native buffer limit";
                return nullptr;
            }
            for (std::size_t i = 0; i < samples; ++i) {
                if (!std::isfinite(scratch[i])) { warning_ = "Invalid Vorbis PCM sample"; return nullptr; }
            }
            clip->pcm.insert(clip->pcm.end(), scratch.begin(), scratch.begin() + samples);
        }
        if (clip->pcm.empty() || stb_vorbis_get_error(decoder) != VORBIS__no_error) {
            warning_ = "Empty or damaged Vorbis audio";
            return nullptr;
        }
        return clip;
    }

    // Submit persistent PCM storage; callers keep its clip alive until voice destruction.
    IXAudio2SourceVoice* start(const Clip& clip, float volume, bool loop, UINT32 offsetFrames = 0, const float* matrix = nullptr) {
        if (!initialize()) return nullptr;
        IXAudio2SourceVoice* source = nullptr;
        if (failed("Audio voice creation", engine_->CreateSourceVoice(&source, &clip.format))) return nullptr;
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(clip.pcm.size() * sizeof(float));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(clip.pcm.data());
        buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
        buffer.PlayBegin = offsetFrames;
        // Resume a loop with its tail first, then queue full loops of the same PCM.
        // This avoids making XAudio2's loop region start before PlayBegin.
        const bool loopTail = loop && offsetFrames != 0;
        if (loopTail) { buffer.LoopCount = 0; buffer.Flags = 0; }
        if (failed("Audio gain", source->SetVolume(volume)) ||
            (matrix && failed("Audio stereo routing", source->SetOutputMatrix(master_, clip.format.nChannels, OutputChannels, matrix))) ||
            failed("Audio submission", source->SubmitSourceBuffer(&buffer))) {
            source->DestroyVoice();
            return nullptr;
        }
        if (loopTail) {
            buffer.PlayBegin = 0;
            buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
            buffer.Flags = XAUDIO2_END_OF_STREAM;
            if (failed("Music loop submission", source->SubmitSourceBuffer(&buffer))) {
                source->DestroyVoice();
                return nullptr;
            }
        }
        if (failed("Audio start", source->Start())) { source->DestroyVoice(); return nullptr; }
        return source;
    }

    // Preserve the original inverse-distance gain independently of stereo routing.
    float voiceGain(const Voice& voice) const {
        const double dx = static_cast<double>(voice.x) - listenerX_;
        const double dy = static_cast<double>(voice.y) - listenerY_;
        const double dz = static_cast<double>(voice.z) - listenerZ_;
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double excess = (std::max)(0.0, distance - ReferenceDistance);
        const double attenuation = ReferenceDistance / (ReferenceDistance + voice.rolloff * excess);
        return gain(static_cast<float>(static_cast<double>(voice.gain) * effectsGain_ * attenuation));
    }

    // Keep stereo assets' channel separation; positional mono effects use equal power.
    std::array<float, 4> voiceMatrix(const Voice& voice) const {
        const auto stereo = spatialGains(static_cast<double>(voice.x) - listenerX_,
            static_cast<double>(voice.y) - listenerY_, static_cast<double>(voice.z) - listenerZ_, voice.rolloff > 0);
        if (voice.channels == 1) return {stereo[0], stereo[1], 0.0f, 0.0f};
        // Stereo clips retain their center volume and never receive a channel boost.
        return {(std::min)(1.0f, stereo[0] * std::sqrt(2.0f)), 0.0f,
            0.0f, (std::min)(1.0f, stereo[1] * std::sqrt(2.0f))};
    }

    // Reapply positions, routing or the effects slider to already active voices.
    void refreshEffects() {
        for (const auto& voice : voices_) {
            failed("Effects gain", voice.source->SetVolume(voiceGain(voice)));
            const auto matrix = voiceMatrix(voice);
            failed("Effects stereo routing", voice.source->SetOutputMatrix(master_, voice.channels, OutputChannels, matrix.data()));
        }
    }

    // Destroy the music voice while leaving decoded PCM in the shared cache.
    void releaseMusic() {
        if (music_) music_->DestroyVoice();
        music_ = nullptr;
        musicClip_ = nullptr;
        musicOffsetFrames_ = 0;
    }

public:
    // Compute routing without opening a device; OpenAL's default listener right is +X.
    static std::array<float, 2> spatialGains(double dx, double dy, double dz, bool spatial = true) {
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double pan = spatial && std::isfinite(distance) && distance > 0
            ? (std::clamp)(dx / distance, -1.0, 1.0) : 0.0;
        const double angle = (pan + 1.0) * QuarterPi;
        return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }

    // Construction is silent and does not open an audio device.
    NativeAudio() = default;
    NativeAudio(const NativeAudio&) = delete;
    NativeAudio& operator=(const NativeAudio&) = delete;

    // Release sources before their PCM, then close the mixer on its owning thread.
    ~NativeAudio() {
        for (auto& voice : voices_) voice.source->DestroyVoice();
        releaseMusic();
        if (master_) master_->DestroyVoice();
        if (engine_) engine_->Release();
        if (comOwned_) CoUninitialize();
    }

    // Decode once per asset path; failures remain optional to the host.
    int load(const std::filesystem::path& path) {
        const auto normalized = path.lexically_normal();
        const auto found = clipIds_.find(normalized);
        if (found != clipIds_.end()) return found->second;
        try {
            auto clip = decode(path);
            if (!clip) return -1;
            const int id = static_cast<int>(clips_.size());
            clips_.push_back(std::move(clip));
            clipIds_.emplace(normalized, id);
            return id;
        } catch (const std::exception& error) { warning_ = std::string("Audio load failed: ") + error.what(); return -1; }
    }

    // Start a bounded effect; a zero rolloff disables distance attenuation.
    int play(int clipId, float volume, bool loop, float x, float y, float z, float rolloff) {
        if (clipId < 0 || static_cast<std::size_t>(clipId) >= clips_.size()) return -1;
        update();
        if (voices_.size() >= MaxVoices) { warning_ = "Audio effect voice limit reached"; return -1; }
        Voice voice;
        voice.loop = loop;
        voice.gain = gain(volume);
        voice.x = std::isfinite(x) ? x : 0.0f;
        voice.y = std::isfinite(y) ? y : 0.0f;
        voice.z = std::isfinite(z) ? z : 0.0f;
        voice.rolloff = std::isfinite(rolloff) ? (std::max)(0.0f, rolloff) : 0.0f;
        const auto& clip = *clips_[static_cast<std::size_t>(clipId)];
        voice.channels = clip.format.nChannels;
        const auto matrix = voiceMatrix(voice);
        voice.source = start(clip, voiceGain(voice), loop, 0, matrix.data());
        if (!voice.source) return -1;
        // Wrap without signed overflow and avoid reusing an active voice identifier.
        do {
            voice.id = nextVoiceId_;
            nextVoiceId_ = nextVoiceId_ == (std::numeric_limits<int>::max)() ? 1 : nextVoiceId_ + 1;
        } while (std::any_of(voices_.begin(), voices_.end(), [&voice](const Voice& item) { return item.id == voice.id; }));
        voices_.push_back(voice);
        return voice.id;
    }

    // Immediately stop the selected effect, including an infinite loop.
    void stop(int voiceId) {
        for (auto it = voices_.begin(); it != voices_.end(); ++it) {
            if (it->id != voiceId) continue;
            it->source->DestroyVoice();
            voices_.erase(it);
            return;
        }
    }

    // Query the mixer directly so guest source pools can reuse completed effects.
    bool isPlaying(int voiceId) const {
        for (const auto& voice : voices_) {
            if (voice.id != voiceId) continue;
            XAUDIO2_VOICE_STATE state{};
            voice.source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            return state.BuffersQueued != 0;
        }
        return false;
    }

    // Update an already playing source when the guest changes its individual gain.
    void setVoiceGain(int voiceId, float value) {
        for (auto& voice : voices_) {
            if (voice.id != voiceId) continue;
            voice.gain = gain(value);
            failed("Source gain", voice.source->SetVolume(voiceGain(voice)));
            return;
        }
    }

    // Update a moving source and its rolloff without restarting the submitted PCM.
    void setVoicePosition(int voiceId, float x, float y, float z, float rolloff) {
        for (auto& voice : voices_) {
            if (voice.id != voiceId) continue;
            voice.x = std::isfinite(x) ? x : 0.0f;
            voice.y = std::isfinite(y) ? y : 0.0f;
            voice.z = std::isfinite(z) ? z : 0.0f;
            voice.rolloff = std::isfinite(rolloff) ? (std::max)(0.0f, rolloff) : 0.0f;
            failed("Source position gain", voice.source->SetVolume(voiceGain(voice)));
            const auto matrix = voiceMatrix(voice);
            failed("Source stereo routing", voice.source->SetOutputMatrix(master_, voice.channels, OutputChannels, matrix.data()));
            return;
        }
    }

    // Change only the distance rolloff for an already active source.
    void setVoiceRolloff(int voiceId, float rolloff) {
        for (auto& voice : voices_) {
            if (voice.id != voiceId) continue;
            voice.rolloff = std::isfinite(rolloff) ? (std::max)(0.0f, rolloff) : 0.0f;
            failed("Source rolloff gain", voice.source->SetVolume(voiceGain(voice)));
            const auto matrix = voiceMatrix(voice);
            failed("Source stereo routing", voice.source->SetOutputMatrix(master_, voice.channels, OutputChannels, matrix.data()));
            return;
        }
    }

    // Stop effect loops without interrupting one-shots or background music.
    void stopLooping() {
        for (auto it = voices_.begin(); it != voices_.end();) {
            if (!it->loop) { ++it; continue; }
            it->source->DestroyVoice();
            it = voices_.erase(it);
        }
    }

    // Move the listener used for original attenuation and stereo source direction.
    void setListener(float x, float y, float z) {
        listenerX_ = std::isfinite(x) ? x : 0.0f;
        listenerY_ = std::isfinite(y) ? y : 0.0f;
        listenerZ_ = std::isfinite(z) ? z : 0.0f;
        refreshEffects();
    }

    // Apply the effects slider to future and currently playing sounds.
    void setEffectsGain(float value) { effectsGain_ = gain(value); refreshEffects(); }

    // Replace music at its beginning; an unreadable replacement preserves current music.
    bool playMusic(const std::filesystem::path& path, bool loop, double offsetSeconds = 0.0) {
        try {
            const int clipId = load(path);
            if (clipId < 0) return false;
            releaseMusic();
            musicLoop_ = loop;
            musicClip_ = clips_[static_cast<std::size_t>(clipId)].get();
            return seekMusic(offsetSeconds);
        } catch (const std::exception& error) { warning_ = std::string("Music load failed: ") + error.what(); return false; }
    }

    // Stop playback; the host may remember musicPosition() before calling this.
    void stopMusic() { releaseMusic(); }

    // Seek loaded music in seconds, preserving its loop mode and slider gain.
    bool seekMusic(double seconds) {
        if (!musicClip_) return false;
        if (music_) music_->DestroyVoice();
        music_ = nullptr;
        const auto frames = musicClip_->pcm.size() / musicClip_->format.nChannels;
        const double duration = static_cast<double>(frames) / musicClip_->format.nSamplesPerSec;
        seconds = std::isfinite(seconds) ? (std::max)(0.0, seconds) : 0.0;
        if (musicLoop_) seconds = std::fmod(seconds, duration);
        else if (seconds >= duration) { releaseMusic(); return true; }
        musicOffsetFrames_ = static_cast<UINT32>(seconds * musicClip_->format.nSamplesPerSec);
        music_ = start(*musicClip_, musicGain_, musicLoop_, musicOffsetFrames_);
        if (!music_) { releaseMusic(); return false; }
        return true;
    }

    // Report the current tune position for original per-tune resume bookkeeping.
    double musicPosition() const {
        if (!music_ || !musicClip_) return 0.0;
        XAUDIO2_VOICE_STATE state{};
        music_->GetState(&state);
        const auto totalFrames = musicClip_->pcm.size() / musicClip_->format.nChannels;
        const auto played = state.SamplesPlayed + musicOffsetFrames_;
        const auto position = musicLoop_ ? played % totalFrames : (std::min)(played, static_cast<UINT64>(totalFrames));
        return static_cast<double>(position) / musicClip_->format.nSamplesPerSec;
    }

    // Apply music gain immediately, including when muted to zero.
    void setMusicGain(float value) {
        musicGain_ = gain(value);
        if (music_) failed("Music gain", music_->SetVolume(musicGain_));
    }

    // Retire completed buffers on the host thread rather than in mixer callbacks.
    void update() {
        for (auto it = voices_.begin(); it != voices_.end();) {
            XAUDIO2_VOICE_STATE state{};
            it->source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued) { ++it; continue; }
            it->source->DestroyVoice();
            it = voices_.erase(it);
        }
        if (music_) {
            XAUDIO2_VOICE_STATE state{};
            music_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (!state.BuffersQueued) releaseMusic();
        }
    }

    // Return the latest diagnostic; successful operations do not erase past failures.
    const std::string& warning() const { return warning_; }
};
