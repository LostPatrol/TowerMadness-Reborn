// Original mode metadata and deterministic endless wave definitions.
#pragma once

#include <json.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tmreborn {

struct CatalogEntry {
    std::string id, map, tech, waves;
    bool endless = false;
    bool selectableKnown = false;
    bool startsUnlocked = false;
    std::string selectionNote;
};

// Enumerate packaged modes without treating resource presence as an unlock.
std::vector<CatalogEntry> loadCatalog(const std::filesystem::path& assets);
// Expand one zero-based endless wave using original float rules and table samples.
nlohmann::json generateEndlessWave(const nlohmann::json& source, int waveIndex,
                                 std::uint32_t seedOffset = 0);
// Build the original runtime endless configuration for a chosen map.
nlohmann::json makeEndlessMode(const std::string& waveSet, const std::string& map);
// Recover the map-specific signed SHA-1 seed additions used by createEndless.
std::uint32_t endlessModeSeedOffset(const std::string& waveSet, const std::string& map);

} // namespace tmreborn
