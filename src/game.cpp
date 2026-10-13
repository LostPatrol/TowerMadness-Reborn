// Fixed-step navigation, original mode loading, and finite or endless wave progression.
#include "game.hpp"
#include "catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>

namespace tmreborn {
namespace {
// Preserve the neighbor order used by MAP_getNextMoveTarget.
constexpr std::array<Cell, 8> Neighbors{{{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}};
// Single-precision diagonal cost from the original distance-field instructions.
constexpr float DiagonalCost = 1.4142135f;
// An unreachable route is separate from the legitimate zero distance at the gate.
constexpr float Unreachable = std::numeric_limits<float>::infinity();
// The original enemy update clamps the decaying beam heat multiplier to one.
constexpr float HeatDecayPerSecond = 0.05f;
// Original GAME_update automatically tries to dispatch another wave after 20 s.
constexpr int WaveIntervalMs = 20000;
// The original scoreboard awards forty points for every surviving sheep.
constexpr int SheepScore = 40;
// Desktop leak cue lifetime is a visual approximation, independent of sheep loss.
constexpr int SheepLeakVisualMs = 750;
// Desktop leak cue size is expressed in world units and never changes gate detection.
constexpr float SheepLeakVisualRadius = 0.9f;
// Enemy hit-point presentation shares the original quarter-unit ground offset.
constexpr float EnemyVisualGroundOffset = 0.25f;
// Original UFO approach divides the square root of its remaining distance by 400.
constexpr float UfoApproachDivisor = 400.0f;
// The original approach keeps this minimum movement per millisecond.
constexpr float UfoApproachMinimum = 0.001f;
// Original departure rises vertically towards this height before becoming idle.
constexpr float UfoDepartureHeight = 50.0f;
// Original departure step is elapsed milliseconds times 1.4 / remaining height.
constexpr float UfoDepartureFactor = 1.4f;
// Original departure finishes below this remaining-height threshold.
constexpr float UfoDepartureTolerance = 0.01f;
// ENEMIES_draw uses reciprocal wave speed times this duration before truncation.
constexpr float WalkFrameBaseMs = 150.0f;
// Recovered float32 angle conversion, turn coefficient, and dead zone.
constexpr float EnemyDegreesPerRadian = 57.295784f, EnemyTurnPerMs = 0.005f, EnemyTurnDeadZone = 2.0f;

// Read one normalized configuration with a useful filename on parse failures.
nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open game data: " + path.string());
    try { return nlohmann::json::parse(stream); }
    catch (const std::exception& error) { throw std::runtime_error(path.string() + ": " + error.what()); }
}

// Convert configuration coordinate pairs without changing their navigation units.
std::vector<Cell> readCells(const nlohmann::json& values) {
    std::vector<Cell> result;
    for (const auto& value : values) result.push_back({value[0].get<int>(),value[1].get<int>()});
    return result;
}

} // namespace

// Load the selected complete original level and initialize its playable state.
Game::Game(const std::filesystem::path& assets, const std::string& selectedMode) : assets_(assets) { loadMode(selectedMode); }

// Resolve a complete packaged level from its original dependency references.
void Game::loadMode(const std::string& selectedMode) {
    modeId = selectedMode;
    if (modeId.starts_with("Endless:") || modeId.starts_with("Sandbox:")) {
        const auto separator = modeId.find(':',modeId.find(':') + 1);
        if (separator == std::string::npos) throw std::runtime_error("Invalid endless mode selection.");
        const std::string waveSet = modeId.starts_with("Sandbox:") ? "Endless_Sandbox" : modeId.substr(modeId.find(':') + 1,separator - modeId.find(':') - 1);
        mode = makeEndlessMode(waveSet,modeId.substr(separator + 1));
    } else mode = readJson(assets_ / ("data/" + modeId + ".gamemode.json"));
    map = readJson(assets_ / ("data/" + mode.at("map").get<std::string>() + ".map.json"));
    technology = readJson(assets_ / ("data/" + mode.at("tech").get<std::string>() + ".tech.json"));
    waves = readJson(assets_ / ("data/" + mode.at("waves").get<std::string>() + ".waves.json"));
    endless = !waves.is_array();
    endlessSource_ = endless ? waves : nlohmann::json{};
    if (endless) {
        endlessSeedOffset_ = endlessModeSeedOffset(modeId.starts_with("Sandbox:") ? "Endless_Sandbox" : mode.at("waves").get<std::string>(),mode.at("map").get<std::string>());
        waves = nlohmann::json::array();
    }
    width = map.at("width").get<int>() * 2;
    height = map.at("height").get<int>() * 2;
    pads_ = readCells(map.at("pads"));
    goals_ = readCells(map.at("entrance"));
    if (pads_.empty() || goals_.empty()) throw std::runtime_error("The level needs UFO pads and a sheep gate.");
    restart();
}

// Use the shipped palettes, filtering technology-disabled root towers.
std::vector<std::string> Game::baseTowers() const {
    std::vector<std::string> result;
    for (const char* palette : {"basetowersenergy","basetowersballistic","basetowerslightning","basetowersspecial"})
        for (const auto& entry : technology.at(palette)) {
            const std::string name = entry.get<std::string>();
            if (!technology.at("towers").at(name).value("disabled",false)) result.push_back(name);
        }
    return result;
}

// Reset the simulation without changing the original level definitions.
void Game::restart() {
    if (endless) waves = nlohmann::json::array();
    money = mode.at("money").get<int>(); lives = mode.at("lifes").get<int>();
    enemies.clear(); towers.clear(); beams.clear(); projectiles.clear(); effects.clear(); pendingChildren_.clear();
    enemySlots_.fill(false); enemySlotHolds_.clear(); nextEnemySlot_ = 0;
    timeMs = wavesSent = killed = leaked = spawned = 0;
    // ENEMIES_alloc starts lastPadIndex at zero and searches from the next pad.
    nextId_ = 1; lastWaveAt_ = 0; nextPad_ = 1 % static_cast<int>(pads_.size());
    randomState_ = 1; randomEngine_.seed(randomState_); score = bonus = 0; waveCounters_.clear(); lastBoostAt_ = 0; paused = false; outcome = Outcome::Preparing;
    grid_.resize(width * height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int terrain = map.at("terrain")[x / 2 + (y / 2) * (width / 2)].get<int>();
        grid_[x + y * width] = terrain == 0 ? 2 : terrain == 1 ? map.value("fixedpath",0) : 3;
    }
    updateDistances();
    ufos.clear();
    for (const Cell pad : pads_) { Ufo ufo; ufo.pad = pad; ufos.push_back(ufo); }
    message = "Choose a tower, build on grass, then send a wave.";
}

// Honor the original blocked-state mask and prevent diagonal corner cutting.
bool Game::traversable(int x, int y, int nextX, int nextY) const {
    const auto blocked = [this](int cx, int cy) {
        return cx < 0 || cy < 0 || cx >= width || cy >= height || (grid_[cx + cy * width] & 9) == 1;
    };
    if (blocked(nextX,nextY)) return false;
    return nextX == x || nextY == y || (!blocked(x,nextY) && !blocked(nextX,y));
}

// Compute both tower-aware ground and tower-independent flying distance fields.
void Game::updateDistances() {
    distances_ = calculateDistances(false,grid_);
    flyingDistances_ = calculateDistances(true,grid_);
}

// Keep trial and live navigation identical, including completed bait targets.
std::vector<float> Game::calculateDistances(bool flying, const std::vector<int>& grid) const {
    // A completed bait replaces the sheep gate with its four passable target cells.
    std::vector<Cell> targets = goals_;
    if (!map.value("fixedpath",0)) for (const auto& tower : towers)
        if (!tower.selling && !tower.consumed && timeMs > tower.readyAt && technology.at("towers").at(tower.type).value("isbait",0))
            targets = {{tower.x,tower.y},{tower.x + 1,tower.y},{tower.x,tower.y + 1},{tower.x + 1,tower.y + 1}};
    std::vector<float> values(width * height,Unreachable);
    using Entry = std::pair<float,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    const auto allowed = [this,flying,&grid](int x,int y,int nx,int ny) {
        const auto blocked = [this,flying,&grid](int cx,int cy) {
            if (cx < 0 || cy < 0 || cx >= width || cy >= height) return true;
            const int terrain = map.at("terrain")[cx / 2 + (cy / 2) * (width / 2)].get<int>();
            return flying ? terrain != 0 && terrain != 1 : (grid[cx + cy * width] & 9) == 1;
        };
        return !blocked(nx,ny) && (x == nx || y == ny || (!blocked(x,ny) && !blocked(nx,y)));
    };
    for (const Cell goal : targets) {
        values[goal.x + goal.y * width] = 0;
        queue.push({0.0f,goal.x + goal.y * width});
    }
    while (!queue.empty()) {
        const auto [distance,index] = queue.top(); queue.pop();
        if (distance != values[index]) continue;
        const int x = index % width,y = index / width;
        for (const Cell delta : Neighbors) {
            const int nx = x + delta.x,ny = y + delta.y;
            if (!allowed(x,y,nx,ny)) continue;
            const int next = nx + ny * width;
            const float candidate = distance + (delta.x && delta.y ? DiagonalCost : 1.0f);
            if (candidate < values[next]) { values[next] = candidate; queue.push({candidate,next}); }
        }
    }
    return values;
}

// Return infinity for invalid cells instead of wrapping indices across rows.
float Game::distanceAt(int x, int y) const {
    return x < 0 || y < 0 || x >= width || y >= height ? Unreachable : distances_[x + y * width];
}

// Inspect the original four-cell construction footprint and tower occupancy.
bool Game::canBuild(int x, int y) const {
    if (x < 0 || y < 0 || x + 1 >= width || y + 1 >= height) return false;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
        if ((grid_[x + dx + (y + dy) * width] & 2) != 0 || towerAt(x + dx,y + dy)) return false;
    return true;
}

// Keep every landing pad and live ground enemy connected to the sheep gate.
bool Game::routesRemain(const std::vector<float>& distances) const {
    // Invalid enemy coordinates remain unreachable, as with distanceAt.
    const auto reachable = [this,&distances](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && std::isfinite(distances[x + y * width]);
    };
    for (const Cell pad : pads_) if (!reachable(pad.x + 2,pad.y + 2)) return false;
    for (const auto& enemy : enemies)
        if (enemy.alive && !waveFor(enemy).value("flying",false) && !reachable(static_cast<int>(enemy.x),static_cast<int>(enemy.y))) return false;
    return true;
}

// Inspect original construction rules without touching the live grid or message.
std::string Game::buildBlockReason(int x, int y, const std::string& type) const {
    if (outcome == Outcome::Won || outcome == Outcome::Lost) return "Restart to build a new defense.";
    if (!technology.at("towers").contains(type)) return "Unknown tower.";
    const auto& definition = technology.at("towers").at(type);
    if (definition.value("disabled",false)) return "This tower is disabled in the selected technology.";
    if (money < definition.at("cost").get<int>()) return "Not enough gold.";
    if (x < 0 || y < 0 || x + 1 >= width || y + 1 >= height) return "The tower footprint is outside the map.";
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        if ((grid_[x + dx + (y + dy) * width] & 2) != 0) return "A tower needs four buildable grass cells (2 x 2).";
        if (towerAt(x + dx,y + dy)) return "The tower footprint is already occupied.";
    }
    if (!map.value("fixedpath",0)) {
        auto trialGrid = grid_;
        // Original build checks the blocking footprint before granting bait passability.
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) trialGrid[x + dx + (y + dy) * width] |= 1;
        if (!routesRemain(calculateDistances(false,trialGrid))) return "Build refused: this would block the route to the sheep.";
    }
    return {};
}

