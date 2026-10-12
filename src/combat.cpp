// Distinct recovered weapon families, integer armor damage, and stable enemy splits.
#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace tmreborn {
namespace {
// Match original float32 yaw smoothing and strict angular tolerance.
constexpr float TurnFactor = 12.5f, FireTolerance = 0.08726646f, FullTurn = 6.2831853f;
// Original line weapons extend one and a half configured targeting radii.
constexpr float LineRangeFactor = 1.5f;
// Desktop effect lifetime affects rendering only, never hit timing.
constexpr int EffectLifetimeMs = 250, BeamLifetimeMs = 125;
// Beam and flame increment target heat by this amount, capped at two.
constexpr float HeatPerHit = 0.05f, MaximumHeat = 2.0f;

// Measure planar distance in the original single-precision world coordinates.
float planar(float x, float y, float tx, float ty) { return std::hypot(tx - x,ty - y); }

// Resolve a target's original height plus configured visual center offset.
Vec3 hitPoint(const Enemy& enemy, const nlohmann::json& wave) {
    return {enemy.x,enemy.y,wave.value("height",0.0f) + wave.value("offset",0.0f) + 0.25f};
}
}

// Original sync_frand48 converts the unsigned MT19937 result to float before scaling.
float Game::randomFloat() { return static_cast<float>(randomEngine_()) * 0x1p-32f; }

// Apply target eligibility and integer armor truncation before kill accounting.
void Game::damage(Enemy& enemy, const Tower& tower, int raw) {
    if (!enemy.alive) return;
    const auto source = std::find_if(towers.begin(),towers.end(),[&](const Tower& active) { return active.id == tower.id; });
    if (source == towers.end()) return;
    const auto& definition = technology.at("towers").at(source->type);
    const auto& wave = waveFor(enemy);
    if (!(wave.value("flying",false) ? definition.value("antiair",false) : definition.value("antiground",false))) return;
    const int armor = wave.value("armor",0);
    float multiplier = 1;
    if (armor == 1) multiplier = definition.value("lightdamage",1.0f);
    else if (armor == 2) multiplier = definition.value("heavydamage",1.0f);
    else if (armor == 3) multiplier = definition.value("bionicdamage",1.0f);
    else if (armor == 4) multiplier = static_cast<float>(1 - definition.value("isflame",0));
    int dealt = std::max(0,static_cast<int>(raw * multiplier));
    if (definition.value("isflame",0)) dealt = static_cast<int>(dealt * enemy.heat);
    enemy.health = std::max(std::int64_t{0},enemy.health - dealt);
    if (enemy.health > 0) return;
    enemy.alive = false; ++killed;
    completeWaveEnemy(enemy.wave,true);
    if (wave.contains("deathsounds") && !wave.at("deathsounds").empty()) soundEvents.push_back(wave.at("deathsounds")[0].get<std::string>());
    if (enemy.generation == 0) { const int reward = wave.at("gold").get<int>(); money += reward; score += reward; }
    if (enemy.generation >= wave.value("spawn",0)) return;
    // Queue children separately so a splash/line traversal never invalidates enemy references.
    for (int index = 0; index < 2; ++index) {
        Enemy child = makeEnemy(enemy.wave,enemy.x,enemy.y);
        child.generation = enemy.generation + 1;
        child.slowMs = enemy.slowMs; child.slowFactor = enemy.slowFactor; child.stunMs = enemy.stunMs;
        child.health = child.maxHealth = wave.at("health").get<std::int64_t>() >> enemy.generation;
        child.model = wave.at("submodel").get<std::string>();
        for (int attempt = 0; attempt < 10; ++attempt) {
            const float x = enemy.x + (randomFloat() - 0.5f) * 3,y = enemy.y + (randomFloat() - 0.5f) * 3;
            if (std::isfinite(distanceAt(static_cast<int>(x),static_cast<int>(y)))) { child.x = x; child.y = y; break; }
        }
        child.previous = {child.x,child.y}; pendingChildren_.push_back(child); ++spawned;
    }
}

// New children become eligible for later projectiles, towers, and lightning jumps.
void Game::releaseChildren() {
    enemies.insert(enemies.end(),pendingChildren_.begin(),pendingChildren_.end()); pendingChildren_.clear();
}

