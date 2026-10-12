// Data-driven single-player simulation reconstructed from the original ARM game.
#pragma once

#include <json.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tmreborn {

// The original GAME_alloc initializes the simulation interval to 25 milliseconds.
inline constexpr int SimulationStepMs = 25;

struct Cell { int x = 0, y = 0; };
struct Enemy {
    int id = 0, wave = 0, generation = 0;
    float x = 0, y = 0, heading = 0, traveled = 0, heat = 1;
    Cell moveTarget{};
    std::int64_t health = 0, maxHealth = 0;
    bool alive = true;
    std::string model;
};
struct Tower {
    int id = 0, x = 0, y = 0, target = -1, refund = 0;
    std::string type;
    int readyAt = 0, nextShotAt = 0;
    float heading = 0;
    bool selling = false;
};
struct Beam {
    float fromX = 0, fromY = 0, toX = 0, toY = 0, toZ = 0;
    int expiresAt = 0;
};
struct Ufo {
    // States follow landing, releasing, departure, and idle lifecycle phases.
    enum class State { Landing, Releasing, Leaving, Idle };
    State state = State::Idle;
    int wave = 0, remaining = 0, lastReleaseAt = 0;
    Cell pad{};
    float x = 0, y = 0, z = 0;
};
enum class Outcome { Preparing, Running, Won, Lost };

class Game {
public:
    // Load the original normalized map, technology, and complete wave definitions.
    explicit Game(const std::filesystem::path& assets);
    // Restore opening money, sheep, empty towers, and the original navigation grid.
    void restart();
    // Advance one original 25 ms tick unless paused or the match has ended.
    void tick();
    // Dispatch the next complete original wave when the current release is done.
    bool sendWave();
    // Occupy a 2x2 footprint only if all spawn points and live enemies keep a route.
    bool build(int x, int y, const std::string& type = "Kilowatt Laser");
    // Begin the configured beam-tower upgrade and charge its original target cost.
    bool upgrade(int id);
    // Remove a tower, rebuild routes, and refund the recovered sale amount.
    bool sell(int id);
    // Find an existing tower covering a selected navigation cell.
    const Tower* towerAt(int x, int y) const;
    // Inspect a 2x2 footprint without changing money or the live distance field.
    bool canBuild(int x, int y) const;
    // Return the shortest ground-route distance from a navigation cell.
    float distanceAt(int x, int y) const;
    // Resolve an original wave definition, including child-enemy appearance.
    const nlohmann::json& waveFor(const Enemy& enemy) const;
    // Return a stable summary used by regression checks and diagnostics.
    nlohmann::json snapshot() const;

    nlohmann::json map, technology, waves, mode;
    std::vector<Enemy> enemies;
    std::vector<Tower> towers;
    std::vector<Beam> beams;
    std::vector<Ufo> ufos;
    int width = 0, height = 0, money = 0, lives = 0, timeMs = 0;
    int wavesSent = 0, killed = 0, leaked = 0, spawned = 0;
    bool paused = false;
    Outcome outcome = Outcome::Preparing;
    std::string message;

private:
    // Recompute the original eight-neighbor distance field towards the sheep gate.
    void updateDistances();
    // Check corner restrictions and the original blocked-cell bit mask.
    bool traversable(int x, int y, int nextX, int nextY) const;
    // Release one enemy from the current UFO at the configured frequency.
    void spawnEnemy(Ufo& ufo);
    // Advance the recovered per-UFO landing and release sequence.
    void updateUfos();
    // Follow downhill navigation distances or a direct route for flying enemies.
    void moveEnemies();
    // Retain a valid target, otherwise choose the enemy closest to the sheep gate.
    void attack();
    // Apply beam damage, reward a kill, and enqueue recovered split enemies.
    void damage(Enemy& enemy, const Tower& tower);
    // Verify routes from every pad and living ground enemy after a trial placement.
    bool routesRemain() const;
    // Allocate an enemy with original wave parameters at a specified world point.
    Enemy makeEnemy(int wave, float x, float y);

    std::vector<int> grid_;
    std::vector<float> distances_, flyingDistances_;
    std::vector<Cell> pads_, goals_;
    int nextId_ = 1, lastWaveAt_ = 0, nextPad_ = 0;
    std::uint32_t randomState_ = 1;
};

// Exercise real money, navigation, waves, and frame-rate independent match outcomes.
int runSimulationChecks(const std::filesystem::path& assets, const std::filesystem::path& output);

} // namespace tmreborn