// Apply only a legal placement after its read-only original-rule query succeeds.
bool Game::build(int x, int y, const std::string& type) {
    const std::string reason = buildBlockReason(x,y,type);
    if (!reason.empty()) { message = reason; return false; }
    const auto& definition = technology.at("towers").at(type);
    const int cost = definition.at("cost").get<int>();
    // Fixed-path grass already blocks navigation; original towers do not alter that grid.
    const bool fixedPath = map.value("fixedpath",0) != 0;
    if (!fixedPath) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) grid_[x + dx + (y + dy) * width] |= 1;
    updateDistances();
    Tower tower; tower.id = nextId_++; tower.x = x; tower.y = y; tower.type = type; tower.refund = cost;
    tower.readyAt = timeMs + (wavesSent == 0 ? 500 : definition.value("buildtime",0));
    tower.appearanceStartedAt = timeMs;
    tower.nextShotAt = tower.readyAt;
    towers.push_back(tower); money -= cost; message = "Building " + type + ".";
    if (!fixedPath && definition.value("isbait",0)) {
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) grid_[x + dx + (y + dy) * width] |= 8;
        updateDistances();
    }
    return true;
}

// Locate a tower by its occupied footprint, independent of its visual mesh bounds.
const Tower* Game::towerAt(int x, int y) const {
    for (const auto& tower : towers) if (x >= tower.x && x < tower.x + 2 && y >= tower.y && y < tower.y + 2) return &tower;
    return nullptr;
}

