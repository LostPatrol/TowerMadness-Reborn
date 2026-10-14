// Data-driven single-player simulation reconstructed from the original ARM game.
#pragma once

#include <json.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <random>
#include <deque>
#include <map>
#include <array>
#include <optional>
#include "math.hpp"

namespace tmreborn {

// The original GAME_alloc initializes the simulation interval to 25 milliseconds.
inline constexpr int SimulationStepMs = 25;
// ENEMIES_alloc owns 1024 lookup slots independently of desktop entity IDs.
inline constexpr int NativeEnemySlotCount = 1024;

struct Cell { int x = 0, y = 0; };
struct Sheep {
    // Native states are hidden (-1), eating, grazing, turning, and abduction.
    int state = 1, color = 0, captureStartMs = 0;
    float x = 0, y = 0, targetX = 0, targetY = 0;
    float yawDegrees = 0, startYawDegrees = 0, size = 1;
    // Reservation is separate from visibility and the gameplay lives counter.
    bool reserved = false;
};
struct SheepIntruder {
    // Leaked enemies leave combat but retain the original geometry and target sheep.
    int sheepIndex = -1, state = 0, wave = 0, generation = 0;
    float x = 0, y = 0, z = 0, yawDegrees = 0, size = 1;
    float speed = 1, walkFactor = 1;
    bool flying = false;
    std::string model;
    std::vector<std::string> walkModels;
};
struct Enemy {
    int id = 0, wave = 0, generation = 0;
    float x = 0, y = 0, heading = 0, traveled = 0, heat = 1;
    Cell moveTarget{};
    std::int64_t health = 0, maxHealth = 0;
    bool alive = true;
    std::string model;
    // Timers use original simulation milliseconds; slow is a reduction, not remaining speed.
    int slowMs = 0, stunMs = 0, lastRegenAt = 0;
    float slowFactor = 0;
    Vec2 previous{};
    // Original animation state is independent of combat IDs and instantaneous yaw.
    int nativeSlot = -1, stunFrameTimeMs = 0, deathFrameTimeMs = 0;
    float visualHeadingDegrees = 0;
};
struct EnemyCorpse {
    // Retained visual occupancy never participates in the combat entity deque.
    int slot = -1;
    float remainingSeconds = 0;
    // Freeze the death pose and geometry frame before later combat removes the entity.
    Enemy enemy{};
    int frame = 0;
    float deathAngleDegrees = 0;
    // Legacy occupancy records and splitting parents have no drawable corpse.
    bool drawable = false;
};
struct Tower {
    int id = 0, x = 0, y = 0, target = -1, refund = 0;
    std::string type;
    int readyAt = 0, nextShotAt = 0;
    float heading = 0;
    bool selling = false;
    int triggeredAt = -1;
    float damageBoost = 1;
    bool consumed = false;
    // Original construction/sale animation uses game time and remembers the upgrade's old body.
    int appearanceStartedAt = -1;
    std::string previousType;
};
struct EffectTiming {
    // Real seconds are cosmetic; legacy game milliseconds remain gameplay metadata.
    double bornSeconds = -1, expiresSeconds = -1;
};
struct Beam {
    float fromX = 0, fromY = 0, toX = 0, toY = 0, toZ = 0;
    int expiresAt = 0;
    // Preserve the recovered muzzle or chain height and weapon family for rendering.
    float fromZ = 0;
    std::string kind;
    // Snapshot the original effect pose and type before damage removes its target.
    std::string sourceType;
    int bornAt = 0;
    float heading = 0, elevation = 0, length = 0, radius = 0;
    EffectTiming timing;
};
struct Ufo {
    // States follow landing, releasing, departure, and idle lifecycle phases.
    enum class State { Landing, Releasing, Leaving, Idle };
    State state = State::Idle;
    int wave = 0, remaining = 0, lastReleaseAt = 0;
    Cell pad{};
    float x = 0, y = 0, z = 0;
    // ENEMIES_alloc samples the UFO's initial spin after initializing the flock.
    float headingRadians = 0;
    // Native draw multiplies the current phase rate by absolute game time.
    float rotationRate = 10;
};
struct Projectile {
    Vec3 position{}, velocity{}, waypoint{}, previous{}, origin{}, endpoint{};
    Tower source;
    std::string kind, model;
    int target = -1, damage = 0, bornAt = 0;
    float speed = 0, splash = 0, traveled = 0;
    float elapsed = 0, elevation = 0, speedMultiplier = 1;
    bool waypointReached = true, active = true;
    // Native trail sampling is independent of the last simulation position.
    Vec3 trailSample{};
    float trailThreshold = 0;
};
struct Effect {
    Vec3 position{};
    float radius = 0;
    int expiresAt = 0;
    std::string kind;
    // Keep simulation birth metadata for visual hashing and existing save-independent events.
    int bornAt = 0;
    std::string sourceType;
    float heading = 0, elevation = 0;
    // Trail events preserve both original world-space endpoints.
    Vec3 end{};
    EffectTiming timing;
};
struct WaveCounter { int remaining = 0, bonus = 0; };
enum class Outcome { Preparing, Running, Won, Lost };

class Game {
public:
    // Load the original normalized map, technology, and complete wave definitions.
    explicit Game(const std::filesystem::path& assets, const std::string& modeId = "PlainPlane", std::uint32_t sessionSeed = 1);
    // Select a packaged mode while retaining the configured technology and wave rules.
    void loadMode(const std::string& modeId);
    // Enumerate enabled root towers in the original technology palette order.
    std::vector<std::string> baseTowers() const;
    // Restore opening money, sheep, empty towers, and the original navigation grid.
    void restart();
    // Advance one original 25 ms tick, including terminal abduction completion.
    void tick();
    // Advance cosmetic realtime independently of paused or accelerated game ticks.
    void advanceEffects(double realtimeSeconds);
    // Offline verification uses deterministic game seconds until a realtime clock is supplied.
    double effectClockSeconds() const;
    // Dispatch the next complete original wave through an idle UFO.
    bool sendWave();
    // Occupy a 2x2 footprint only if all spawn points and live enemies keep a route.
    bool build(int x, int y, const std::string& type = "Kilowatt Laser");
    // Begin an enabled original upgrade branch and charge its target cost.
    bool upgrade(int id, const std::string& nextType = "");
    // Begin dismantling, refunding money and releasing occupancy after its timer.
    bool sell(int id);
    // Find an existing tower covering a selected navigation cell.
    const Tower* towerAt(int x, int y) const;
    // Inspect a 2x2 footprint without changing money or the live distance field.
    bool canBuild(int x, int y) const;
    // Return an empty reason only when the complete original construction rules allow it.
    std::string buildBlockReason(int x, int y, const std::string& type) const;
    // Inspect an upgrade branch without charging funds or changing construction state.
    std::string upgradeBlockReason(int id, const std::string& nextType = "") const;
    // Inspect dismantling eligibility without changing the selected tower.
    std::string sellBlockReason(int id) const;
    // Return the exact original refund, retaining an already scheduled sale amount.
    int sellRefund(int id) const;
    // Return time until the next automatic dispatch attempt, including its strict boundary.
    int nextWaveDelayMs() const;
    // Return the completion bonus reserved by a wave dispatched at the current time.
    int earlyWaveBonus() const;
    // Return the shortest ground-route distance from a navigation cell.
    float distanceAt(int x, int y) const;
    // Resolve an original wave definition, including child-enemy appearance.
    const nlohmann::json& waveFor(const Enemy& enemy) const;
    // Select the original time-based walk or generation-one subwalk geometry frame.
    int walkFrame(const Enemy& enemy) const;
    // Inspect frozen visual corpses, including non-drawable legacy slot occupancy.
    const std::vector<EnemyCorpse>& corpses() const;
    // Inspect original flock state without coupling its visibility to lost lives.
    const std::vector<Sheep>& sheep() const;
    // Capture focus is fixed at the last reservation, before further grazing motion.
    std::optional<Vec2> lastAbductionFocus() const;
    // Inspect retained abductors independently of the combat enemy deque.
    const std::vector<SheepIntruder>& intruders() const;
    // Return the animal subsystem clock, advanced after sheep and intruder updates.
    int animalClockMs() const;
    // Keep terminal scenes advancing until every reserved sheep has departed.
    bool hasPendingAbductions() const;
    // Share recovered target elevation between muzzle geometry and model presentation.
    float towerElevation(const Tower& tower) const;
    // Return a stable summary used by regression checks and diagnostics.
    nlohmann::json snapshot() const;
    // Original final score includes earned score, completed-wave bonus, and forty per sheep.
    int finalScore() const;
    // Serialize and restore a Windows session, including projectiles and random state.
    nlohmann::json saveState() const;
    void restoreState(const nlohmann::json& state);

