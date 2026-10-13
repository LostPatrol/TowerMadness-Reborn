// Original muzzle geometry, ballistic prediction, and moving projectile hit rules.
#include "game.hpp"

#include <algorithm>
#include <cmath>

namespace tmreborn {
namespace {
// Original ARM literals used for degrees and milliseconds conversion.
constexpr float DegreesToRadians = 0.0174532924f, SecondsPerMillisecond = 0.001f;
// The original velocity solver multiplies by a double half-gravity literal.
constexpr double BallisticCoefficient = 4.9;
// Original analytic projectile gravity and its final half factor are floats.
constexpr float Gravity = -9.8f, Half = 0.5f;
// Tracking hit distances and zero-motion threshold from PROJECTILES_update.
constexpr float TrackingHitDistance = 0.35f, NuclearHitDistance = 1.0f, MotionEpsilon = 0.0001f;
// Tracking turn allowance grows by ten degrees per traveled world unit.
constexpr float TurnDegreesPerUnit = 10.0f;
// Original muzzle and tracked enemy point both include this ground offset.
constexpr float GroundOffset = 0.25f;
// Direct vertical aim is selected below this original horizontal distance.
constexpr float MinimumAimDistance = 0.001f;
// H Bomb's pre-damage heat addition saturates the original heat cap.
constexpr float MaximumHeat = 2.0f;
// Desktop explosion lifetime is visual only and does not defer damage.
constexpr int ExplosionVisualMs = 1000, NukeVisualMs = 4000;
// Original mortar impact ground damage outlives its explosion.
constexpr int CraterVisualMs = 5000, NukeCraterVisualMs = 10000;
// Original trail records expire independently of their parent projectile.
constexpr int TrailVisualMs = 250;
// A half turn maps desktop direction radians to the original tower yaw frame.
constexpr float HalfPi = 1.57079633f;

/** Read a configuration offset while preserving the original XYZ order. */
Vec3 offsetVector(const nlohmann::json& value) {
    return value.is_array() && value.size() == 3
        ? Vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()} : Vec3{};
}

/** Rotate the previous travel direction toward the target by the original cap. */
Vec3 trackingDirection(Vec3 previous, Vec3 desired, float traveled) {
    const float previousLength = std::sqrt(dot(previous, previous));
    if (previousLength < MotionEpsilon) return desired;
    const Vec3 oldDirection = previous * (1.0f / previousLength);
    const float angle = traveled * TurnDegreesPerUnit * DegreesToRadians;
    if (dot(oldDirection, desired) >= std::cos(angle)) return desired;
    Vec3 axis = cross(oldDirection, desired);
    if (dot(axis, axis) < MotionEpsilon * MotionEpsilon) return desired;
    axis = normalized(axis);
    return oldDirection * std::cos(angle) + cross(axis, oldDirection) * std::sin(angle)
        + axis * (dot(axis, oldDirection) * (1.0f - std::cos(angle)));
}
} // namespace

/** Resolve the original fixed or target-dependent tower elevation in radians. */
float Game::towerElevation(const Tower& tower) const {
    const auto& type = technology.at("towers").at(tower.type);
    const std::string kind = technology.at("weapons").at(type.at("weapon").get<std::string>()).at("type").get<std::string>();
    // These native aim callbacks are no-ops; their initialized elevation stays zero.
    if (kind == "lightning" || kind == "boost" || kind == "mine" || kind == "bait") return 0;
    float elevation = type.value("elevation", 0.0f) * DegreesToRadians;
    if (!type.contains("elevation")) {
        const auto enemy = std::find_if(enemies.begin(), enemies.end(), [&](const Enemy& value) {
            return value.id == tower.target && value.alive;
        });
        if (enemy != enemies.end()) {
            const auto& wave = waveFor(*enemy);
            const float distance = std::hypot(enemy->x - tower.x - 1.0f, enemy->y - tower.y - 1.0f);
            elevation = distance < MinimumAimDistance ? HalfPi
                : std::atan2(wave.value("height", 0.0f) + wave.value("offset", 0.0f) - GroundOffset, distance);
        }
    }
    return elevation;
}