// Use strict radius membership and linear falloff before armor truncation.
void Game::radiusDamage(float x, float y, float radius, int raw, const Tower& tower) {
    if (radius <= 0) return;
    for (auto& enemy : enemies) {
        const float distance = planar(x,y,enemy.x,enemy.y);
        if (distance < radius) damage(enemy,tower,static_cast<int>(raw * (1 - distance / radius)));
    }
}

// Original radius is compared directly with squared perpendicular distance.
void Game::lineDamage(float x, float y, float tx, float ty, float length, float radius, int raw, const Tower& tower) {
    const float dx = tx - x,dy = ty - y,distance = planar(x,y,tx,ty);
    if (distance == 0) return;
    const float ux = dx / distance,uy = dy / distance;
    for (auto& enemy : enemies) {
        const float ex = enemy.x - x,ey = enemy.y - y;
        const float projection = ex * ux + ey * uy,perpendicular = ex * uy - ey * ux;
        if (projection > 0 && projection * projection < length * length && perpendicular * perpendicular < radius)
            damage(enemy,tower,raw);
    }
}

// Dispatch the original family-specific attack; shared damage only handles armor and kills.
void Game::fire(Tower& tower, Enemy* target) {
    const auto& definition = technology.at("towers").at(tower.type);
    const auto& weapon = technology.at("weapons").at(definition.at("weapon").get<std::string>());
    const std::string kind = weapon.at("type").get<std::string>();
    const float x = tower.x + 1.0f,y = tower.y + 1.0f;
    const float baseDamage = weapon.value("damage",0.0f);
    const int boosted = static_cast<int>(baseDamage * tower.damageBoost);
    if (kind == "boost") return;
    if (kind == "mine" || kind == "bait") {
        if (tower.triggeredAt < 0) { if (target) tower.triggeredAt = timeMs; return; }
        if (timeMs < tower.triggeredAt + weapon.at("triggerdelay").get<int>()) return;
        const float radius = weapon.at("splash").get<float>();
        if (kind == "mine") radiusDamage(x,y,radius,static_cast<int>(baseDamage),tower);
        else for (auto& enemy : enemies) if (planar(x,y,enemy.x,enemy.y) < radius) {
            slow(enemy,weapon.at("factor").get<float>(),weapon.at("slowduration").get<int>());
            enemy.stunMs = std::max(enemy.stunMs,weapon.at("stunduration").get<int>());
        }
        effects.push_back({{x,y,0.15f},radius,timeMs + EffectLifetimeMs,kind}); tower.consumed = true; return;
    }
    if (!target) return;
    if (weapon.contains("firesounds") && !weapon.at("firesounds").empty()) soundEvents.push_back(weapon.at("firesounds")[0].get<std::string>());
    const auto& wave = waveFor(*target);
    const Vec3 endpoint = hitPoint(*target,wave),origin = muzzle(tower);
    if (kind == "beam") {
        damage(*target,tower,static_cast<int>(baseDamage * tower.damageBoost * target->heat));
        if (wave.value("armor",0) != 4) target->heat = std::min(MaximumHeat,target->heat + HeatPerHit);
        beams.push_back({origin.x,origin.y,endpoint.x,endpoint.y,endpoint.z,timeMs + BeamLifetimeMs});
    } else if (kind == "railgun" || kind == "flamethrower") {
        lineDamage(x,y,target->x,target->y,definition.at("range").get<float>() * LineRangeFactor,weapon.at("radius").get<float>(),boosted,tower);
        if (kind == "flamethrower" && wave.value("armor",0) != 4) target->heat = std::min(MaximumHeat,target->heat + HeatPerHit);
        const float angle = std::atan2(target->y - y,target->x - x),length = definition.at("range").get<float>() * LineRangeFactor;
        beams.push_back({x,y,x + std::cos(angle) * length,y + std::sin(angle) * length,endpoint.z,timeMs + BeamLifetimeMs});
    } else if (kind == "flak") {
        const float tx = target->x + randomFloat() - 0.5f,ty = target->y + randomFloat() - 0.5f;
        randomFloat(); // The original consumes a third combat RNG sample for explosion height.
        radiusDamage(tx,ty,weapon.at("splash").get<float>(),static_cast<int>(baseDamage),tower);
        effects.push_back({{tx,ty,endpoint.z},weapon.at("splash").get<float>(),timeMs + EffectLifetimeMs,kind});
    } else if (kind == "slow") {
        for (auto& enemy : enemies) if (planar(target->x,target->y,enemy.x,enemy.y) < weapon.at("splash").get<float>())
            slow(enemy,weapon.at("factor").get<float>(),weapon.at("duration").get<int>());
        beams.push_back({origin.x,origin.y,endpoint.x,endpoint.y,endpoint.z,timeMs + BeamLifetimeMs});
    } else if (kind == "lightning") {
        // Original chain height samples span 0.5..1; Tesla pull decays by powers of 2.5.
        constexpr float ChainHeightMinimum = 0.5f, ChainHeightSpan = 0.5f, PullDecayBase = 2.5f;
        std::set<int> hit;
        Enemy* current = target;
        const float range = definition.at("range").get<float>();
        const float attracts = weapon.value("attracts",0.0f);
        // Every target is pulled toward the original muzzle, rather than the preceding enemy.
        const auto pull = [&](Enemy& enemy, float amount) {
            const Vec3 direction = normalized(Vec3{origin.x - enemy.x,origin.y - enemy.y,0});
            attract(enemy,{direction.x,direction.y},amount);
        };
        hit.insert(current->id);
        damage(*current,tower,static_cast<int>(baseDamage));
        const Vec3 firstPoint{current->x,current->y,wave.value("height",0.0f) + wave.value("offset",0.0f) + ChainHeightSpan * randomFloat()};
        beams.push_back({origin.x,origin.y,firstPoint.x,firstPoint.y,firstPoint.z,timeMs + BeamLifetimeMs});
        if (attracts > 0) pull(*current,attracts);
        // Retain float damage between jumps; only each delivered hit is truncated to integer HP.
        float remainingDamage = baseDamage;
        for (int jump = 1; jump < weapon.at("jumps").get<int>(); ++jump) {
            releaseChildren();
            Enemy* next = nullptr;
            float nearestSquared = range * range;
            // Original active enemies are prepended, so newest objects occur first in the chain.
            for (auto candidateIt = enemies.rbegin(); candidateIt != enemies.rend(); ++candidateIt) {
                auto& candidate = *candidateIt;
                if (!candidate.alive || hit.contains(candidate.id)) continue;
                if (!(waveFor(candidate).value("flying",false) ? definition.value("antiair",false) : definition.value("antiground",false))) continue;
                const float dx = candidate.x - current->x,dy = candidate.y - current->y;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared < nearestSquared) { nearestSquared = distanceSquared; next = &candidate; }
            }
            if (!next) break;
            // Two independent random heights affect damage, not merely the rendered bolt.
            const float previousHeight = waveFor(*current).value("height",0.0f) + ChainHeightMinimum + ChainHeightSpan * randomFloat();
            const float nextHeight = waveFor(*next).value("height",0.0f) + ChainHeightMinimum + ChainHeightSpan * randomFloat();
            const Vec3 previous{current->x,current->y,previousHeight},point{next->x,next->y,nextHeight};
            const Vec3 difference = point - previous;
            const float attenuation = std::max(0.0f,1 - std::sqrt(dot(difference,difference)) / range);
            hit.insert(next->id);
            damage(*next,tower,static_cast<int>(remainingDamage * attenuation));
            beams.push_back({previous.x,previous.y,point.x,point.y,point.z,timeMs + BeamLifetimeMs});
            if (attracts > 0) pull(*next,static_cast<float>(2 * static_cast<double>(attracts) / std::pow(static_cast<double>(PullDecayBase),jump)));
            remainingDamage *= attenuation;
            current = next;
        }
    } else if (kind == "plasma" || kind == "homing" || kind == "mortar") {
        const int muzzles = kind == "homing" ? static_cast<int>(definition.value("muzzleoffset",nlohmann::json::array({{0,0,0}})).size()) : 1;
        const int volley = kind == "homing" ? weapon.value("volley",1) : 1;
        for (int muzzleIndex = 0; muzzleIndex < muzzles; ++muzzleIndex) for (int shot = 0; shot < volley; ++shot) {
            Projectile projectile; projectile.source = tower; projectile.target = target->id; projectile.kind = kind;
            projectile.position = muzzle(tower,muzzleIndex); projectile.bornAt = timeMs;
            projectile.damage = static_cast<int>(baseDamage * tower.damageBoost / (muzzles * volley));
            projectile.speed = weapon.value("speed",1.0f); projectile.splash = weapon.value("splash",0.0f);
            projectile.endpoint = endpoint;
            projectile.model = kind == "homing" ? (definition.value("ispulsemissile",0) ? "pulseburst" : "grenade")
                : kind == "mortar" ? (tower.type == "Mortar I" ? "grenade" : "mortarshell") : "";
            if (kind == "homing") {
                const float spread = weapon.value("spread",0.0f);
                projectile.position.x += spread * (randomFloat() - 0.5f);
                projectile.position.y += spread * (randomFloat() - 0.5f);
                projectile.position.z += spread * (randomFloat() - 0.5f);
                const float dx = target->x - projectile.position.x,dy = target->y - projectile.position.y;
                const float side = 0.4f * (randomFloat() - 0.5f),forward = 0.2f + 0.25f * randomFloat();
                const float distance = std::hypot(dx,dy),heightSample = 0.5f * distance * randomFloat();
                projectile.waypoint = {projectile.position.x + dx * forward - dy * side,
                                       projectile.position.y + dy * forward + dx * side,
                                       heightSample >= 4.5f ? 5 : 0.5f + 0.5f * distance * randomFloat()};
                projectile.waypointReached = false;
            } else if (kind == "mortar") {
                Vec2 aim; projectile.speed = ballisticLaunch(tower,*target,aim);
                projectile.elevation = definition.value("elevation",45.0f) * FullTurn / 360;
                projectile.speedMultiplier = weapon.value("speedmultiplier",1.0f);
                const Vec3 heading = normalized(Vec3{aim.x - projectile.position.x,aim.y - projectile.position.y,0});
                projectile.velocity = {heading.x * std::cos(projectile.elevation) * projectile.speed,
                                       heading.y * std::cos(projectile.elevation) * projectile.speed,std::sin(projectile.elevation) * projectile.speed};
                if (tower.type == "Nuke" || tower.type == "H Bomb") {
                    projectile.model = tower.type == "Nuke" ? "nukemissile" : "hbombmissile";
                    projectile.waypointReached = false;
                }
            }
            projectile.previous = projectile.origin = projectile.position;
            projectiles.push_back(projectile);
        }
    }
}