// Share terminal, readiness, technology, branch and affordability rules with the UI.
std::string Game::upgradeBlockReason(int id, const std::string& nextType) const {
    if (outcome == Outcome::Won || outcome == Outcome::Lost) return "Restart to change the defense.";
    const auto tower = std::find_if(towers.begin(),towers.end(),[id](const Tower& value) { return value.id == id; });
    if (tower == towers.end()) return "Tower no longer exists.";
    if (tower->selling) return "Tower is being dismantled.";
    if (timeMs <= tower->readyAt) return "Wait for construction to finish.";
    const auto& upgrades = technology.at("towers").at(tower->type).at("upgrades");
    if (upgrades.empty()) return "This tower is fully upgraded.";
    const std::string next = nextType.empty() ? upgrades[0].get<std::string>() : nextType;
    if (std::find(upgrades.begin(),upgrades.end(),next) == upgrades.end()) return "Invalid upgrade branch.";
    const auto& definition = technology.at("towers").at(next);
    if (definition.value("disabled",false)) return "This upgrade is disabled in the selected technology.";
    if (money < definition.at("cost").get<int>()) return "Not enough gold for " + next + ".";
    return {};
}

// Use original upgrade costs and keep accumulated investment for later sales.
bool Game::upgrade(int id, const std::string& nextType) {
    const std::string reason = upgradeBlockReason(id,nextType);
    if (!reason.empty()) { message = reason; return false; }
    auto tower = std::find_if(towers.begin(),towers.end(),[id](const Tower& value) { return value.id == id; });
    const auto& upgrades = technology.at("towers").at(tower->type).at("upgrades");
    const std::string next = nextType.empty() ? upgrades[0].get<std::string>() : nextType;
    const auto& definition = technology.at("towers").at(next);
    const int cost = definition.at("cost").get<int>();
    tower->previousType = tower->type; tower->appearanceStartedAt = timeMs;
    money -= cost; tower->refund += cost; tower->type = next; tower->target = -1;
    tower->readyAt = timeMs + (wavesSent == 0 ? 500 : definition.value("buildtime",0));
    tower->nextShotAt = tower->readyAt; message = "Upgrading to " + next + ".";
    return true;
}