/** Transform the original muzzle with inverse yaw/pitch and rotation offset. */
Vec3 Game::muzzle(const Tower& tower, int index) const {
    const auto& type = technology.at("towers").at(tower.type);
    Vec3 local{};
    if (type.contains("muzzleoffset") && !type.at("muzzleoffset").empty()) {
        const auto& muzzles = type.at("muzzleoffset");
        local = offsetVector(muzzles.at(std::min(index, static_cast<int>(muzzles.size()) - 1)));
    }
    const Vec3 rotation = type.contains("rotationoffset") ? offsetVector(type.at("rotationoffset")) : Vec3{};
    const float elevation = towerElevation(tower);
    // Muzzle helpers use negative degree conversion, unlike ballistic physics.
    const float yaw = HalfPi - tower.heading, pitch = -elevation;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    return {tower.x + 1.0f + rotation.x + local.x * cy + local.y * sy * cp + local.z * sy * sp,
            tower.y + 1.0f + rotation.y - local.x * sy + local.y * cy * cp + local.z * cy * sp,
            GroundOffset + rotation.z - local.y * sp + local.z * cp};
}

/** Compute the exact ground-impact velocity and original planar lead factor. */
float Game::ballisticLaunch(const Tower& tower, const Enemy& target, Vec2& aim) const {
    const auto& type = technology.at("towers").at(tower.type);
    const auto& weapon = technology.at("weapons").at(type.at("weapon").get<std::string>());
    const Vec3 start = muzzle(tower);
    const float elevation = type.value("elevation", 0.0f) * DegreesToRadians;
    const float cosine = std::cos(elevation), sine = std::sin(elevation);
    const float distance = std::hypot(target.x - start.x, target.y - start.y);
    const float denominator = cosine * cosine * start.z + sine * cosine * distance;
    const float numerator = static_cast<float>(static_cast<double>(distance * distance) * BallisticCoefficient);
    const float speed = denominator > 0 ? std::sqrt(numerator / denominator) : 0;
    const float multiplier = weapon.value("speedmultiplier", 1.0f);
    const float flightTime = speed * cosine > 0 ? distance / (speed * cosine) : 0;
    // Original predictor explicitly multiplies step milliseconds without 0.001.
    const float lead = flightTime * static_cast<float>(SimulationStepMs) / multiplier;
    aim = {target.x + (target.x - target.previous.x) * lead,
           target.y + (target.y - target.previous.y) * lead};
    return speed;
}

