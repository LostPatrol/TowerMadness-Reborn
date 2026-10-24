// Display original campaign achievement conditions against the unchanged local account.
#pragma once

namespace desktop_achievements {
struct Row { std::string title, description, progress; bool earned = false; };
struct Campaign {
    std::string identifier, perfectIdentifier;
    std::vector<std::string> maps;
    int sheep = 0;
};

/** Read the original packaged definitions and sheep counts once, without altering progress. */
const std::vector<Campaign>& campaigns() {
    static const std::vector<Campaign> definitions = [] {
        std::vector<Campaign> result;
        std::ifstream input(assetsDirectory / "campaigns.achievements");
        const auto source = nlohmann::json::parse(input);
        for (const auto& item : source) {
            Campaign campaign;
            campaign.identifier = item.at("identifier").get<std::string>();
            campaign.perfectIdentifier = item.at("identifier_perfect").get<std::string>();
            campaign.maps = item.at("maps").get<std::vector<std::string>>();
            for (const auto& name : campaign.maps) {
                std::ifstream mapInput(assetsDirectory / (name + ".gamemode"));
                campaign.sheep += nlohmann::json::parse(mapInput).at("lifes").get<int>();
            }
            result.push_back(std::move(campaign));
        }
        return result;
    }();
    return definitions;
}

/** Expose verified campaign progress; IDs remain visible because online titles are unavailable. */
std::vector<Row> rows() {
    std::vector<Row> result;
    try {
        for (const auto& campaign : campaigns()) {
            int completed = 0, savedSheep = 0;
            std::string mapNames;
            for (const auto& map : campaign.maps) {
                if (!mapNames.empty()) mapNames += ", ";
                mapNames += map;
                const auto found = hostAccountInts.find(map + "_beaten");
                const int best = found == hostAccountInts.end() ? 0 : found->second;
                if (best > 0) { ++completed; savedSheep += best; }
            }
            // Match original MapProgress and PerfectMapProgress, including partial sheep progress.
            result.push_back({campaign.identifier,
                "Complete every map: " + mapNames,
                std::to_string(completed) + " / " + std::to_string(campaign.maps.size()) + " maps",
                completed == static_cast<int>(campaign.maps.size())});
            result.push_back({campaign.perfectIdentifier,
                "Save every sheep across these maps: " + mapNames,
                std::to_string(savedSheep) + " / " + std::to_string(campaign.sheep) + " sheep",
                campaign.sheep > 0 && savedSheep >= campaign.sheep});
        }
    } catch (const std::exception& error) {
        // Broken optional metadata must not prevent the original game from running.
        result.push_back({"Local achievements unavailable", error.what(), "Metadata could not be read", false});
    }
    return result;
}
} // namespace desktop_achievements
