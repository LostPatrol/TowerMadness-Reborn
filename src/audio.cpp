// Decode original Vorbis assets; playback remains disabled until user opt-in.
#include "audio.hpp"
#include <SDL3/SDL.h>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <ostream>
#include <vector>

#ifdef _MSC_VER
// MSVC emits this upstream decoder warning during optimization after pragma pop.
#pragma warning(disable: 4701)
#pragma warning(push, 0)
#endif
#include <stb_vorbis.c>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace tmreborn {
namespace {
// Simultaneous effects are capped to keep rapid-fire scenes bounded.
constexpr std::size_t MaxVoices = 32;
// Decode chunks hold at most this many interleaved samples.
constexpr int ChunkSamples = 16384;
// Original packaged sound count makes missing assets visible in offline checks.
constexpr int ExpectedSounds = 36;

struct Decoder {
    std::vector<unsigned char> bytes;
    stb_vorbis* handle = nullptr;
    // Release decoder state before its backing compressed bytes.
    ~Decoder() { if (handle) stb_vorbis_close(handle); }
    // Load via filesystem paths so non-ASCII installation paths work on Windows.
    bool open(const std::filesystem::path& path, std::string& warning) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) { warning = "Cannot read audio: " + path.filename().string(); return false; }
        const auto length = file.tellg();
        if (length <= 0 || length > std::numeric_limits<int>::max()) {
            warning = "Invalid audio size: " + path.filename().string(); return false;
        }
        bytes.resize(static_cast<std::size_t>(length));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            warning = "Incomplete audio read: " + path.filename().string(); return false;
        }
        int error = 0;
        handle = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
        if (!handle) { warning = "Vorbis decode failed: " + path.filename().string(); return false; }
        const auto info = stb_vorbis_get_info(handle);
        if (info.channels < 1 || info.channels > 2 || info.sample_rate == 0) {
            warning = "Unsupported audio format: " + path.filename().string(); return false;
        }
        return true;
    }
};

struct Clip {
    SDL_AudioSpec spec{};
    std::vector<float> pcm;
};

// Decode an entire short effect or perform offline validation of a music asset.
bool decode(const std::filesystem::path& path, Clip& clip, std::string& warning) {
    Decoder decoder;
    if (!decoder.open(path, warning)) return false;
    const auto info = stb_vorbis_get_info(decoder.handle);
    clip.spec = {SDL_AUDIO_F32, info.channels, static_cast<int>(info.sample_rate)};
    std::array<float, ChunkSamples> buffer{};
    int frames = 0;
    while ((frames = stb_vorbis_get_samples_float_interleaved(decoder.handle, info.channels, buffer.data(), ChunkSamples)) > 0) {
        const int samples = frames * info.channels;
        for (int i = 0; i < samples; ++i) {
            if (!std::isfinite(buffer[static_cast<std::size_t>(i)])) {
                warning = "Nonfinite PCM: " + path.filename().string(); return false;
            }
        }
        clip.pcm.insert(clip.pcm.end(), buffer.begin(), buffer.begin() + samples);
    }
    if (clip.pcm.empty() || stb_vorbis_get_error(decoder.handle) != VORBIS__no_error) {
        warning = "Empty or damaged audio: " + path.filename().string(); return false;
    }
    return true;
}

// Sanitize persisted/user settings without allowing NaN to reach SDL.
float volume(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
} // namespace

struct Audio::Impl {
    std::filesystem::path root;
    std::map<std::string, std::string> names;
    std::map<std::string, Clip> clips;
    std::vector<SDL_AudioStream*> voices;
    std::unique_ptr<Decoder> musicDecoder;
    SDL_AudioStream* musicStream = nullptr;
    SDL_AudioSpec musicSpec{};
    std::string musicName = "game";
    std::string warning;
    float effectsVolume = 0.0f;
    float musicVolume = 0.0f;
    bool initialized = false;

