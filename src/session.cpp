// Windows session snapshots retain simulation state and the synchronized RNG.
#include "game.hpp"
#include <sstream>
#include <stdexcept>

namespace tmreborn {
// Describe the original entity state, excluding rendering caches and derived routes.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Cell,x,y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec2,x,y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3,x,y,z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Enemy,id,wave,generation,x,y,heading,traveled,heat,moveTarget,health,maxHealth,alive,model,slowMs,stunMs,lastRegenAt,slowFactor,previous)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Tower,id,x,y,target,refund,type,readyAt,nextShotAt,heading,selling,triggeredAt,damageBoost,consumed)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Ufo,state,wave,remaining,lastReleaseAt,pad,x,y,z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Projectile,position,velocity,waypoint,previous,origin,endpoint,source,kind,model,target,damage,bornAt,speed,splash,traveled,elapsed,elevation,speedMultiplier,waypointReached,active)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WaveCounter,remaining,bonus)

// Save all simulation fields that affect the next tick; visual effects may expire on reload.
nlohmann::json Game::saveState() const {
    std::ostringstream engine; engine << randomEngine_;
    return {{"version",1},{"mode",modeId},{"timeMs",timeMs},{"money",money},{"lives",lives},
        {"wavesSent",wavesSent},{"score",score},{"bonus",bonus},{"waveCounters",waveCounters_},{"killed",killed},{"leaked",leaked},{"spawned",spawned},
        {"paused",paused},{"outcome",static_cast<int>(outcome)},{"enemies",enemies},{"towers",towers},{"ufos",ufos},
        {"projectiles",projectiles},{"pendingChildren",pendingChildren_},{"waves",waves},{"grid",grid_},
        {"nextId",nextId_},{"lastWaveAt",lastWaveAt_},{"nextPad",nextPad_},{"lastBoostAt",lastBoostAt_},{"rng",engine.str()}};
}

// Construct a valid candidate before replacing a working match with stored data.
void Game::restoreState(const nlohmann::json& state) {
    if (state.at("version") != 1) throw std::runtime_error("Unsupported session version.");
    Game restored(assets_,state.at("mode").get<std::string>());
    restored.timeMs = state.at("timeMs").get<int>(); restored.money = state.at("money").get<int>();
    restored.lives = state.at("lives").get<int>(); restored.wavesSent = state.at("wavesSent").get<int>();
    restored.score = state.at("score").get<int>(); restored.killed = state.at("killed").get<int>();
    restored.bonus = state.value("bonus",0);
    if (state.contains("waveCounters")) restored.waveCounters_ = state.at("waveCounters").get<std::map<int,WaveCounter>>();
    restored.leaked = state.at("leaked").get<int>(); restored.spawned = state.at("spawned").get<int>();
    restored.paused = state.at("paused").get<bool>();
    restored.outcome = static_cast<Outcome>(state.at("outcome").get<int>());
    restored.enemies = state.at("enemies").get<std::deque<Enemy>>();
    restored.towers = state.at("towers").get<std::vector<Tower>>();
    restored.ufos = state.at("ufos").get<std::vector<Ufo>>();
    restored.projectiles = state.at("projectiles").get<std::vector<Projectile>>();
    restored.pendingChildren_ = state.at("pendingChildren").get<std::vector<Enemy>>();
    restored.waves = state.at("waves"); restored.grid_ = state.at("grid").get<std::vector<int>>();
    restored.nextId_ = state.at("nextId").get<int>(); restored.lastWaveAt_ = state.at("lastWaveAt").get<int>();
    restored.nextPad_ = state.at("nextPad").get<int>(); restored.lastBoostAt_ = state.at("lastBoostAt").get<int>();
    std::istringstream engine(state.at("rng").get<std::string>()); engine >> restored.randomEngine_;
    if (!engine || restored.grid_.size() != static_cast<std::size_t>(restored.width * restored.height)
        || !restored.waves.is_array() || restored.wavesSent < 0 || restored.wavesSent > static_cast<int>(restored.waves.size())
        || restored.ufos.size() != restored.pads_.size()) throw std::runtime_error("Invalid saved simulation state.");
    for (const auto& enemy : restored.enemies)
        if (enemy.wave < 0 || enemy.wave >= static_cast<int>(restored.waves.size()) || !std::isfinite(enemy.x) || !std::isfinite(enemy.y)
            || enemy.x < 0 || enemy.y < 0 || enemy.x >= restored.width || enemy.y >= restored.height)
            throw std::runtime_error("Invalid saved enemy.");
    for (const auto& tower : restored.towers)
        if (!restored.technology.at("towers").contains(tower.type) || tower.x < 0 || tower.y < 0
            || tower.x + 1 >= restored.width || tower.y + 1 >= restored.height) throw std::runtime_error("Invalid saved tower.");
    restored.updateDistances(); restored.message = "Saved game restored.";
    *this = std::move(restored);
}
} // namespace tmreborn