// Share terminal and construction-state sale rules with the selected-tower controls.
std::string Game::sellBlockReason(int id) const {
    if (outcome == Outcome::Won || outcome == Outcome::Lost) return "Restart to change the defense.";
    const auto tower = std::find_if(towers.begin(),towers.end(),[id](const Tower& value) { return value.id == id; });
    if (tower == towers.end()) return "Tower no longer exists.";
    if (tower->selling) return "Tower is being dismantled.";
    if (timeMs <= tower->readyAt) return "Wait for construction to finish.";
    return {};
}

// Preserve original float truncation and avoid discounting a pending refund twice.
int Game::sellRefund(int id) const {
    const auto tower = std::find_if(towers.begin(),towers.end(),[id](const Tower& value) { return value.id == id; });
    if (tower == towers.end()) return 0;
    return tower->selling || wavesSent == 0 ? tower->refund : static_cast<int>(tower->refund * 0.75f);
}

// Keep the footprint blocked until the original sale timer has completed.
bool Game::sell(int id) {
    const std::string reason = sellBlockReason(id);
    if (!reason.empty()) { message = reason; return false; }
    auto tower = std::find_if(towers.begin(),towers.end(),[id](const Tower& value) { return value.id == id; });
    tower->refund = sellRefund(id);
    tower->appearanceStartedAt = timeMs;
    tower->selling = true; tower->target = -1; tower->readyAt = timeMs + (wavesSent == 0 ? 500 : 3000);
    score -= std::min(score,10);
    message = "Selling tower; refund when dismantling finishes."; return true;
}

// Automatic dispatch occurs on the first fixed step strictly beyond twenty seconds.
int Game::nextWaveDelayMs() const {
    return std::max(0,WaveIntervalMs + SimulationStepMs - (timeMs - lastWaveAt_));
}