    nlohmann::json map, technology, waves, mode;
    // Stable enemy references permit newborn splits to join subsequent chain jumps safely.
    std::deque<Enemy> enemies;
    std::vector<Tower> towers;
    std::vector<Beam> beams;
    std::vector<Ufo> ufos;
    std::vector<Projectile> projectiles;
    std::vector<Effect> effects;
    std::vector<std::string> soundEvents;
    std::string modeId = "PlainPlane";
    bool endless = false;
    int width = 0, height = 0, money = 0, lives = 0, timeMs = 0;
    int wavesSent = 0, killed = 0, leaked = 0, spawned = 0;
    int score = 0, bonus = 0;
    bool paused = false;
    Outcome outcome = Outcome::Preparing;
    std::string message;

private:
    // Allocate the original single-player flock using the shared combat RNG.
    void initializeAnimals();
    // Select a field cell by signed integer modulo, then consume two float offsets.
    Vec2 generateAnimalFieldPoint();
    // Advance sheep first, intruders second, and the animal clock last.
    void updateAnimals();
    // Reserve the first available sheep and retain a leaked enemy outside combat.
    void addSheepIntruder(const Enemy& enemy);
    // Stamp original lifetimes without consuming gameplay random state.
    void addEffect(Effect effect);
    void addBeam(Beam beam);
    // Remove only expired cosmetic events; gameplay and saved RNG are untouched.
    void pruneEffects();
    // Recompute the original eight-neighbor distance field towards the sheep gate.
    void updateDistances();
    // Calculate an unchanged route field against a live or trial occupancy grid.
    std::vector<float> calculateDistances(bool flying, const std::vector<int>& grid) const;
    // Check corner restrictions and the original blocked-cell bit mask.
    bool traversable(int x, int y, int nextX, int nextY) const;
    // Release one enemy from the current UFO at the configured frequency.
    void spawnEnemy(Ufo& ufo);
    // Advance the recovered per-UFO landing and release sequence.
    void updateUfos();
    // Follow the original ground or immutable-terrain flying distance field.
    void moveEnemies();
    // Retain a valid target, otherwise choose the enemy closest to the sheep gate.
    void attack();
    // Apply integer damage through original armor and enqueue splits after each attack batch.
    void damage(Enemy& enemy, const Tower& tower, int raw);
    // Apply the shared linear splash or forward-line rules to a stable enemy batch.
    void radiusDamage(float x, float y, float radius, int raw, const Tower& tower);
    void lineDamage(float x, float y, float tx, float ty, float length, float radius, int raw, const Tower& tower);
    // Advance moving tracking and ballistic projectiles before enemy movement.
    void updateProjectiles();
    // Fire one recovered weapon family with its own hit and effect semantics.
    void fire(Tower& tower, Enemy* target);
    // Recalculate additive support-tower boosts at the original interval.
    void updateBoosts();
    // Produce the original synchronized random float used by combat and splitting.
    float randomFloat();
    // Merge a reduction factor and duration with the original slow precedence rule.
    void slow(Enemy& enemy, float factor, int duration);
    // Pull an enemy only when the original one-unit navigation probe is unblocked.
    void attract(Enemy& enemy, Vec2 direction, float amount);
    // Publish children between damage batches while preserving live enemy references.
    void releaseChildren();
    // Reward an early wave only when its last counted removal was a kill.
    void completeWaveEnemy(int wave, bool killed);
    // Resolve a tower muzzle in world coordinates from its original offsets.
    Vec3 muzzle(const Tower& tower, int index = 0) const;
    // Calculate original ballistic launch speed and predicted planar aim.
    float ballisticLaunch(const Tower& tower, const Enemy& target, Vec2& aim) const;
    // Verify routes from actual pad-offset spawns and living ground enemies.
    bool routesRemain(const std::vector<float>& distances) const;
    // Allocate an enemy with original wave parameters at a specified world point.
    Enemy makeEnemy(int wave, float x, float y);
    // Allocate visual slots without imposing the original entity limit on combat.
    int allocateEnemySlot();
    // Release a lookup slot immediately when its enemy leaks or corpse expires.
    void releaseEnemySlot(int slot);
    // Advance retained corpse occupancy with the original float32 countdown.
    void updateEnemySlotHolds();
    // Refresh the original frozen-frame timestamp on every applied stun.
    void stun(Enemy& enemy, int duration);

