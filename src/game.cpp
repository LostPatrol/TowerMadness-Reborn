// Fixed-step navigation, beam combat, and complete PlainPlane wave progression.
#include "game.hpp"

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
// Original turret yaw approaches its target with this factor per second.
constexpr float TurretTurnFactor = 12.5f;
// Original beam fire permits less than five degrees of angular error.
constexpr float FireAngleTolerance = 0.08726646f;
// Beam display lifetime is a desktop visual choice, separate from damage timing.
constexpr int BeamVisualMs = 125;
// Full circle for normalizing the turret's shortest turn direction.
constexpr float FullTurn = 6.2831853f;
// Desktop RNG is deterministic; matching the original GAME_rand remains future work.
constexpr std::uint32_t RandomMultiplier = 1664525u, RandomIncrement = 1013904223u;
// Using the upper 24 bits keeps random conversion within float precision.
constexpr float RandomScale = 16777216.0f;
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
// Original beam heat rises by this amount after each hit, up to twice the damage.
constexpr float BeamHeatPerHit = 0.05f;
// Original heat accumulation is capped at this multiplier.
constexpr float MaximumHeat = 2.0f;

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

// Measure ground-plane separation without introducing double precision into combat.
float separation(float x, float y, float nextX, float nextY) {
    const float dx = nextX - x, dy = nextY - y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

// Load the selected complete original level and initialize its playable state.
Game::Game(const std::filesystem::path& assets) {
    mode = readJson(assets / "data/PlainPlane.gamemode.json");
    map = readJson(assets / ("data/" + mode.at("map").get<std::string>() + ".map.json"));
    technology = readJson(assets / ("data/" + mode.at("tech").get<std::string>() + ".tech.json"));
    waves = readJson(assets / ("data/" + mode.at("waves").get<std::string>() + ".waves.json"));
    width = map.at("width").get<int>() * 2;
    height = map.at("height").get<int>() * 2;
    pads_ = readCells(map.at("pads"));
    goals_ = readCells(map.at("entrance"));
    if (pads_.empty() || goals_.empty()) throw std::runtime_error("The level needs UFO pads and a sheep gate.");
    restart();
}

// Reset the simulation without changing the original level definitions.
void Game::restart() {
    money = mode.at("money").get<int>(); lives = mode.at("lifes").get<int>();
    enemies.clear(); towers.clear(); beams.clear();
    timeMs = wavesSent = killed = leaked = spawned = 0;
    // ENEMIES_alloc starts lastPadIndex at zero and searches from the next pad.
    nextId_ = 1; lastWaveAt_ = 0; nextPad_ = 1 % static_cast<int>(pads_.size());
    randomState_ = 1; paused = false; outcome = Outcome::Preparing;
    grid_.resize(width * height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int terrain = map.at("terrain")[x / 2 + (y / 2) * (width / 2)].get<int>();
        grid_[x + y * width] = terrain == 0 ? 2 : terrain == 1 ? map.value("fixedpath",0) : 3;
    }
    updateDistances();
    ufos.clear();
    for (const Cell pad : pads_) { Ufo ufo; ufo.pad = pad; ufos.push_back(ufo); }
    message = "Build lasers, then send wave. Left click: build/select; U: upgrade; Del: sell.";
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
    const auto calculate = [this](bool flying) {
        std::vector<float> values(width * height,Unreachable);
        using Entry = std::pair<float,int>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
        const auto allowed = [this,flying](int x,int y,int nx,int ny) {
            const auto blocked = [this,flying](int cx,int cy) {
                if (cx < 0 || cy < 0 || cx >= width || cy >= height) return true;
                const int terrain = map.at("terrain")[cx / 2 + (cy / 2) * (width / 2)].get<int>();
                return flying ? terrain != 0 && terrain != 1 : (grid_[cx + cy * width] & 9) == 1;
            };
            return !blocked(nx,ny) && (x == nx || y == ny || (!blocked(x,ny) && !blocked(nx,y)));
        };
        for (const Cell goal : goals_) {
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
    };
    distances_ = calculate(false); flyingDistances_ = calculate(true);
}

// Return infinity for invalid cells instead of wrapping indices across rows.
float Game::distanceAt(int x, int y) const {
    return x < 0 || y < 0 || x >= width || y >= height ? Unreachable : distances_[x + y * width];
}

// Inspect the original four-cell construction footprint and tower occupancy.
bool Game::canBuild(int x, int y) const {
    if (x < 0 || y < 0 || x + 1 >= width || y + 1 >= height) return false;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
        if ((grid_[x + dx + (y + dy) * width] & 3) != 0) return false;
    return true;
}

// Keep every landing pad and live ground enemy connected to the sheep gate.
bool Game::routesRemain() const {
    for (const Cell pad : pads_) if (!std::isfinite(distanceAt(pad.x,pad.y))) return false;
    for (const auto& enemy : enemies)
        if (enemy.alive && !waveFor(enemy).value("flying",false) && !std::isfinite(distanceAt(static_cast<int>(enemy.x),static_cast<int>(enemy.y)))) return false;
    return true;
}

// Trial the actual footprint and revert it before charging an illegal placement.
bool Game::build(int x, int y, const std::string& type) {
    if (outcome == Outcome::Won || outcome == Outcome::Lost) { message = "Restart to build a new defense."; return false; }
    if (!technology.at("towers").contains(type)) { message = "Unknown tower."; return false; }
    const auto& definition = technology.at("towers").at(type);
    if (technology.at("weapons").at(definition.at("weapon").get<std::string>()).at("type") != "beam") { message = "Only the recovered laser family is available in this prototype."; return false; }
    const int cost = definition.at("cost").get<int>();
    if (money < cost) { message = "Not enough gold."; return false; }
    if (!canBuild(x,y)) { message = "A laser needs four free buildable cells (2 x 2)."; return false; }
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) grid_[x + dx + (y + dy) * width] |= 1;
    updateDistances();
    if (!routesRemain()) {
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) grid_[x + dx + (y + dy) * width] &= ~1;
        updateDistances(); message = "Build refused: this would block the route to the sheep."; return false;
    }
    Tower tower; tower.id = nextId_++; tower.x = x; tower.y = y; tower.type = type; tower.refund = cost;
    tower.readyAt = timeMs + (wavesSent == 0 ? 500 : definition.value("buildtime",0));
    tower.nextShotAt = tower.readyAt;
    towers.push_back(tower); money -= cost; message = "Building " + type + ".";
    return true;
}