    // Keep failures local to optional playback and expose the SDL diagnostic.
    void error() { warning = std::string("Audio unavailable: ") + SDL_GetError(); }
    // Lazily initialize only after a positive user volume selection.
    bool initialize() {
        if (initialized) return true;
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { error(); return false; }
        initialized = true;
        return true;
    }
    // Stop looping music and release its compressed backing storage.
    void stopMusic() {
        if (musicStream) SDL_DestroyAudioStream(musicStream);
        musicStream = nullptr;
        musicDecoder.reset();
    }
    // Close every logical device when the manager is muted or destroyed.
    void stop() {
        stopMusic();
        for (auto* stream : voices) SDL_DestroyAudioStream(stream);
        voices.clear();
        if (initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized = false;
    }
    // Open a paused SDL stream and set its gain before any PCM is resumed.
    SDL_AudioStream* stream(const SDL_AudioSpec& spec, float gain) {
        if (!initialize()) return nullptr;
        auto* result = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!result) { error(); return nullptr; }
        if (!SDL_SetAudioStreamGain(result, gain)) { error(); SDL_DestroyAudioStream(result); return nullptr; }
        return result;
    }
    // Start the selected music; its PCM is streamed in small bounded chunks.
    void startMusic() {
        if (musicVolume <= 0.0f || musicStream) return;
        auto decoder = std::make_unique<Decoder>();
        if (!decoder->open(root / "audio" / (musicName + ".ogg"), warning)) return;
        const auto info = stb_vorbis_get_info(decoder->handle);
        musicSpec = {SDL_AUDIO_F32, info.channels, static_cast<int>(info.sample_rate)};
        musicStream = stream(musicSpec, musicVolume);
        if (!musicStream) return;
        musicDecoder = std::move(decoder);
    }
};

// Configuration loading never creates audio devices, including for saved gains.
Audio::Audio(const std::filesystem::path& root) : impl_(std::make_unique<Impl>()) {
    impl_->root = std::filesystem::is_directory(root / "audio") ? root : root / "assets";
    for (const auto* config : {"game.sound.json", "ui.sound.json"}) {
        try {
            std::ifstream file(impl_->root / "data" / config);
            const auto settings = nlohmann::json::parse(file);
            for (auto it = settings.begin(); it != settings.end(); ++it) {
                if (it.value().contains("file")) impl_->names[it.key()] = it.value().at("file").get<std::string>();
            }
        } catch (const std::exception& e) { impl_->warning = std::string("Audio config: ") + e.what(); }
    }
}

// Destruction can only close devices previously opened after explicit opt-in.
Audio::~Audio() { impl_->stop(); }

// Positive volumes opt into playback; full mute shuts the subsystem down.
void Audio::setVolumes(float effects, float music) {
    impl_->effectsVolume = volume(effects);
    impl_->musicVolume = volume(music);
    if (impl_->effectsVolume == 0.0f) {
        for (auto* voice : impl_->voices) SDL_DestroyAudioStream(voice);
        impl_->voices.clear();
    } else {
        for (auto* voice : impl_->voices) if (!SDL_SetAudioStreamGain(voice, impl_->effectsVolume)) impl_->error();
    }
    if (impl_->musicVolume == 0.0f) impl_->stopMusic();
    else if (impl_->musicStream && !SDL_SetAudioStreamGain(impl_->musicStream, impl_->musicVolume)) impl_->error();
    if (impl_->effectsVolume == 0.0f && impl_->musicVolume == 0.0f) impl_->stop();
    else { impl_->startMusic(); update(); }
}

// Original disabled comment_file entries stay silent rather than inventing sounds.
void Audio::play(const std::string& sound) {
    if (sound == "menu" || sound == "build" || sound == "game") {
        if (impl_->musicName != sound) { impl_->stopMusic(); impl_->musicName = sound; }
        impl_->startMusic();
        update();
        return;
    }
    if (impl_->effectsVolume <= 0.0f) return;
    const auto name = impl_->names.find(sound);
    if (name == impl_->names.end()) return;
    auto found = impl_->clips.find(name->second);
    if (found == impl_->clips.end()) {
        Clip clip;
        if (!decode(impl_->root / "audio" / (name->second + ".ogg"), clip, impl_->warning)) return;
        found = impl_->clips.emplace(name->second, std::move(clip)).first;
    }
    update();
    if (impl_->voices.size() >= MaxVoices) {
        SDL_DestroyAudioStream(impl_->voices.front());
        impl_->voices.erase(impl_->voices.begin());
    }
    auto* voice = impl_->stream(found->second.spec, impl_->effectsVolume);
    if (!voice) return;
    const auto& pcm = found->second.pcm;
    if (!SDL_PutAudioStreamData(voice, pcm.data(), static_cast<int>(pcm.size() * sizeof(float))) ||
        !SDL_FlushAudioStream(voice) || !SDL_ResumeAudioStreamDevice(voice)) {
        impl_->error(); SDL_DestroyAudioStream(voice); return;
    }
    impl_->voices.push_back(voice);
}