// Acquire the nearest sheep-gate target and preserve original reload and turn limits.
void Game::attack() {
    if (timeMs - lastBoostAt_ > 3000) updateBoosts();
    for (auto& tower : towers) {
        if (tower.selling || tower.consumed || timeMs <= tower.readyAt) continue;
        const auto& definition = technology.at("towers").at(tower.type);
        const auto& weapon = technology.at("weapons").at(definition.at("weapon").get<std::string>());
        const std::string kind = weapon.at("type").get<std::string>();
        if (kind == "boost") continue;
        const float range = definition.at("range").get<float>();
        const auto valid = [&](const Enemy& enemy, bool acquiring) {
            const float distance = planar(tower.x + 1.0f,tower.y + 1.0f,enemy.x,enemy.y);
            return enemy.alive && (waveFor(enemy).value("flying",false) ? definition.value("antiair",false) : definition.value("antiground",false))
                   && (acquiring ? distance < range : distance <= range);
        };
        Enemy* target = nullptr;
        for (auto& enemy : enemies) if (enemy.id == tower.target && valid(enemy,false)) { target = &enemy; break; }
        if (!target) {
            float shortest = std::numeric_limits<float>::infinity();
            for (auto candidateIt = enemies.rbegin(); candidateIt != enemies.rend(); ++candidateIt) {
                auto& candidate = *candidateIt;
                if (!valid(candidate,true)) continue;
                const auto& field = waveFor(candidate).value("flying",false) ? flyingDistances_ : distances_;
                const int cx = static_cast<int>(candidate.x),cy = static_cast<int>(candidate.y);
                if (cx < 0 || cy < 0 || cx >= width || cy >= height) continue;
                const float distance = field[cx + cy * width];
                if ((distance > 0 || kind == "bait" || kind == "mine") && distance < shortest) { shortest = distance; target = &candidate; }
            }
        }
        tower.target = target ? target->id : -1;
        if (kind == "mine" || kind == "bait") { fire(tower,target); releaseChildren(); continue; }
        if (!target) continue;
        const float desired = std::atan2(target->y - tower.y - 1,target->x - tower.x - 1);
        const float difference = std::remainder(desired - tower.heading,FullTurn);
        tower.heading += difference * TurnFactor * SimulationStepMs / 1000.0f;
        const bool turns = kind != "lightning";
        if ((!turns || std::abs(difference) < FireTolerance) && timeMs > tower.nextShotAt) {
            tower.nextShotAt = timeMs + definition.value("reload",0); fire(tower,target);
            releaseChildren();
        }
    }
}
} // namespace tmreborn
