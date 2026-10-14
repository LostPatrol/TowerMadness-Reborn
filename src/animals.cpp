// Original synchronized sheep state machine and independent leaked-enemy abductions.
#include "game.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace tmreborn {
namespace {
// Native animal motion constants apply once per original 25 ms update.
constexpr float SheepStep = 0.02f, IntruderStep = 0.05f, TurnFactor = 0.1f;
// Native eating probability, target tolerance, and avoidance radius squared.
constexpr float EatingThreshold = 0.98f, ArrivalDistance = 0.1f, NeighborRadiusSquared = 1.0f;
// Movement oscillates with distance rather than animation time.
constexpr float MoveFrequency = 16.0f, MoveAmplitude = 3.0f, MoveMinimum = 0.1f;
// Capture starts inside half a unit; flying intruders descend to this minimum height.
constexpr float CaptureDistance = 0.5f, FlyingMinimumHeight = 0.6f;
// Recovered float32 conversion and turn dead zone match the animal angle routines.
constexpr float DegreesPerRadian = 57.295784f, TurnDeadZone = 2.0f;
// Sheep turn for 600 ms, then ascend for 2000 ms before hiding.
constexpr int CaptureTurnMs = 600, CaptureRiseMs = 2000;
// Original initial size distribution and the rare single-player blue color.
constexpr float SmallChance = 0.2f, BlueChance = 0.1f;
constexpr float SmallSize = 0.6f, SmallVariation = 0.1f, LargeSize = 0.9f, LargeVariation = 0.2f;

// Correct the original angular difference once rather than normalizing stored yaw.
float shortestDifference(float target, float current) {
    float difference = target - current;
    if (difference > 180.0f) difference -= 360.0f;
    else if (difference < -180.0f) difference += 360.0f;
    return difference;
}

// Normalize direction before atan2, matching the recovered float32 instructions.
float targetAngle(float x, float y, float targetX, float targetY) {
    const float dx = targetX - x, dy = targetY - y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    return distance > 0 ? std::atan2(dy / distance,dx / distance) * DegreesPerRadian : 0;
}

// Arriving sheep eat in place; other sheep use the distance-dependent native step.
void moveSheep(Sheep& animal) {
    const float dx = animal.targetX - animal.x, dy = animal.targetY - animal.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance < ArrivalDistance) { animal.state = 0; return; }
    const float factor = std::abs(std::sin(distance * MoveFrequency)) * MoveAmplitude + MoveMinimum;
    animal.x += ((dx * factor) * SheepStep) / distance;
    animal.y += ((dy * factor) * SheepStep) / distance;
}
} // namespace

// Every point consumes an integer sample followed by the two original float samples.
Vec2 Game::generateAnimalFieldPoint() {
    const auto raw = static_cast<std::uint32_t>(randomEngine_());
    const auto signedSample = std::bit_cast<std::int32_t>(raw);
    // Widen before absolute value so the single INT_MIN sample cannot cause C++ UB.
    const auto magnitude = signedSample < 0 ? -static_cast<std::int64_t>(signedSample) : static_cast<std::int64_t>(signedSample);
    const auto& cell = sheepField_[static_cast<std::size_t>(magnitude) % sheepField_.size()];
    const float x = randomFloat(), y = randomFloat();
    return {static_cast<float>(cell.x) + x,static_cast<float>(cell.y) + y};
}

// Initialize position, target, size, and color in the original nine-sample order.
void Game::initializeAnimals() {
    sheep_.clear(); intruders_.clear(); sheepField_.clear(); animalClockMs_ = 0; lastAbductionFocus_.reset();
    if (map.contains("enclosure") && map.at("enclosure").is_array())
        for (const auto& cell : map.at("enclosure"))
            if (cell.is_array() && cell.size() == 2 && cell[0].is_number_integer() && cell[1].is_number_integer())
                sheepField_.push_back({cell[0].get<int>(),cell[1].get<int>()});
    // Older custom maps remain playable while their missing enclosure is reported.
    if (sheepField_.empty()) { sheepField_ = goals_; message += " Warning: missing enclosure; sheep use gate cells."; }
    for (int index = 0; index < lives; ++index) {
        Sheep animal;
        const auto position = generateAnimalFieldPoint(), target = generateAnimalFieldPoint();
        animal.x = position.x; animal.y = position.y; animal.targetX = target.x; animal.targetY = target.y;
        animal.yawDegrees = targetAngle(animal.x,animal.y,animal.targetX,animal.targetY);
        const float sizeSample = randomFloat(), sizeClass = randomFloat(), colorSample = randomFloat();
        animal.size = sizeClass < SmallChance ? SmallSize + SmallVariation * sizeSample : LargeSize + LargeVariation * sizeSample;
        animal.color = colorSample < BlueChance ? 2 : 0;
        sheep_.push_back(animal);
    }
}