// Reserve the original whole-second early bonus; the opening wave has no bonus.
int Game::earlyWaveBonus() const {
    return wavesSent == 0 ? 0 : std::max(0,WaveIntervalMs / 1000 - (timeMs - lastWaveAt_) / 1000);
}

// Resolve the unchanged original wave parameters for a live or split enemy.
const nlohmann::json& Game::waveFor(const Enemy& enemy) const { return waves.at(enemy.wave); }

// Preserve float32 duration arithmetic and the original generation-one frame list.
int Game::walkFrame(const Enemy& enemy) const {
    const auto& wave = waveFor(enemy);
    const auto& frames = enemy.generation == 1 && wave.contains("subwalk") && !wave.at("subwalk").empty()
        ? wave.at("subwalk") : wave.at("walk");
    if (frames.empty()) return 0;
    const float speed = wave.at("speed").get<float>();
    if (speed <= 0) return 0;
    const int baseMs = static_cast<int>((1.0f / speed) * WalkFrameBaseMs);
    const int periodMs = static_cast<int>(wave.value("walkanimfactor",1.0f) * static_cast<float>(baseMs));
    // Slot exhaustion affects only animation variation, never combat availability.
    const int frameMs = std::max(1,std::max(0,enemy.nativeSlot) + periodMs);
    const int sampleMs = !enemy.alive ? enemy.deathFrameTimeMs : enemy.stunMs > 0 ? enemy.stunFrameTimeMs : timeMs;
    return std::max(0,sampleMs / frameMs) % static_cast<int>(frames.size());
}

// Keep corpse presentation read-only and outside all active-enemy iteration.
const std::vector<EnemyCorpse>& Game::corpses() const { return enemySlotHolds_; }

// Search the original cyclic allocator while keeping visual capacity non-blocking.
int Game::allocateEnemySlot() {
    for (int attempt = 0; attempt < NativeEnemySlotCount; ++attempt) {
        const int slot = (nextEnemySlot_ + attempt) % NativeEnemySlotCount;
        if (enemySlots_[slot]) continue;
        enemySlots_[slot] = true; nextEnemySlot_ = slot + 1;
        return slot;
    }
    return -1;
}

// Ignore the visual fallback slot used when all native slots are occupied.
void Game::releaseEnemySlot(int slot) {
    if (slot >= 0 && slot < NativeEnemySlotCount) enemySlots_[slot] = false;
}

// Check before subtraction so the last positive corpse tick remains occupied.
void Game::updateEnemySlotHolds() {
    std::erase_if(enemySlotHolds_,[this](EnemyCorpse& hold) {
        if (hold.remainingSeconds > 0) {
            hold.remainingSeconds -= static_cast<float>(SimulationStepMs) / 1000.0f;
            return false;
        }
        releaseEnemySlot(hold.slot);
        return true;
    });
}

// Repeat and weaker stuns still resample the original frozen animation time.
void Game::stun(Enemy& enemy, int duration) {
    enemy.stunMs = std::max(enemy.stunMs,duration);
    enemy.stunFrameTimeMs = timeMs;
}

// Record actual live state without adjusting results to satisfy a test expectation.
nlohmann::json Game::snapshot() const {
    return {{"mode",modeId},{"score",score},{"bonus",bonus},{"finalScore",finalScore()},{"endless",endless},{"timeMs",timeMs},{"money",money},{"lives",lives},{"wavesSent",wavesSent},{"totalWaves",waves.size()},
            {"spawned",spawned},{"killed",killed},{"leaked",leaked},{"activeEnemies",std::count_if(enemies.begin(),enemies.end(),[](const Enemy& e) { return e.alive; })},
            {"towers",towers.size()},{"outcome",static_cast<int>(outcome)}};
}

// Preserve the original separate score and bonus buckets until the scoreboard is shown.
int Game::finalScore() const { return score + bonus + lives * SheepScore; }

// Original completion counters assume three objects for any nonzero split configuration.
void Game::completeWaveEnemy(int wave, bool killedEnemy) {
    const auto found = waveCounters_.find(wave);
    if (found == waveCounters_.end()) return;
    if (--found->second.remaining > 0) return;
    if (killedEnemy) bonus += found->second.bonus;
    waveCounters_.erase(found);
}