// Locate a tower by its occupied footprint, independent of its visual mesh bounds.
const Tower* Game::towerAt(int x, int y) const {
    for (const auto& tower : towers) if (x >= tower.x && x < tower.x + 2 && y >= tower.y && y < tower.y + 2) return &tower;
    return nullptr;
}

// Use original upgrade costs and keep accumulated investment for later sales.
bool Game::upgrade(int id) {
    auto tower = std::find_if(towers.begin(),towers.end(),[id](const Tower& value) { return value.id == id; });
    if (tower == towers.end() || tower->selling || timeMs <= tower->readyAt) { message = "Wait for construction to finish."; return false; }
    const auto& upgrades = technology.at("towers").at(tower->type).at("upgrades");
    if (upgrades.empty()) { message = "This laser is fully upgraded."; return false; }
    const std::string next = upgrades[0].get<std::string>();
    const auto& definition = technology.at("towers").at(next);
    const int cost = definition.at("cost").get<int>();
    if (money < cost) { message = "Not enough gold for " + next + "."; return false; }
    money -= cost; tower->refund += cost; tower->type = next; tower->target = -1;
    tower->readyAt = timeMs + (wavesSent == 0 ? 500 : definition.value("buildtime",0));
    tower->nextShotAt = tower->readyAt; message = "Upgrading to " + next + ".";
    return true;
}

// Keep the footprint blocked until the original sale timer has completed.
bool Game::sell(int id) {
    auto tower = std::find_if(towers.begin(),towers.end(),[id](const Tower& value) { return value.id == id; });
    if (tower == towers.end() || tower->selling || timeMs <= tower->readyAt) { message = "Wait for construction to finish."; return false; }
    tower->selling = true; tower->target = -1; tower->readyAt = timeMs + (wavesSent == 0 ? 500 : 3000);
    tower->refund = wavesSent == 0 ? tower->refund : static_cast<int>(tower->refund * 0.75f);
    message = "Selling laser; refund when dismantling finishes."; return true;
}

