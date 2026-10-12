// Versioned Windows progress and settings, stored outside the release directory.
#pragma once
#include <json.hpp>
#include <filesystem>
#include <string>

namespace tmreborn {
class Profile {
public:
    // Read the user profile, keeping the game usable when a file is damaged.
    explicit Profile(const std::filesystem::path& directory);
    // Atomically replace the profile with the current progress and settings.
    bool save();
    // Record the best sheep count and score for a completed packaged mode.
    void complete(const std::string& mode, int sheep, int score);
    // Resolve the current Windows user's writable application-data directory.
    static std::filesystem::path userDirectory();

    nlohmann::json data;
    std::string warning;
private:
    std::filesystem::path directory_;
};
} // namespace tmreborn