// Dispatch waves across idle UFOs while preserving every original wave entry.
bool Game::sendWave() {
    if (outcome == Outcome::Won || outcome == Outcome::Lost || (!endless && wavesSent >= static_cast<int>(waves.size()))) return false;
    for (int offset = 0; offset < static_cast<int>(ufos.size()); ++offset) {
        const int index = (nextPad_ + offset) % static_cast<int>(ufos.size());
        auto& ufo = ufos[index];
        if (ufo.state != Ufo::State::Idle) continue;
        if (endless && wavesSent >= static_cast<int>(waves.size())) waves.push_back(generateEndlessWave(endlessSource_,wavesSent,endlessSeedOffset_));
        const int earlyBonus = earlyWaveBonus();
        ufo.wave = wavesSent++; ufo.remaining = waves.at(ufo.wave).at("count").get<int>();
        waveCounters_[ufo.wave] = {ufo.remaining * (waves.at(ufo.wave).value("spawn",0) ? 3 : 1),earlyBonus};
        ufo.x = width * 0.5f; ufo.y = height + 20.0f; ufo.z = 10;
        ufo.state = Ufo::State::Landing; nextPad_ = (index + 1) % static_cast<int>(ufos.size());
        lastWaveAt_ = timeMs; outcome = Outcome::Running; message = "Wave " + std::to_string(wavesSent) + " incoming.";
        return true;
    }
    message = "Both UFOs are busy. The next wave will wait for a free landing pad.";
    return false;
}

// Allocate an original integer-health enemy without changing its configured speed.
Enemy Game::makeEnemy(int wave, float x, float y) {
    Enemy enemy; enemy.id = nextId_++; enemy.wave = wave; enemy.x = x; enemy.y = y;
    enemy.nativeSlot = allocateEnemySlot();
    enemy.previous = {x,y}; enemy.lastRegenAt = timeMs;
    enemy.health = enemy.maxHealth = waves.at(wave).at("health").get<std::int64_t>();
    enemy.model = waves.at(wave).at("model").get<std::string>();
    enemy.moveTarget = {static_cast<int>(x),static_cast<int>(y)};
    return enemy;
}

// Release at pad + (2,2), as ENEMIES_sendNextUFO initializes the original chain.
void Game::spawnEnemy(Ufo& ufo) {
    enemies.push_back(makeEnemy(ufo.wave,ufo.pad.x + 2.0f,ufo.pad.y + 2.0f));
    ++spawned; --ufo.remaining;
}

// Move each UFO using its original nonlinear approach and strict release interval.
void Game::updateUfos() {
    for (auto& ufo : ufos) {
        if (ufo.state == Ufo::State::Idle) continue;
        if (ufo.state == Ufo::State::Releasing) {
            const int frequency = waves.at(ufo.wave).at("frequency").get<int>();
            while (ufo.remaining > 0 && timeMs - ufo.lastReleaseAt > frequency) {
                spawnEnemy(ufo); ufo.lastReleaseAt += frequency;
            }
            if (ufo.remaining == 0) ufo.state = Ufo::State::Leaving;
            continue;
        }
        if (ufo.state == Ufo::State::Leaving) {
            const float remainingHeight = UfoDepartureHeight - ufo.z;
            if (remainingHeight < UfoDepartureTolerance) {
                ufo.x = ufo.pad.x + 2.0f; ufo.y = ufo.pad.y + 2.0f; ufo.z = 3.0f;
                ufo.state = Ufo::State::Idle;
            } else {
                ufo.z += SimulationStepMs * UfoDepartureFactor / remainingHeight;
            }
            continue;
        }
        const float tx = ufo.pad.x + 2.0f;
        const float ty = ufo.pad.y + 2.0f;
        const float tz = 3.0f;
        const float dx = tx - ufo.x,dy = ty - ufo.y,dz = tz - ufo.z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float step = (std::sqrt(length) / UfoApproachDivisor + UfoApproachMinimum) * SimulationStepMs;
        if (length < step) {
            ufo.x = tx; ufo.y = ty; ufo.z = tz;
            ufo.state = Ufo::State::Releasing;
            ufo.lastReleaseAt = timeMs;
        } else { ufo.x += dx / length * step; ufo.y += dy / length * step; ufo.z += dz / length * step; }
    }
}