// Main-thread pumping avoids callbacks and bounds pending music to 0.5s plus a chunk.
void Audio::update() {
    for (auto it = impl_->voices.begin(); it != impl_->voices.end();) {
        const int available = SDL_GetAudioStreamAvailable(*it);
        if (available <= 0) {
            if (available < 0) impl_->error();
            SDL_DestroyAudioStream(*it);
            it = impl_->voices.erase(it);
        } else ++it;
    }
    if (!impl_->musicStream || !impl_->musicDecoder) return;
    // Half a second is enough to tolerate ordinary rendering stalls.
    const int targetBytes = impl_->musicSpec.freq * impl_->musicSpec.channels * static_cast<int>(sizeof(float)) / 2;
    std::array<float, ChunkSamples> buffer{};
    int queued = SDL_GetAudioStreamQueued(impl_->musicStream);
    if (queued < 0) { impl_->error(); impl_->stopMusic(); return; }
    while (queued < targetBytes) {
        int frames = stb_vorbis_get_samples_float_interleaved(impl_->musicDecoder->handle,
            impl_->musicSpec.channels, buffer.data(), ChunkSamples);
        if (frames == 0) {
            if (stb_vorbis_get_error(impl_->musicDecoder->handle) != VORBIS__no_error) {
                impl_->warning = "Damaged music: " + impl_->musicName; impl_->stopMusic(); return;
            }
            if (!stb_vorbis_seek_start(impl_->musicDecoder->handle)) {
                impl_->warning = "Cannot loop music: " + impl_->musicName; impl_->stopMusic(); return;
            }
            frames = stb_vorbis_get_samples_float_interleaved(impl_->musicDecoder->handle,
                impl_->musicSpec.channels, buffer.data(), ChunkSamples);
            if (frames == 0) { impl_->warning = "Empty music: " + impl_->musicName; impl_->stopMusic(); return; }
        }
        for (int i = 0; i < frames * impl_->musicSpec.channels; ++i) {
            if (!std::isfinite(buffer[static_cast<std::size_t>(i)])) {
                impl_->warning = "Nonfinite music PCM: " + impl_->musicName; impl_->stopMusic(); return;
            }
        }
        const int bytes = frames * impl_->musicSpec.channels * static_cast<int>(sizeof(float));
        if (!SDL_PutAudioStreamData(impl_->musicStream, buffer.data(), bytes)) {
            impl_->error(); impl_->stopMusic(); return;
        }
        queued += bytes;
    }
    if (!SDL_ResumeAudioStreamDevice(impl_->musicStream)) { impl_->error(); impl_->stopMusic(); }
}

// Audio errors are nonfatal and can be surfaced in the game's settings panel.
const std::string& Audio::warning() const { return impl_->warning; }

// This path performs file I/O and Vorbis decoding only, never SDL audio calls.
bool Audio::verifyAll(std::ostream& output) const {
    std::error_code error;
    std::filesystem::directory_iterator files(impl_->root / "audio", error);
    if (error) { output << "Audio directory unavailable: " << error.message() << '\n'; return false; }
    int count = 0;
    bool valid = true;
    for (const auto& file : files) {
        if (file.path().extension() != ".ogg") continue;
        ++count;
        Clip clip;
        std::string diagnostic;
        const bool decoded = decode(file.path(), clip, diagnostic);
        output << file.path().filename().string() << ": ";
        if (decoded) output << clip.spec.freq << "Hz " << clip.spec.channels << "ch "
            << clip.pcm.size() / static_cast<std::size_t>(clip.spec.channels) << " frames\n";
        else { output << diagnostic << '\n'; valid = false; }
    }
    output << "Audio verification: " << count << '/' << ExpectedSounds << " files, " << (valid ? "PCM valid" : "failed") << '\n';
    return valid && count == ExpectedSounds;
}
} // namespace tmreborn