// Preserve the leaked enemy's original wave geometry and reserve in insertion order.
void Game::addSheepIntruder(const Enemy& enemy) {
    const auto available = std::find_if(sheep_.begin(),sheep_.end(),[](const Sheep& animal) { return !animal.reserved; });
    if (available == sheep_.end()) return;
    available->reserved = true;
    if (std::all_of(sheep_.begin(),sheep_.end(),[](const Sheep& animal) { return animal.reserved; }))
        lastAbductionFocus_ = Vec2{available->x,available->y};
    const auto& wave = waveFor(enemy);
    SheepIntruder intruder;
    intruder.sheepIndex = static_cast<int>(available - sheep_.begin());
    intruder.wave = enemy.wave; intruder.generation = enemy.generation;
    intruder.x = enemy.x; intruder.y = enemy.y; intruder.z = wave.value("height",0.0f);
    intruder.yawDegrees = targetAngle(intruder.x,intruder.y,available->x,available->y);
    intruder.size = wave.at("size").get<float>(); intruder.speed = wave.at("speed").get<float>();
    intruder.walkFactor = wave.value("walkanimfactor",1.0f); intruder.flying = wave.value("flying",false);
    intruder.model = enemy.generation == 1 ? wave.at("submodel").get<std::string>() : wave.at("model").get<std::string>();
    const auto& frames = enemy.generation == 1 && wave.contains("subwalk") && !wave.at("subwalk").empty() ? wave.at("subwalk") : wave.at("walk");
    for (const auto& frame : frames) intruder.walkModels.push_back(frame.get<std::string>());
    intruders_.push_back(std::move(intruder));
}

// Sheep update in place so later neighbors observe earlier motion, as the native list does.
void Game::updateAnimals() {
    for (auto& animal : sheep_) {
        if (animal.state == 0) {
            if (randomFloat() > EatingThreshold) {
                animal.state = 2;
                const auto target = generateAnimalFieldPoint(); animal.targetX = target.x; animal.targetY = target.y;
            }
        } else if (animal.state == 1) {
            float meanX = 0, meanY = 0;
            int neighbors = 0;
            // Captured and hidden sheep intentionally remain in the avoidance list.
            for (const auto& other : sheep_) {
                if (&animal == &other) continue;
                const float dx = animal.x - other.x, dy = animal.y - other.y;
                if (dx * dx + dy * dy < NeighborRadiusSquared) { meanX += other.x; meanY += other.y; ++neighbors; }
            }
            if (neighbors) {
                animal.targetX = animal.x + animal.x - meanX / static_cast<float>(neighbors);
                animal.targetY = animal.y + animal.y - meanY / static_cast<float>(neighbors);
                animal.state = 2;
            }
            const bool inside = std::any_of(sheepField_.begin(),sheepField_.end(),[&](Cell cell) {
                return animal.targetX >= cell.x && animal.targetX <= cell.x + 1 && animal.targetY >= cell.y && animal.targetY <= cell.y + 1;
            });
            if (!inside) { const auto target = generateAnimalFieldPoint(); animal.targetX = target.x; animal.targetY = target.y; }
            // Avoidance may switch to turning, but this same tick still moves the sheep.
            moveSheep(animal);
        } else if (animal.state == 2) {
            const float target = targetAngle(animal.x,animal.y,animal.targetX,animal.targetY);
            const float difference = shortestDifference(target,animal.yawDegrees);
            if (std::abs(difference) > TurnDeadZone) animal.yawDegrees += difference * TurnFactor;
            else { animal.yawDegrees = target; animal.state = 1; }
        } else if (animal.state == 3) {
            const int elapsed = animalClockMs_ - animal.captureStartMs;
            if (elapsed >= CaptureTurnMs + CaptureRiseMs) animal.state = -1;
            else if (elapsed < CaptureTurnMs) {
                const float target = targetAngle(animal.x,animal.y,animal.targetX,animal.targetY) + 180.0f;
                const float difference = shortestDifference(target,animal.startYawDegrees);
                if (std::abs(difference) > TurnDeadZone)
                    animal.yawDegrees = animal.startYawDegrees + difference * (static_cast<float>(elapsed) / CaptureTurnMs);
            }
        }
    }
    std::erase_if(intruders_,[this](SheepIntruder& intruder) {
        auto& animal = sheep_[intruder.sheepIndex];
        // The intruder survives one additional boundary tick after its sheep is hidden.
        if (intruder.state == 1) return animalClockMs_ - animal.captureStartMs > CaptureTurnMs + CaptureRiseMs;
        const float dx = animal.x - intruder.x, dy = animal.y - intruder.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        intruder.yawDegrees = targetAngle(intruder.x,intruder.y,animal.x,animal.y);
        if (distance < CaptureDistance) {
            intruder.state = 1; animal.state = 3;
            animal.targetX = intruder.x; animal.targetY = intruder.y;
            animal.captureStartMs = animalClockMs_; animal.startYawDegrees = animal.yawDegrees;
        } else {
            intruder.x += (dx * IntruderStep) / distance; intruder.y += (dy * IntruderStep) / distance;
            if (intruder.flying) intruder.z = std::max(FlyingMinimumHeight,intruder.z - IntruderStep);
        }
        return false;
    });
    animalClockMs_ += SimulationStepMs;
}

// Expose the retained native list for deterministic model and shadow rendering.
const std::vector<Sheep>& Game::sheep() const { return sheep_; }
// Native camera deltas target the sheep position at reservation, not its later moving position.
std::optional<Vec2> Game::lastAbductionFocus() const { return lastAbductionFocus_; }
// Abductors cannot be targeted after their removal from the combat deque.
const std::vector<SheepIntruder>& Game::intruders() const { return intruders_; }
// Rendering samples the same post-update clock used by the original animal draw calls.
int Game::animalClockMs() const { return animalClockMs_; }
// Loss waits for hidden sheep; victory uses the original active-intruder boundary.
bool Game::hasPendingAbductions() const {
    if (lives <= 0)
        return std::any_of(sheep_.begin(),sheep_.end(),[](const Sheep& animal) { return animal.state != -1; });
    return std::any_of(intruders_.begin(),intruders_.end(),[this](const SheepIntruder& intruder) {
        return intruder.state == 0 || animalClockMs_ - sheep_[intruder.sheepIndex].captureStartMs < CaptureTurnMs + CaptureRiseMs;
    });
}

} // namespace tmreborn