// Follow the original center-to-center movement and cap each tick at its target.
void Game::moveEnemies() {
    const float seconds = SimulationStepMs / 1000.0f;
    updateEnemySlotHolds();
    for (auto& enemy : enemies) {
        if (!enemy.alive) continue;
        const auto& wave = waveFor(enemy);
        // Status activity is decided before countdown, including the step crossing zero.
        const bool slowed = enemy.slowMs > 0,stunned = enemy.stunMs > 0;
        // Regeneration uses whole original HP and a strict one-second boundary.
        if (timeMs - enemy.lastRegenAt > 1000) {
            const auto regeneration = wave.value("regen",std::int64_t{0});
            if (slowed) {
                const float increment = static_cast<float>(regeneration) * (1 - enemy.slowFactor);
                enemy.health = std::min(enemy.maxHealth,static_cast<std::int64_t>(static_cast<float>(enemy.health) + increment));
            } else enemy.health = std::min(enemy.maxHealth,enemy.health + regeneration);
            enemy.lastRegenAt = timeMs;
        }
        enemy.heat = std::max(1.0f,enemy.heat * (1 - HeatDecayPerSecond * seconds));
        if (slowed) enemy.slowMs -= SimulationStepMs;
        if (stunned) enemy.stunMs -= SimulationStepMs;
        const bool flying = wave.value("flying",false);
        const auto& field = flying ? flyingDistances_ : distances_;
        int x = static_cast<int>(enemy.x),y = static_cast<int>(enemy.y);
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        const bool sheepGate = std::any_of(goals_.begin(),goals_.end(),[x,y](Cell goal) { return goal.x == x && goal.y == y; });
        if (sheepGate) {
            completeWaveEnemy(enemy.wave,false);
            enemy.alive = false; ++leaked; --lives;
            releaseEnemySlot(enemy.nativeSlot);
            addEffect({{enemy.x,enemy.y,wave.value("height",0.0f) + wave.value("offset",0.0f) + EnemyVisualGroundOffset},
                SheepLeakVisualRadius,timeMs + SheepLeakVisualMs,"sheepLeak",timeMs});
            if (lives <= 0) { lives = 0; outcome = Outcome::Lost; message = "The sheep are gone. Restart to try a new defense."; }
            continue;
        }
        if (field[x + y * width] == 0) continue;
        // MAP_getNextMoveTarget evaluates the current cell's neighbors every tick.
        {
            float shortest = Unreachable; Cell next{x,y};
            for (const Cell delta : Neighbors) {
                const int nx = x + delta.x,ny = y + delta.y;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                // Flying uses the immutable terrain field and can pass over towers.
                const auto terrainBlocked = [this](int cx,int cy) {
                    const int value = map.at("terrain")[cx / 2 + (cy / 2) * (width / 2)].get<int>();
                    return value != 0 && value != 1;
                };
                const bool allowed = flying ? !terrainBlocked(nx,ny) && (nx == x || ny == y || (!terrainBlocked(x,ny) && !terrainBlocked(nx,y))) : traversable(x,y,nx,ny);
                const float candidate = field[nx + ny * width] + (delta.x && delta.y ? DiagonalCost : 1.0f);
                if (allowed && candidate < shortest) { shortest = candidate; next = {nx,ny}; }
            }
            enemy.moveTarget = next;
        }
        const float dx = enemy.moveTarget.x + 0.5f - enemy.x,dy = enemy.moveTarget.y + 0.5f - enemy.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0) {
            const float velocity = wave.at("speed").get<float>() * (slowed ? 1 - enemy.slowFactor : 1);
            const float step = stunned ? 0 : std::min(length,velocity * seconds);
            enemy.previous = {enemy.x,enemy.y};
            enemy.x += dx / length * step; enemy.y += dy / length * step; enemy.traveled += step;
            enemy.heading = std::atan2(dy,dx);
            if (!stunned) {
                const float target = std::atan2(dy / length,dx / length) * EnemyDegreesPerRadian;
                float difference = target - enemy.visualHeadingDegrees;
                if (difference > 180.0f) difference -= 360.0f;
                else if (difference < -180.0f) difference += 360.0f;
                if (std::abs(difference) > EnemyTurnDeadZone)
                    enemy.visualHeadingDegrees += (static_cast<float>(SimulationStepMs) * EnemyTurnPerMs) * difference;
            }
        }
    }
}


