// Windows session snapshots retain simulation state and the synchronized RNG.
#include "game.hpp"
#include <algorithm>
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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Sheep,x,y,targetX,targetY,yawDegrees,size,color,state,reserved,captureStartMs,startYawDegrees)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SheepIntruder,x,y,z,yawDegrees,size,speed,walkFactor,flying,state,sheepIndex,wave,generation,model,walkModels)

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
    nlohmann::json state{{"version",1},{"mode",modeId},{"sessionSeed",sessionSeed_},{"timeMs",timeMs},{"money",money},{"lives",lives},
        {"wavesSent",wavesSent},{"score",score},{"bonus",bonus},{"waveCounters",waveCounters_},{"killed",killed},{"leaked",leaked},{"spawned",spawned},
        {"paused",paused},{"outcome",static_cast<int>(outcome)},{"enemies",enemies},{"towers",towers},{"ufos",ufos},
        {"projectiles",projectiles},{"pendingChildren",pendingChildren_},{"waves",waves},{"grid",grid_},
        {"nextId",nextId_},{"lastWaveAt",lastWaveAt_},{"nextPad",nextPad_},{"lastBoostAt",lastBoostAt_},{"rng",engine.str()}};
    for (std::size_t index = 0; index < enemies.size(); ++index)
        state["enemies"][index]["animation"] = enemyAnimation(enemies[index]);
    for (std::size_t index = 0; index < pendingChildren_.size(); ++index)
        state["pendingChildren"][index]["animation"] = enemyAnimation(pendingChildren_[index]);
    for (std::size_t index = 0; index < ufos.size(); ++index) {
        state["ufos"][index]["headingRadians"] = ufos[index].headingRadians;
        state["ufos"][index]["rotationRate"] = ufos[index].rotationRate;
    }
    for (std::size_t index = 0; index < towers.size(); ++index)
        state["towers"][index]["appearance"] = {{"startedAt",towers[index].appearanceStartedAt},{"previousType",towers[index].previousType}};
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
    state["animals"] = {{"clockMs",animalClockMs_},{"sheep",sheep_},{"intruders",intruders_}};
    if (lastAbductionFocus_) state["animals"]["lossFocus"] = *lastAbductionFocus_;
    return state;
}