// Resolve the unchanged original wave parameters for a live or split enemy.
const nlohmann::json& Game::waveFor(const Enemy& enemy) const { return waves.at(enemy.wave); }

// Record actual live state without adjusting results to satisfy a test expectation.
nlohmann::json Game::snapshot() const {
    return {{"timeMs",timeMs},{"money",money},{"lives",lives},{"wavesSent",wavesSent},{"totalWaves",waves.size()},
            {"spawned",spawned},{"killed",killed},{"leaked",leaked},{"activeEnemies",std::count_if(enemies.begin(),enemies.end(),[](const Enemy& e) { return e.alive; })},
            {"towers",towers.size()},{"outcome",static_cast<int>(outcome)}};
}

// Dispatch waves across idle UFOs while preserving every original wave entry.
bool Game::sendWave() {
    if (outcome == Outcome::Won || outcome == Outcome::Lost || wavesSent >= static_cast<int>(waves.size())) return false;
    for (int offset = 0; offset < static_cast<int>(ufos.size()); ++offset) {
        const int index = (nextPad_ + offset) % static_cast<int>(ufos.size());
        auto& ufo = ufos[index];
        if (ufo.state != Ufo::State::Idle) continue;
        ufo.wave = wavesSent++; ufo.remaining = waves.at(ufo.wave).at("count").get<int>();
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
    for (auto& enemy : enemies) {
        if (!enemy.alive) continue;
        const auto& wave = waveFor(enemy);
        enemy.heat = std::max(1.0f,enemy.heat * (1 - HeatDecayPerSecond * seconds));
        const bool flying = wave.value("flying",false);
        const auto& field = flying ? flyingDistances_ : distances_;
        int x = static_cast<int>(enemy.x),y = static_cast<int>(enemy.y);
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        if (field[x + y * width] == 0) {
            enemy.alive = false; ++leaked; --lives;
            if (lives <= 0) { lives = 0; outcome = Outcome::Lost; message = "The sheep are gone. Restart to try a new defense."; }
            continue;
        }
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
            const float step = std::min(length,wave.at("speed").get<float>() * seconds);
            enemy.x += dx / length * step; enemy.y += dy / length * step; enemy.traveled += step;
            enemy.heading = std::atan2(dy,dx);
        }
    }
}

// Preserve beam heat, integer truncation, kill rewards, and generation-limited splits.
void Game::damage(Enemy& enemy, const Tower& tower) {
    const auto& definition = technology.at("towers").at(tower.type);
    const auto& weapon = technology.at("weapons").at(definition.at("weapon").get<std::string>());
    const auto& wave = waveFor(enemy);
    const int armor = wave.value("armor",0);
    float armorMultiplier = 1;
    if (armor == 1) armorMultiplier = definition.value("lightdamage",1.0f);
    else if (armor == 2) armorMultiplier = definition.value("heavydamage",1.0f);
    else if (armor == 3) armorMultiplier = definition.value("bionicdamage",1.0f);
    const int raw = static_cast<int>(weapon.at("damage").get<float>() * enemy.heat);
    const int dealt = static_cast<int>(raw * armorMultiplier);
    enemy.health -= dealt;
    if (armor != 4) enemy.heat = std::min(MaximumHeat,enemy.heat + BeamHeatPerHit);
    beams.push_back({tower.x + 1.0f,tower.y + 1.0f,enemy.x,enemy.y,wave.value("height",0.0f) + 0.5f,timeMs + BeamVisualMs});
    if (enemy.health > 0) return;
    enemy.alive = false; ++killed;
    if (enemy.generation == 0) money += wave.at("gold").get<int>();
    if (enemy.generation >= wave.value("spawn",0)) return;
    // Reserve child data before pushing into the vector, which can invalidate enemy.
    const Enemy parent = enemy;
    for (int childIndex = 0; childIndex < 2; ++childIndex) {
        Enemy child = makeEnemy(parent.wave,parent.x,parent.y);
        child.generation = parent.generation + 1;
        child.health = child.maxHealth = wave.at("health").get<std::int64_t>() >> parent.generation;
        child.model = wave.at("submodel").get<std::string>();
        // RNG sequence remains an explicit reconstruction gap; offsets follow the recovered bounds.
        for (int attempt = 0; attempt < 10; ++attempt) {
            const auto offset = [this]() { randomState_ = randomState_ * RandomMultiplier + RandomIncrement; return static_cast<float>(randomState_ >> 8) / RandomScale * 3 - 1.5f; };
            const float x = parent.x + offset(),y = parent.y + offset();
            if (std::isfinite(distanceAt(static_cast<int>(x),static_cast<int>(y)))) { child.x = x; child.y = y; break; }
        }
        child.moveTarget = {static_cast<int>(child.x),static_cast<int>(child.y)};
        enemies.push_back(child); ++spawned;
    }
}