/** Advance analytic mortar arcs and capped trackers, applying each hit once. */
void Game::updateProjectiles() {
    const float dt = static_cast<float>(SimulationStepMs) * SecondsPerMillisecond;
    // PROJECTILES_create inserts at the front of the original linked list.
    for (auto iterator = projectiles.rbegin(); iterator != projectiles.rend(); ++iterator) {
        auto& projectile = *iterator;
        if (!projectile.active) continue;
        const bool nuclear = projectile.source.type == "Nuke" || projectile.source.type == "H Bomb";
        auto enemy = std::find_if(enemies.begin(), enemies.end(), [&](const Enemy& value) {
            return value.id == projectile.target && value.alive;
        });
        if (enemy != enemies.end() && (!nuclear || projectile.waypointReached)) {
            const auto& wave = waveFor(*enemy);
            projectile.endpoint = {enemy->x, enemy->y,
                wave.value("height", 0.0f) + wave.value("offset", 0.0f) + GroundOffset};
        }
        bool hit = false;
        const bool ballistic = projectile.kind == "mortar" && (!nuclear || !projectile.waypointReached);
        if (ballistic) {
            projectile.previous = projectile.position;
            const float scaledStep = dt * projectile.speedMultiplier;
            projectile.elapsed += scaledStep;
            projectile.position.x += projectile.velocity.x * scaledStep;
            projectile.position.y += projectile.velocity.y * scaledStep;
            projectile.position.z = projectile.origin.z + std::sin(projectile.elevation)
                * projectile.speed * projectile.elapsed
                + ((projectile.elapsed * projectile.elapsed) * Gravity) * Half;
            if (projectile.position.z < 0) hit = true;
            else if (nuclear && projectile.position.z < projectile.previous.z) {
                projectile.waypointReached = true;
                // Original offset 0x58 changes from elapsed seconds to travel distance.
                projectile.traveled = projectile.elapsed;
            }
        } else {
            Vec3 endpoint = !projectile.waypointReached ? projectile.waypoint : projectile.endpoint;
            // Losing a target during phase four follows its last final endpoint.
            if (enemy == enemies.end() && !projectile.waypointReached) {
                projectile.waypointReached = true;
                endpoint = projectile.endpoint;
            }
            float distance = std::sqrt(dot(endpoint - projectile.position, endpoint - projectile.position));
            if (distance < TrackingHitDistance && !projectile.waypointReached) {
                projectile.waypointReached = true;
                endpoint = projectile.endpoint;
                distance = std::sqrt(dot(endpoint - projectile.position, endpoint - projectile.position));
            }
            if (distance < (nuclear && enemy != enemies.end() ? NuclearHitDistance : TrackingHitDistance)) hit = true;
            else {
                const Vec3 direction = trackingDirection(projectile.position - projectile.previous,
                    normalized(endpoint - projectile.position), projectile.traveled);
                projectile.previous = projectile.position;
                projectile.velocity = direction * projectile.speed;
                const float step = projectile.speed * dt;
                projectile.traveled += step;
                projectile.position = projectile.position + direction * step;
            }
        }
        if (!hit) {
            const std::string weaponName = technology.at("towers").at(projectile.source.type).at("weapon").get<std::string>();
            const bool suppressed = !ballistic && (weaponName.starts_with("Plasmatron") || weaponName.starts_with("Gauss")
                || weaponName.starts_with("Fusion") || weaponName.starts_with("Flame") || weaponName.starts_with("Energy"));
            // Nuclear descent retains the ascent's last sample and threshold unchanged.
            const float progress = ballistic ? projectile.elapsed : projectile.traveled;
            if (!suppressed && progress > projectile.trailThreshold) {
                const float trailWidth = nuclear ? 5 : ballistic ? 2.5f : 3;
                Effect trail{projectile.position,trailWidth,timeMs + TrailVisualMs,"trail",timeMs,projectile.source.type};
                trail.end = projectile.trailSample;
                addEffect(std::move(trail));
                projectile.trailSample = projectile.position;
                projectile.trailThreshold = progress + (ballistic && !nuclear ? 0.1f : 0.2f);
            }
            continue;
        }
        projectile.active = false;
        const auto& type = technology.at("towers").at(projectile.source.type);
        const auto& weapon = technology.at("weapons").at(type.at("weapon").get<std::string>());
        if (weapon.contains("hitsounds") && !weapon.at("hitsounds").empty()) soundEvents.push_back(weapon.at("hitsounds")[0].get<std::string>());
        if (projectile.kind == "plasma") {
            if (enemy != enemies.end()) damage(*enemy, projectile.source, projectile.damage);
            const float chance = weapon.value("stun", 0.0f);
            if (chance > 0 && randomFloat() < chance && enemy != enemies.end() && enemy->alive)
                stun(*enemy, weapon.value("duration", 0));
        } else {
            if (projectile.source.type == "H Bomb") {
                for (auto& victim : enemies) {
                    const float dx = victim.x - projectile.position.x, dy = victim.y - projectile.position.y;
                    if (victim.alive && dx * dx + dy * dy < projectile.splash * projectile.splash) victim.heat = MaximumHeat;
                }
            }
            radiusDamage(projectile.position.x, projectile.position.y, projectile.splash, projectile.damage, projectile.source);
        }
        // Original plasma hits apply damage/status without a generic impact explosion.
        if (projectile.kind != "plasma") {
            Vec3 visualPosition = projectile.position;
            int lifetime = nuclear ? NukeVisualMs : ExplosionVisualMs;
            if (projectile.kind == "homing") {
                // Original impact jitter uses only the separate visual random stream.
                const std::uint32_t seed = visualSeed(timeMs,visualPosition);
                visualPosition = visualPosition + Vec3{0.25f * (visualRandom(seed + 1) - 0.5f),
                    0.25f * (visualRandom(seed + 2) - 0.5f),0.25f * (visualRandom(seed + 3) - 0.5f)};
                lifetime = static_cast<int>(1500 + 3000 * visualRandom(visualSeed(timeMs,visualPosition)));
            }
            addEffect({visualPosition,projectile.splash,timeMs + lifetime,
                nuclear ? "nuke" : projectile.kind,timeMs,projectile.source.type});
            if (projectile.kind == "mortar") addEffect({projectile.position,projectile.splash * (nuclear ? 0.5f : 0.75f),
                timeMs + (nuclear ? NukeCraterVisualMs : CraterVisualMs),"groundDamage",timeMs,projectile.source.type});
        }
        releaseChildren();
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& value) {
        return !value.active;
    }), projectiles.end());
}
} // namespace tmreborn