// Construct a valid candidate before replacing a working match with stored data.
void Game::restoreState(const nlohmann::json& state) {
    if (state.at("version") != 1) throw std::runtime_error("Unsupported session version.");
    // Older sessions used the deterministic desktop seed; malformed optional metadata uses that adaptation.
    std::uint32_t sessionSeed = 1;
    try {
        const auto seed = state.at("sessionSeed").get<std::int64_t>();
        if (seed >= 0 && static_cast<std::uint64_t>(seed) <= UINT32_MAX) sessionSeed = static_cast<std::uint32_t>(seed);
    } catch (const nlohmann::json::exception&) {}
    Game restored(assets_,state.at("mode").get<std::string>(),sessionSeed);
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
    for (std::size_t index = 0; index < restored.towers.size(); ++index) {
        auto& tower = restored.towers[index];
        // Unknown legacy appearance stays usable without inventing its previous upgrade type.
        try {
            const auto& appearance = state.at("towers").at(index).at("appearance");
            const int started = appearance.at("startedAt").get<int>();
            const std::string previous = appearance.at("previousType").get<std::string>();
            if (started >= -1 && started <= tower.readyAt && (previous.empty() || restored.technology.at("towers").contains(previous))) {
                tower.appearanceStartedAt = started; tower.previousType = previous;
            }
        } catch (const nlohmann::json::exception&) {}
    }
    restored.ufos = state.at("ufos").get<std::vector<Ufo>>();
    for (std::size_t index = 0; index < restored.ufos.size(); ++index) try {
        const float heading = state.at("ufos").at(index).at("headingRadians").get<float>();
        if (std::isfinite(heading)) restored.ufos[index].headingRadians = heading;
    } catch (const nlohmann::json::exception&) {
        // Legacy UFO orientation is unknown and does not consume restored combat randomness.
    }
    for (std::size_t index = 0; index < restored.ufos.size(); ++index) try {
        const float rate = state.at("ufos").at(index).at("rotationRate").get<float>();
        if (std::isfinite(rate)) restored.ufos[index].rotationRate = rate;
    } catch (const nlohmann::json::exception&) {
        // Missing visual angular velocity keeps the native allocation default.
    }
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
    bool restoredAnimals = false;
    try {
        const auto& animals = state.at("animals");
        const int clock = animals.at("clockMs").get<int>();
        auto sheep = animals.at("sheep").get<std::vector<Sheep>>();
        auto intruders = animals.at("intruders").get<std::vector<SheepIntruder>>();
        if (clock < 0 || sheep.size() != restored.sheep_.size())
            throw std::runtime_error("Invalid saved animal clock or flock.");
        std::vector<bool> claimed(sheep.size(),false);
        int reserved = 0;
        for (const auto& animal : sheep) {
            if (!std::isfinite(animal.x) || !std::isfinite(animal.y) || !std::isfinite(animal.targetX)
                || !std::isfinite(animal.targetY) || !std::isfinite(animal.yawDegrees) || !std::isfinite(animal.startYawDegrees)
                || !std::isfinite(animal.size) || animal.size <= 0 || animal.color < 0 || animal.color > 2
                || animal.state < -1 || animal.state > 3 || animal.captureStartMs < 0 || animal.captureStartMs > clock
                || ((animal.state == -1 || animal.state == 3) && !animal.reserved))
                throw std::runtime_error("Invalid saved sheep.");
            if (animal.reserved) ++reserved;
        }
        if (reserved != std::clamp(static_cast<int>(sheep.size()) - restored.lives,0,static_cast<int>(sheep.size())))
            throw std::runtime_error("Invalid saved sheep reservation count.");
        for (const auto& intruder : intruders) {
            if (intruder.sheepIndex < 0 || intruder.sheepIndex >= static_cast<int>(sheep.size())
                || claimed[intruder.sheepIndex] || !sheep[intruder.sheepIndex].reserved
                || intruder.state < 0 || intruder.state > 1 || intruder.wave < 0
                || intruder.wave >= static_cast<int>(restored.waves.size()) || intruder.generation < 0
                || !std::isfinite(intruder.x) || !std::isfinite(intruder.y) || !std::isfinite(intruder.z)
                || !std::isfinite(intruder.yawDegrees) || !std::isfinite(intruder.size) || intruder.size <= 0
                || !std::isfinite(intruder.speed) || intruder.speed <= 0
                || !std::isfinite(intruder.walkFactor) || intruder.walkFactor <= 0)
                throw std::runtime_error("Invalid saved sheep intruder.");
            const auto& animal = sheep[intruder.sheepIndex];
            if ((intruder.state == 0 && (animal.state == 3 || animal.state == -1))
                || (intruder.state == 1 && animal.state != 3 && animal.state != -1))
                throw std::runtime_error("Invalid saved abduction relation.");
            const auto& wave = restored.waves.at(intruder.wave);
            const auto& frames = intruder.generation == 1 && wave.contains("subwalk") && !wave.at("subwalk").empty()
                ? wave.at("subwalk") : wave.at("walk");
            const auto model = wave.at(intruder.generation == 1 ? "submodel" : "model").get<std::string>();
            if (intruder.model != model || nlohmann::json(intruder.walkModels) != frames)
                throw std::runtime_error("Invalid saved intruder model.");
            claimed[intruder.sheepIndex] = true;
        }
        for (std::size_t index = 0; index < sheep.size(); ++index)
            if ((sheep[index].state == 3 || (sheep[index].reserved && sheep[index].state != -1)) && !claimed[index])
                throw std::runtime_error("Missing saved sheep intruder.");
        restored.sheep_ = std::move(sheep); restored.intruders_ = std::move(intruders); restored.animalClockMs_ = clock;
        restoredAnimals = true;
    } catch (const std::exception&) {
        // Constructor placement is a legacy adaptation on its isolated session seed, never the restored combat RNG.
        restored.intruders_.clear(); restored.animalClockMs_ = restored.timeMs;
        const int lost = std::max(0,static_cast<int>(restored.sheep_.size()) - restored.lives);
        for (std::size_t index = 0; index < restored.sheep_.size(); ++index)
            if (index < static_cast<std::size_t>(lost)) {
                restored.sheep_[index].reserved = true; restored.sheep_[index].state = -1;
            }
    }
    // Camera focus is optional and cannot invalidate the saved sheep state or combat RNG.
    try {
        const auto focus = state.at("animals").at("lossFocus").get<Vec2>();
        if (std::isfinite(focus.x) && std::isfinite(focus.y)) restored.lastAbductionFocus_ = focus;
    } catch (const nlohmann::json::exception&) {}
    if (restored.lives <= 0 && !restored.lastAbductionFocus_ && !restored.sheep_.empty())
        restored.lastAbductionFocus_ = Vec2{restored.sheep_.back().x,restored.sheep_.back().y};
    restored.updateDistances();
    restored.message = restoredAnimation ? "Saved game restored." : "Saved game restored; animation history reconstructed.";
    if (!restoredAnimals) restored.message += " Legacy animal state reconstructed.";
    *this = std::move(restored);
}
} // namespace tmreborn
