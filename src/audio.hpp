// Optional original Ogg playback and device-free asset verification.
#pragma once
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>

namespace tmreborn {
class Audio {
public:
    // Locate original assets without initializing the audio subsystem.
    explicit Audio(const std::filesystem::path& root);
    // Release streams and the audio subsystem owned by this manager.
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    // Apply user-selected gains; zero gains close all playback devices.
    void setVolumes(float effects, float music);
    // Play a configured effect or select menu/build/game looping music.
    void play(const std::string& sound);
    // Stop already-open playback for the original final capture without changing user gains.
    void silence();
    // Refill bounded music queues and retire completed effect streams.
    void update();
    // Return the last nonfatal audio diagnostic.
    const std::string& warning() const;
    // Decode every packaged Ogg and check finite PCM without any device calls.
    bool verifyAll(std::ostream& output) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace tmreborn
