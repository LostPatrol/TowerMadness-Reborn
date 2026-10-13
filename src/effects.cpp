// Recovered additive tower support, status merging, and Tesla pull collision rules.
#include "game.hpp"
#include <algorithm>

namespace tmreborn {

// Reset support multipliers and add every original non-targeting tower's contribution.
void Game::updateBoosts() {
    for (auto& tower : towers) tower.damageBoost = 1;
    for (const auto& source : towers) {
        const auto& definition = technology.at("towers").at(source.type);
        if (definition.value("antiair",false) || definition.value("antiground",false)) continue;
        const auto& weapon = technology.at("weapons").at(definition.at("weapon").get<std::string>());
        if (weapon.at("type") != "boost") continue;
        const float range = definition.at("range").get<float>();
        const float contribution = weapon.at("boost").get<float>();
        // Original support recomputation emits a short realtime rangecircle pulse.
        addEffect({{source.x + 1.0f,source.y + 1.0f,0.2f},range,timeMs + 875,"boost",timeMs,source.type});
        // The original scan deliberately includes towers under construction or being sold.
        for (auto& target : towers) {
            const auto& targetDefinition = technology.at("towers").at(target.type);
            if (target.type.starts_with("ElectroSlow") || target.type.starts_with("ElectroBoost") ||
                target.type.starts_with("Muscle") || targetDefinition.value("isbait",0)) continue;
            const int dx = source.x - target.x, dy = source.y - target.y;
            if (static_cast<float>(dx * dx + dy * dy) < range * range)
                target.damageBoost += contribution;
        }
    }
    lastBoostAt_ = timeMs;
}

// A weaker effect extends the stronger one; equal or stronger effects replace its duration.
void Game::slow(Enemy& enemy, float factor, int duration) {
    if (enemy.slowMs > 0 && enemy.slowFactor > factor) {
        enemy.slowMs = std::max(enemy.slowMs,duration);
        return;
    }
    enemy.slowMs = duration;
    enemy.slowFactor = factor;
}

// Validate one full direction unit before applying the smaller original Tesla displacement.
void Game::attract(Enemy& enemy, Vec2 direction, float amount) {
    const int x = static_cast<int>(enemy.x + direction.x);
    const int y = static_cast<int>(enemy.y + direction.y);
    // Pull collision uses bit1 alone, including the otherwise walkable bait footprint.
    if (x < 0 || y < 0 || x >= width || y >= height || (grid_[x + y * width] & 1)) return;
    enemy.previous = {enemy.x,enemy.y};
    enemy.x += direction.x * amount;
    enemy.y += direction.y * amount;
}

} // namespace tmreborn
