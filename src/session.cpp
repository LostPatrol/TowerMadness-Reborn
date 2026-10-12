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

namespace {
// Legacy saves retain radians but cannot recover their original allocation history.
constexpr float LegacyDegreesPerRadian = 57.295784f;

// Append optional animation fields without weakening strict core entity decoding.
nlohmann::json enemyAnimation(const Enemy& enemy) {
    return {{"slot",enemy.nativeSlot},{"headingDegrees",enemy.visualHeadingDegrees},
        {"stunFrameTimeMs",enemy.stunFrameTimeMs},{"deathFrameTimeMs",enemy.deathFrameTimeMs}};
}
} // namespace

// Save all simulation fields that affect the next tick; visual effects may expire on reload.
nlohmann::json Game::saveState() const {
    std::ostringstream engine; engine << randomEngine_;
    nlohmann::json state{{"version",1},{"mode",modeId},{"timeMs",timeMs},{"money",money},{"lives",lives},
        {"wavesSent",wavesSent},{"score",score},{"bonus",bonus},{"waveCounters",waveCounters_},{"killed",killed},{"leaked",leaked},{"spawned",spawned},
        {"paused",paused},{"outcome",static_cast<int>(outcome)},{"enemies",enemies},{"towers",towers},{"ufos",ufos},
        {"projectiles",projectiles},{"pendingChildren",pendingChildren_},{"waves",waves},{"grid",grid_},
        {"nextId",nextId_},{"lastWaveAt",lastWaveAt_},{"nextPad",nextPad_},{"lastBoostAt",lastBoostAt_},{"rng",engine.str()}};
    for (std::size_t index = 0; index < enemies.size(); ++index)
        state["enemies"][index]["animation"] = enemyAnimation(enemies[index]);
    for (std::size_t index = 0; index < pendingChildren_.size(); ++index)
        state["pendingChildren"][index]["animation"] = enemyAnimation(pendingChildren_[index]);
    for (std::size_t index = 0; index < projectiles.size(); ++index)
        state["projectiles"][index]["trail"] = {{"position",projectiles[index].trailSample},{"threshold",projectiles[index].trailThreshold}};
    auto holds = nlohmann::json::array();
    for (const auto& hold : enemySlotHolds_) {
        nlohmann::json raw{{"slot",hold.slot},{"remainingSeconds",hold.remainingSeconds}};
        if (hold.drawable) {
            nlohmann::json enemy = hold.enemy;
            enemy["animation"] = enemyAnimation(hold.enemy);
            raw["corpse"] = {{"enemy",enemy},{"frame",hold.frame},{"deathAngleDegrees",hold.deathAngleDegrees}};
        }
        holds.push_back(std::move(raw));
    }
    state["animation"] = {{"nextSlot",nextEnemySlot_},{"slotHolds",holds}};
    return state;
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
    for (std::size_t index = 0; index < restored.projectiles.size(); ++index) {
        auto& projectile = restored.projectiles[index];
        projectile.trailSample = projectile.position;
        projectile.trailThreshold = 0;
        // Old or malformed cosmetic fields must not prevent continuation of the core match.
        try {
            const auto& trail = state.at("projectiles").at(index).at("trail");
            const Vec3 sample = trail.at("position").get<Vec3>();
            const float threshold = trail.at("threshold").get<float>();
            if (std::isfinite(sample.x) && std::isfinite(sample.y) && std::isfinite(sample.z) && std::isfinite(threshold) && threshold >= 0) {
                projectile.trailSample = sample; projectile.trailThreshold = threshold;
            }
        } catch (const nlohmann::json::exception&) {}
    }
    restored.pendingChildren_ = state.at("pendingChildren").get<std::vector<Enemy>>();
    restored.waves = state.at("waves"); restored.grid_ = state.at("grid").get<std::vector<int>>();
    restored.nextId_ = state.at("nextId").get<int>(); restored.lastWaveAt_ = state.at("lastWaveAt").get<int>();
    restored.nextPad_ = state.at("nextPad").get<int>(); restored.lastBoostAt_ = state.at("lastBoostAt").get<int>();
    // Animation extensions are optional; old or damaged visual data keeps the core match usable.
    bool restoredAnimation = false;
    if (state.contains("animation")) try {
        const auto& animation = state.at("animation");
        restored.nextEnemySlot_ = animation.at("nextSlot").get<int>();
        if (restored.nextEnemySlot_ < 0 || restored.nextEnemySlot_ > NativeEnemySlotCount)
            throw std::runtime_error("Invalid animation slot counter.");
        // Rebuild occupancy, allowing -1 solely as the non-blocking visual fallback.
        const auto occupy = [&](int slot) {
            if (slot == -1) return;
            if (slot < 0 || slot >= NativeEnemySlotCount || restored.enemySlots_[slot])
                throw std::runtime_error("Invalid animation slot.");
            restored.enemySlots_[slot] = true;
        };
        const auto restoreEnemyAnimation = [&](Enemy& enemy, const nlohmann::json& raw, bool claimSlot = true) {
            const auto& visual = raw.at("animation");
            enemy.nativeSlot = visual.at("slot").get<int>();
            enemy.visualHeadingDegrees = visual.at("headingDegrees").get<float>();
            enemy.stunFrameTimeMs = visual.at("stunFrameTimeMs").get<int>();
            enemy.deathFrameTimeMs = visual.at("deathFrameTimeMs").get<int>();
            if (!std::isfinite(enemy.visualHeadingDegrees)) throw std::runtime_error("Invalid animation heading.");
            if (claimSlot) occupy(enemy.nativeSlot);
        };
        for (std::size_t index = 0; index < restored.enemies.size(); ++index)
            restoreEnemyAnimation(restored.enemies[index],state.at("enemies").at(index));
        for (std::size_t index = 0; index < restored.pendingChildren_.size(); ++index)
            restoreEnemyAnimation(restored.pendingChildren_[index],state.at("pendingChildren").at(index));
        for (const auto& raw : animation.at("slotHolds")) {
            EnemyCorpse hold;
            hold.slot = raw.at("slot").get<int>(); hold.remainingSeconds = raw.at("remainingSeconds").get<float>();
            if (!std::isfinite(hold.remainingSeconds)) throw std::runtime_error("Invalid animation countdown.");
            occupy(hold.slot);
            // Older extensions retain occupancy without inventing a model or death pose.
            if (raw.contains("corpse")) {
                const auto& corpse = raw.at("corpse");
                hold.enemy = corpse.at("enemy").get<Enemy>();
                restoreEnemyAnimation(hold.enemy,corpse.at("enemy"),false);
                hold.frame = corpse.at("frame").get<int>();
                hold.deathAngleDegrees = corpse.at("deathAngleDegrees").get<float>();
                if (hold.enemy.nativeSlot != hold.slot || hold.enemy.alive || hold.enemy.wave < 0
                    || hold.enemy.wave >= static_cast<int>(restored.waves.size())
                    || !std::isfinite(hold.enemy.x) || !std::isfinite(hold.enemy.y) || !std::isfinite(hold.deathAngleDegrees))
                    throw std::runtime_error("Invalid saved visual corpse.");
                const auto& wave = restored.waveFor(hold.enemy);
                const auto& frames = hold.enemy.generation == 1 && wave.contains("subwalk") && !wave.at("subwalk").empty()
                    ? wave.at("subwalk") : wave.at("walk");
                if (hold.frame < 0 || (frames.empty() ? hold.frame != 0 : hold.frame >= static_cast<int>(frames.size())))
                    throw std::runtime_error("Invalid saved corpse frame.");
                hold.drawable = hold.enemy.generation >= wave.value("spawn",0);
            }
            restored.enemySlotHolds_.push_back(std::move(hold));
        }
        restoredAnimation = true;
    } catch (const std::exception&) {
        // Optional visual corruption must not turn a valid version-one match into a failed load.
    }
    if (!restoredAnimation) {
        restored.enemySlots_.fill(false); restored.enemySlotHolds_.clear(); restored.nextEnemySlot_ = 0;
        // Entity order is the best available fallback; historical slots and frozen times are unknown.
        const auto rebuildAnimation = [&](Enemy& enemy) {
            enemy.nativeSlot = restored.allocateEnemySlot();
            enemy.visualHeadingDegrees = enemy.heading * LegacyDegreesPerRadian;
            enemy.stunFrameTimeMs = restored.timeMs; enemy.deathFrameTimeMs = restored.timeMs;
        };
        for (auto& enemy : restored.enemies) rebuildAnimation(enemy);
        for (auto& enemy : restored.pendingChildren_) rebuildAnimation(enemy);
    }
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
    restored.updateDistances();
    restored.message = restoredAnimation ? "Saved game restored." : "Saved game restored; animation history reconstructed.";
    *this = std::move(restored);
}
} // namespace tmreborn
