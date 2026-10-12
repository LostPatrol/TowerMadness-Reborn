// Load and atomically replace a small, versioned Windows progress profile.
#include "profile.hpp"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace tmreborn {
namespace {
// Changing this value requires an explicit migration of the profile fields.
constexpr int ProfileVersion = 1;
}

// Prefer LocalAppData; the temporary directory is a usable fallback on unusual hosts.
std::filesystem::path Profile::userDirectory() {
    PWSTR location = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&location))) {
        const auto result = std::filesystem::path(location) / "TowerMadness-Reborn";
        CoTaskMemFree(location);
        return result;
    }
    return std::filesystem::temp_directory_path() / "TowerMadness-Reborn";
}

// Preserve malformed files and report the exact problem instead of blocking play.
Profile::Profile(const std::filesystem::path& directory) : directory_(directory) {
    data = {{"version",ProfileVersion},{"completed",nlohmann::json::object()},
            {"settings",{{"speed",1},{"volume",0.0f},{"musicVolume",0.0f}}},{"lastMode","PlainPlane"}};
    try {
        const auto path = directory_ / "profile.json";
        if (!std::filesystem::exists(path)) return;
        std::ifstream stream(path);
        auto loaded = nlohmann::json::parse(stream);
        if (loaded.at("version") != ProfileVersion || !loaded.at("completed").is_object() || !loaded.at("settings").is_object())
            throw std::runtime_error("Unsupported or malformed profile");
        const auto& settings = loaded.at("settings");
        if (!settings.value("speed",nlohmann::json(1)).is_number_integer()
            || !settings.value("volume",nlohmann::json(0.0f)).is_number()
            || !settings.value("musicVolume",nlohmann::json(0.0f)).is_number()) throw std::runtime_error("Invalid saved settings");
        data.update(loaded);
    } catch (const std::exception& error) {
        warning = "Progress could not be read: " + std::string(error.what());
        // Retain the damaged input before later successful progress writes replace it.
        std::error_code ignored;
        std::filesystem::copy_file(directory_ / "profile.json",directory_ / "profile.damaged.json",
                                   std::filesystem::copy_options::overwrite_existing,ignored);
    }
}

// Write and close a temporary file before Windows replaces the previous profile.
bool Profile::save() {
    try {
        std::filesystem::create_directories(directory_);
        const auto temporary = directory_ / "profile.tmp";
        std::ofstream stream(temporary,std::ios::binary | std::ios::trunc);
        stream << data.dump(2); stream.close();
        if (!stream) throw std::runtime_error("Cannot write profile.tmp");
        if (!MoveFileExW(temporary.c_str(),(directory_ / "profile.json").c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace profile.json, Windows error " + std::to_string(GetLastError()));
        warning.clear(); return true;
    } catch (const std::exception& error) {
        warning = "Progress could not be saved: " + std::string(error.what()); return false;
    }
}

// Keep only the best result while preserving progress across restarts.
void Profile::complete(const std::string& mode, int sheep, int score) {
    auto& result = data["completed"][mode];
    if (!result.is_object()) result = nlohmann::json::object();
    result["sheep"] = std::max(sheep,result.value("sheep",0));
    result["score"] = std::max(score,result.value("score",0));
    save();
}
} // namespace tmreborn