// Read only the presentation clock; it never contributes to simulation state.
double Game::effectClockSeconds() const {
    return effectClock_ >= 0 ? effectClock_ : timeMs * 0.001;
}

// Stamp a visual event using the original realtime lifetime, retaining game metadata.
void Game::addEffect(Effect effect) {
    effect.timing.bornSeconds = effectClockSeconds();
    effect.timing.expiresSeconds = effect.timing.bornSeconds + (effect.expiresAt - effect.bornAt) * 0.001;
    effects.push_back(std::move(effect));
}

// Beam lifetimes share the same cosmetic clock as other native effect callbacks.
void Game::addBeam(Beam beam) {
    beam.timing.bornSeconds = effectClockSeconds();
    beam.timing.expiresSeconds = beam.timing.bornSeconds + (beam.expiresAt - beam.bornAt) * 0.001;
    beams.push_back(std::move(beam));
}

// Expire native visual records independently of simulation pause and playback speed.
void Game::pruneEffects() {
    const double now = effectClockSeconds();
    std::erase_if(beams,[&](const Beam& beam) {
        return effectClock_ >= 0 && beam.timing.expiresSeconds >= 0 ? now >= beam.timing.expiresSeconds : timeMs >= beam.expiresAt;
    });
    std::erase_if(effects,[&](const Effect& effect) {
        return effectClock_ >= 0 && effect.timing.expiresSeconds >= 0 ? now >= effect.timing.expiresSeconds : timeMs >= effect.expiresAt;
    });
}

// The desktop supplies monotonic real seconds before simulation and while paused.
void Game::advanceEffects(double realtimeSeconds) {
    if (!std::isfinite(realtimeSeconds) || realtimeSeconds < 0) return;
    // Attaching a real clock preserves any already-created offline event's current age.
    if (effectClock_ < 0) {
        const double shift = realtimeSeconds - effectClockSeconds();
        for (auto& beam : beams) { beam.timing.bornSeconds += shift; beam.timing.expiresSeconds += shift; }
        for (auto& effect : effects) { effect.timing.bornSeconds += shift; effect.timing.expiresSeconds += shift; }
    }
    effectClock_ = realtimeSeconds;
    pruneEffects();
}

// Apply fixed-step updates independently from drawing frequency or wall-clock speed.
void Game::tick() {
    soundEvents.clear();
    if (paused || outcome == Outcome::Won || outcome == Outcome::Lost) return;
    timeMs += SimulationStepMs;
    pruneEffects();
    for (const auto& tower : towers) if (tower.selling && timeMs > tower.readyAt) {
        money += tower.refund;
        if (!map.value("fixedpath",0)) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) grid_[tower.x + dx + (tower.y + dy) * width] &= ~9;
    }
    const auto removed = std::erase_if(towers,[this](const Tower& tower) { return tower.selling && timeMs > tower.readyAt; });
    if (removed) updateDistances();
    for (const auto& tower : towers) if (technology.at("towers").at(tower.type).value("isbait",0) && timeMs == tower.readyAt + SimulationStepMs) updateDistances();
    if (outcome == Outcome::Preparing) return;
    updateUfos(); updateProjectiles(); moveEnemies();
    // GAME_timestep stops tower attacks once loss is recorded in enemy movement.
    if (outcome != Outcome::Lost) attack();
    for (const auto& tower : towers) if (tower.consumed && !map.value("fixedpath",0))
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) grid_[tower.x + dx + (tower.y + dy) * width] &= ~9;
    if (std::erase_if(towers,[](const Tower& tower) { return tower.consumed; })) updateDistances();
    enemies.insert(enemies.end(),pendingChildren_.begin(),pendingChildren_.end()); pendingChildren_.clear();
    std::erase_if(enemies,[](const Enemy& enemy) { return !enemy.alive; });
    if (outcome == Outcome::Lost) return;
    if ((endless || wavesSent < static_cast<int>(waves.size())) && timeMs - lastWaveAt_ > WaveIntervalMs) sendWave();
    const bool allIdle = std::all_of(ufos.begin(),ufos.end(),[](const Ufo& ufo) { return ufo.state == Ufo::State::Idle; });
    if (!endless && wavesSent == static_cast<int>(waves.size()) && enemies.empty() && allIdle) {
        outcome = Outcome::Won; beams.clear(); message = "All original waves cleared. The sheep are safe!";
    }
}

} // namespace tmreborn