    // Dead combat entities retain frozen visual state independently of combat lifetime.
    std::vector<Sheep> sheep_;
    // Cosmetic target still belongs to the reservation event and survives session continuation.
    std::optional<Vec2> lastAbductionFocus_;
    std::vector<SheepIntruder> intruders_;
    std::vector<Cell> sheepField_;
    int animalClockMs_ = 0;
    std::array<bool,NativeEnemySlotCount> enemySlots_{};
    std::vector<EnemyCorpse> enemySlotHolds_;
    int nextEnemySlot_ = 0;
    std::vector<int> grid_;
    std::vector<float> distances_, flyingDistances_;
    std::vector<Cell> pads_, goals_;
    int nextId_ = 1, lastWaveAt_ = 0, nextPad_ = 0;
    std::uint32_t randomState_ = 1;
    // Production uses the original microsecond seed; offline checks default to one.
    std::uint32_t sessionSeed_ = 1;
    std::mt19937 randomEngine_{1};
    std::filesystem::path assets_;
    std::vector<Enemy> pendingChildren_;
    int lastBoostAt_ = 0;
    // Negative means deterministic offline verification rather than a supplied wall clock.
    double effectClock_ = -1;
    nlohmann::json endlessSource_;
    std::uint32_t endlessSeedOffset_ = 0;
    std::map<int,WaveCounter> waveCounters_;
};

// Exercise real money, navigation, waves, and frame-rate independent match outcomes.
int runSimulationChecks(const std::filesystem::path& assets, const std::filesystem::path& output);
// Check packaged modes, distinct weapon families, and persisted simulation continuity.
int runCoverageChecks(const std::filesystem::path& assets, const std::filesystem::path& output);

} // namespace tmreborn