// Select minimum path distance, retain targets, and respect turn and reload limits.
void Game::attack() {
    for (auto& tower : towers) {
        if (tower.selling || timeMs <= tower.readyAt) continue;
        const auto& definition = technology.at("towers").at(tower.type);
        const float range = definition.at("range").get<float>();
        const auto valid = [this,&tower,range,&definition](const Enemy& enemy, bool acquiring) {
            const bool flying = waveFor(enemy).value("flying",false);
            const float distance = separation(tower.x + 1.0f,tower.y + 1.0f,enemy.x,enemy.y);
            return enemy.alive && (flying ? definition.value("antiair",false) : definition.value("antiground",false)) &&
                (acquiring ? distance < range : distance <= range);
        };
        auto target = std::find_if(enemies.begin(),enemies.end(),[&tower,&valid](const Enemy& e) { return e.id == tower.target && valid(e,false); });
        if (target == enemies.end()) {
            float shortest = Unreachable;
            for (auto candidate = enemies.begin(); candidate != enemies.end(); ++candidate) {
                if (!valid(*candidate,true)) continue;
                const auto& field = waveFor(*candidate).value("flying",false) ? flyingDistances_ : distances_;
                const float distance = field[static_cast<int>(candidate->x) + static_cast<int>(candidate->y) * width];
                if (distance > 0 && distance < shortest) { shortest = distance; target = candidate; }
            }
        }
        if (target == enemies.end()) { tower.target = -1; continue; }
        tower.target = target->id;
        const float desired = std::atan2(target->y - tower.y - 1,target->x - tower.x - 1);
        const float difference = std::remainder(desired - tower.heading,FullTurn);
        tower.heading += difference * TurretTurnFactor * SimulationStepMs / 1000.0f;
        if (std::abs(difference) < FireAngleTolerance && timeMs > tower.nextShotAt) {
            tower.nextShotAt = timeMs + definition.at("reload").get<int>();
            damage(*target,tower);
        }
    }
}

// Apply fixed-step updates independently from drawing frequency or wall-clock speed.
void Game::tick() {
    if (paused || outcome == Outcome::Won || outcome == Outcome::Lost) return;
    timeMs += SimulationStepMs;
    std::erase_if(beams,[this](const Beam& beam) { return timeMs >= beam.expiresAt; });
    for (const auto& tower : towers) if (tower.selling && timeMs > tower.readyAt) {
        money += tower.refund;
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) grid_[tower.x + dx + (tower.y + dy) * width] &= ~1;
    }
    const auto removed = std::erase_if(towers,[this](const Tower& tower) { return tower.selling && timeMs > tower.readyAt; });
    if (removed) updateDistances();
    if (outcome == Outcome::Preparing) return;
    updateUfos(); moveEnemies();
    // GAME_timestep stops tower attacks once loss is recorded in enemy movement.
    if (outcome != Outcome::Lost) attack();
    std::erase_if(enemies,[](const Enemy& enemy) { return !enemy.alive; });
    if (outcome == Outcome::Lost) return;
    if (wavesSent < static_cast<int>(waves.size()) && timeMs - lastWaveAt_ > WaveIntervalMs) sendWave();
    const bool allIdle = std::all_of(ufos.begin(),ufos.end(),[](const Ufo& ufo) { return ufo.state == Ufo::State::Idle; });
    if (wavesSent == static_cast<int>(waves.size()) && enemies.empty() && allIdle) {
        outcome = Outcome::Won; beams.clear(); message = "All 26 original waves cleared. The sheep are safe!";
    }
}

} // namespace tmreborn
