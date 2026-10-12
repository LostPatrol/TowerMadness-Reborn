// Silent native Windows entry point, desktop controls, and original-resource scene.
#include "game.hpp"
#include "renderer.hpp"
#include "catalog.hpp"
#include "profile.hpp"
#include "audio.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <psapi.h>

namespace tmreborn {
namespace {
// HUD geometry is in SDL logical pixels so mouse interaction follows DPI scaling.
constexpr float ButtonWidth = 132, ButtonHeight = 34, ButtonGap = 10;
// At most four simulation seconds are processed per frame after a desktop stall.
constexpr double MaximumFrameSeconds = 1.0;
// Tower/projectile meshes face local negative Y; enemies retain native yaw directly.
constexpr float TowerHeadingOffset = 1.5707963f, DegreesToRadians = 0.0174532924f;
// TOWERS_draw raises the body above its separate ground-level base.
constexpr float TowerGroundOffset = 0.25f;
// Sheep grazing uses an approximate desktop cadence pending original animation timing.
constexpr int SheepFrameMs = 400;
// Selection pages keep large packaged catalogs manageable at ordinary window sizes.
constexpr int ModesPerPage = 12;
// Menu, palette, and upgrade actions use separate ranges from the legacy six controls.
constexpr int ModeActionBase = 1000, EndlessActionBase = 2000, PaletteActionBase = 3000, UpgradeActionBase = 4000;
// Reserve the right edge for details and the bottom rows for original tower cards.
constexpr float DetailsWidth = 278, PaletteCardWidth = 168, PaletteRowHeight = 58;
// HUD colors are shared by menu, battle, and modal panels.
constexpr Color PanelColor{0.06f,0.10f,0.14f,0.96f}, AccentColor{0.55f,0.88f,0.62f,1};
// Original longitudinal effect meshes face +Z before their X-axis aim rotation.
constexpr float EffectHalfPi = 1.57079633f, EffectPi = 3.14159265f;
// Native death animation uses a three-second countdown split at thirty percent.
constexpr float CorpseSeconds = 3, DeathFirstPhase = 0.3f, DeathLastPhase = 0.7f;
// Original ENEMIES_draw hop literal differs slightly from mathematical pi.
constexpr float DeathHopPi = 3.141f;
// Original living slow/stun tint, independent of the desktop status rings.
constexpr Color EnemyStatusTint{0.7647059f,0.8509804f,1,1};

struct Desktop {
    bool menu = true;
    bool restartConfirm = false, quitRequested = false, resultsDismissed = false;
    bool exitSaveFailed = false;
    int page = 0, category = 0;
    std::string buildType;
    std::string previewMode, menuMessage, completedMode;
    std::vector<std::string> newUnlocks;
    std::unique_ptr<Game> preview;
    std::vector<CatalogEntry> catalog;
    std::vector<std::string> visibleModes;
};

// Find a command-line switch without treating a following path as another switch.
bool hasArgument(int argc, char** argv, const std::string& wanted) {
    for (int index = 1; index < argc; ++index) if (argv[index] == wanted) return true;
    return false;
}

// Convert SDL's UTF-8 paths to native Windows paths, including Chinese installation folders.
std::filesystem::path utf8Path(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}

// Anchor runtime assets to the executable regardless of the launch working directory.
std::filesystem::path executableDirectory() {
    const char* base = SDL_GetBasePath();
    if (!base) throw std::runtime_error(SDL_GetError());
    return utf8Path(base);
}

// Test a logical-pixel point against the same button rectangle that is drawn.
bool inside(const HudButton& button, float x, float y) {
    return button.enabled && x >= button.x && y >= button.y && x < button.x + button.width && y < button.y + button.height;
}

// Keep disabled controls opaque to mouse input as well as enabled controls.
bool contains(const HudButton& button, float x, float y) {
    return x >= button.x && y >= button.y && x < button.x + button.width && y < button.y + button.height;
}

// Detect the frozen end state once for commands and their visible availability.
bool ended(const Game& game) {
    return game.outcome == Outcome::Won || game.outcome == Outcome::Lost;
}

// Size the shared palette from the current technology and logical window width.
float paletteTop(const Game& game, int width, int height) {
    const int columns = std::max(1,static_cast<int>((width - 36) / PaletteCardWidth));
    const int rows = (static_cast<int>(game.baseTowers().size()) + columns - 1) / columns;
    return height - 60.0f - std::max(1,rows) * PaletteRowHeight;
}

// Use the visible board rectangle for placement, zoom, and right-drag initiation.
bool onBoard(const Game& game, int width, int height, float x, float y) {
    return x >= 0 && x < width - DetailsWidth - 12 && y > 108 && y < paletteTop(game,width,height) - 8;
}

// Translate recovered weapon families into short player-facing descriptions.
std::string weaponName(const Game& game, const std::string& type) {
    const auto& definition = game.technology.at("towers").at(type);
    const auto& weapon = game.technology.at("weapons").at(definition.at("weapon").get<std::string>());
    const std::string kind = weapon.value("type",std::string{});
    if (kind == "beam") return "激光";
    if (kind == "plasma") return "等离子";
    if (kind == "railgun") return "轨道炮";
    if (kind == "homing") return "导弹";
    if (kind == "mortar") return "迫击炮";
    if (kind == "flak") return "防空炮";
    if (kind == "flamethrower") return "火焰";
    if (kind == "lightning") return "闪电";
    if (kind == "mine") return "地雷";
    if (kind == "slow") return "减速";
    if (kind == "boost") return "增幅";
    if (kind == "bait") return "诱饵";
    return type;
}

// Reuse the packaged tower pictures without introducing new art dependencies.
std::string towerIcon(const std::string& family) {
    if (family == "激光") return "tower-laser.png";
    if (family == "等离子") return "tower-plasmatron.png";
    if (family == "轨道炮") return "tower-railgun.png";
    if (family == "导弹") return "tower-missile.png";
    if (family == "迫击炮") return "tower-mortar.png";
    if (family == "防空炮") return "tower-flak.png";
    if (family == "火焰") return "tower-flamethrower.png";
    if (family == "闪电") return "tower-electroshock.png";
    if (family == "地雷") return "tower-landmine.png";
    if (family == "减速") return "tower-electroslow.png";
    if (family == "增幅") return "tower-electroboost.png";
    if (family == "诱饵") return "tower-flashbait.png";
    return {};
}

// Give the campaign clear names while retaining readable names for packaged extras.
std::string levelName(const std::string& id) {
    if (id == "PlainPlane") return "平原";
    if (id == "DividedAttention") return "分兵防守";
    if (id == "MuttonsFolly") return "羊群困境";
    if (id == "Madness") return "疯狂挑战";
    if (id.starts_with("Sandbox:")) return "沙盒 · 当前地图";
    if (id.starts_with("Endless:")) return id.find("Easy") != std::string::npos ? "无限 · 简单" : id.find("Medium") != std::string::npos ? "无限 · 困难" : "无限 · 疯狂";
    std::string result;
    for (const char letter : id) {
        if (letter >= 'A' && letter <= 'Z' && !result.empty() && result.back() != ' ') result += ' ';
        result += letter;
    }
    return result;
}

// Format short numeric tower statistics at a stable, readable precision.
std::string number(float value) {
    std::ostringstream stream; stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

// Color a placement result and translate the normal command feedback.
std::string actionReason(const std::string& reason) {
    if (reason.empty()) return "可以建造";
    if (reason.starts_with("Choose a tower")) return "选择防御塔，在草地布防，再点击发波。";
    if (reason.starts_with("Wave ")) return "第 " + reason.substr(5,reason.find(' ',5) - 5) + " 波来袭。";
    if (reason.starts_with("Both UFOs")) return "飞碟正在投放敌人，下一波等待降落点空闲。";
    if (reason == "Saved game restored.") return "已继续保存的游戏。";
    if (reason.starts_with("All original waves")) return "所有波次已结束，羊群安全！";
    if (reason.starts_with("The sheep are gone")) return "羊群被带走了，重新布防再试一次。";
    if (reason.starts_with("Building ")) return "防御塔开始建造。";
    if (reason.starts_with("Upgrading ")) return "防御塔开始升级。";
    if (reason.starts_with("Selling tower")) return "正在拆除，完成后返还金币。";
    if (reason.find("dismantled") != std::string::npos) return "防御塔正在拆除。";
    if (reason.find("no longer") != std::string::npos) return "先选择已有的防御塔。";
    if (reason.find("gold") != std::string::npos) return "金币不足";
    if (reason.find("route") != std::string::npos) return "会堵住通往羊圈的道路";
    if (reason.find("occupied") != std::string::npos || reason.find("overlap") != std::string::npos) return "与已有防御塔重叠";
    if (reason.find("terrain") != std::string::npos || reason.find("buildable") != std::string::npos || reason.find("map") != std::string::npos) return "需要四格可建造草地";
    if (reason.find("finish") != std::string::npos) return "等待建造或拆除完成";
    if (reason.find("fully") != std::string::npos) return "已升至最高级";
    if (reason.find("disabled") != std::string::npos) return "当前科技不可用";
    if (reason.find("Restart") != std::string::npos || reason.find("ended") != std::string::npos) return "本局已结束";
    return reason;
}

// Compose the recovered local death transforms after original size and yaw.
Mat4 deathTransform(const EnemyCorpse& corpse, const nlohmann::json& wave) {
    const float q = corpse.remainingSeconds / CorpseSeconds, q3 = (q * q) * q;
    const float t = 1 - q, size = wave.at("size").get<float>(), angle = corpse.deathAngleDegrees;
    if (wave.value("armor",0) == 4) return modelMatrix({},0,q3 * q3);
    const bool flying = wave.value("flying",false), early = t < DeathFirstPhase;
    const float height = wave.value("height",0.0f);
    if (early) {
        const float e = t / DeathFirstPhase, e2 = e * e;
        if (flying) return modelMatrix({},0,q3) * modelMatrix({0,0,-(4 * size + height) * (e2 * 0.5f)},e2 * angle * 4 * DegreesToRadians,1) * rotationY(e2 * 90 * DegreesToRadians);
        if (wave.at("speed").get<float>() <= 1) return modelMatrix({},e2 * angle * DegreesToRadians,1) * rotationY(e2 * -90 * DegreesToRadians);
        const float f = std::min(e + e,1.0f);
        return modelMatrix({0,0,0.2f * size + std::sin(f * DeathHopPi) * 0.5f},0,1,f * (angle < 0 ? -180 : 180) * DegreesToRadians)
            * rotationY(f * (angle < 0 ? -20 : 10) * DegreesToRadians);
    }
    const float v = (t - DeathFirstPhase) / DeathLastPhase;
    if (flying) return modelMatrix({0,0,-v - (4 * size + height)},angle * DegreesToRadians,1) * rotationY(90 * DegreesToRadians);
    if (wave.at("speed").get<float>() <= 1) return modelMatrix({0,0,-v},angle * DegreesToRadians,1) * rotationY(-90 * DegreesToRadians);
    const Mat4 fallen = modelMatrix({0,0,0.2f * size - v},0,1,(angle < 0 ? -180 : 180) * DegreesToRadians);
    // The original negative-angle branch intentionally drops its late Y rotation.
    return angle < 0 ? fallen : fallen * rotationY(10 * DegreesToRadians);
}

// Draw the original ground projection independently of enemy heading and body pose.
void enemyShadow(SceneView& scene, const Enemy& enemy, const nlohmann::json& wave, float remaining = CorpseSeconds) {
    if (wave.value("flying",false)) return;
    const float q = remaining / CorpseSeconds;
    const float scale = (2 * wave.at("size").get<float>() / static_cast<float>(enemy.generation + 1)) * ((q * q) * q);
    scene.models.push_back({"directionalshadow",{enemy.x,enemy.y,0.01f},-90 * DegreesToRadians,scale});
    scene.models.back().unlit = true;
}

// Use a separate deterministic visual sample without consuming battle RNG.
float visualRandom(std::uint32_t seed) {
    seed ^= seed >> 16; seed *= 0x7feb352du;
    seed ^= seed >> 15; seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    return static_cast<float>(seed) * 0x1p-32f;
}

// Keep one effect's random width and direction stable throughout its lifetime.
std::uint32_t visualSeed(int bornAt, Vec3 origin) {
    return static_cast<std::uint32_t>(bornAt) ^ std::bit_cast<std::uint32_t>(origin.x)
        ^ (std::bit_cast<std::uint32_t>(origin.y) * 31u) ^ std::bit_cast<std::uint32_t>(origin.z);
}

// Match recovered laser and rail tier colors rather than reusing one red ribbon.
Color beamColor(const Beam& beam) {
    const auto& type = beam.sourceType;
    if (type == "Kilowatt Laser" || type == "Railgun I") return {1,0,0,1};
    if (type == "Megawatt Laser" || type == "Railgun II") return {1,116.0f / 255,0,1};
    if (type == "Gigawatt Laser" || type == "Railgun III") return {1,1,0,1};
    if (type == "Terawatt Laser" || type == "Railgun IV") return {0,1,0,1};
    if (beam.kind == "railgun" && type != "Railgun V") return {159.0f / 255,0,1,1};
    return {0,0,1,1};
}

// Compose original effect meshes with explicit radial and longitudinal scale.
Renderable& effectModel(SceneView& scene, const std::string& model, Vec3 position, Vec3 scale, Color color = {}, float yaw = 0, float pitch = 0) {
    scene.models.push_back({model,position,yaw,1,color});
    auto& object = scene.models.back();
    object.pitch = pitch; object.animationTransform = scaleMatrix(scale);
    object.overrideMaterialColor = true; object.unlit = true;
    return object;
}

// Draw desktop status aids as actual circles rather than stretching a ring atlas into a quad.
void statusRing(SceneView& scene, Vec3 center, float radius, Color color) {
    constexpr int Segments = 48;
    for (int segment = 0; segment < Segments; ++segment) {
        const float a = segment * 2 * EffectPi / Segments, b = (segment + 1) * 2 * EffectPi / Segments;
        scene.lines.push_back({center + Vec3{std::cos(a) * radius,std::sin(a) * radius,0},
            center + Vec3{std::cos(b) * radius,std::sin(b) * radius,0},color});
    }
}

// Assemble original mesh instances and the shared desktop command HUD.
SceneView makeScene(const Game& game, int selectedId, std::optional<Cell> hover, int speed, int windowWidth, int windowHeight) {
    SceneView scene;
    const Tower* selected = nullptr;
    for (const auto& tower : game.towers) {
        const auto& definition = game.technology.at("towers").at(tower.type);
        const bool waiting = game.timeMs <= tower.readyAt;
        scene.sprites.push_back({"shadowblob.pvr.png",{tower.x + 1.0f,tower.y + 1.0f,0.035f},2,2,{0,0,0,0.28f},true});
        const std::string kind = game.technology.at("weapons").at(definition.at("weapon").get<std::string>()).at("type").get<std::string>();
        if (!waiting && (kind == "boost" || tower.damageBoost > 1)) {
            const float radius = kind == "boost" ? definition.at("range").get<float>() : 1.1f;
            statusRing(scene,{tower.x + 1.0f,tower.y + 1.0f,0.06f},radius,
                {0.35f,1,0.6f,0.4f + 0.1f * std::sin(game.timeMs * 0.006f)});
        }
        const bool directional = kind != "lightning" && kind != "boost" && kind != "mine" && kind != "bait" && tower.type != "Nuke" && tower.type != "H Bomb";
        const float yaw = directional ? tower.heading + TowerHeadingOffset : 0;
        const float pitch = directional ? -game.towerElevation(tower) : 0;
        scene.models.push_back({definition.at("model").get<std::string>(),{tower.x + 1.0f,tower.y + 1.0f,TowerGroundOffset},yaw,1,
                                tower.selling ? Color{1,0.45f,0.4f,0.7f} : waiting ? Color{0.6f,0.85f,1,0.65f} : Color{}});
        if (definition.contains("turret")) {
            scene.models.push_back({definition.at("turret").get<std::string>(),
                {tower.x + 1.0f,tower.y + 1.0f,TowerGroundOffset},yaw});
            if (definition.contains("rotationoffset")) {
                const auto& pivot = definition.at("rotationoffset");
                scene.models.back().pitchPivot = {pivot[0].get<float>(),pivot[1].get<float>(),pivot[2].get<float>()};
            }
        }
        scene.models.back().pitch = pitch;
        if (definition.contains("towerbase")) {
            const int base = definition.at("towerbase").get<int>();
            scene.models.push_back({base == 0 ? "turretbase" : "turretbase_" + std::to_string(base),{tower.x + 1.0f,tower.y + 1.0f,0}});
        }
        if (tower.id == selectedId) selected = &tower;
    }
    for (const auto& enemy : game.enemies) {
        if (!enemy.alive) continue;
        const auto& wave = game.waveFor(enemy);
        const auto& frames = enemy.generation == 1 && wave.contains("subwalk") && !wave.at("subwalk").empty() ? wave.at("subwalk") : wave.at("walk");
        const int frame = game.walkFrame(enemy);
        const std::string model = frames.empty() ? enemy.model : frames[frame].get<std::string>();
        const float size = wave.at("size").get<float>();
        enemyShadow(scene,enemy,wave);
        scene.models.push_back({model,{enemy.x,enemy.y,wave.value("height",0.0f)},enemy.visualHeadingDegrees * DegreesToRadians,size,
            enemy.stunMs > 0 || enemy.slowMs > 0 ? EnemyStatusTint : Color{}});
        scene.models.back().overrideMaterialColor = true;
        const int armor = wave.value("armor",0);
        if (armor >= 1 && armor <= 3) {
            scene.models.push_back({armor == 1 ? "bionicaura" : armor == 2 ? "lightaura" : "heavyaura",
                {enemy.x,enemy.y,0.01f},0,2 * size / static_cast<float>(enemy.generation + 1)});
            scene.models.back().unlit = true;
        }
        if (enemy.stunMs > 0 || enemy.slowMs > 0) {
            const Color color = enemy.stunMs > 0 ? Color{0.72f,0.4f,1,0.65f} : Color{0.25f,0.85f,1,0.55f};
            statusRing(scene,{enemy.x,enemy.y,0.055f},size * 0.95f,color);
        }
    }
    for (const auto& corpse : game.corpses()) {
        if (!corpse.drawable) continue;
        const auto& enemy = corpse.enemy;
        const auto& wave = game.waveFor(enemy);
        const auto& frames = enemy.generation == 1 && wave.contains("subwalk") && !wave.at("subwalk").empty() ? wave.at("subwalk") : wave.at("walk");
        const std::string model = frames.empty() ? enemy.model : frames.at(corpse.frame).get<std::string>();
        const float q = corpse.remainingSeconds / CorpseSeconds;
        enemyShadow(scene,enemy,wave,corpse.remainingSeconds);
        scene.models.push_back({model,{enemy.x,enemy.y,wave.value("height",0.0f)},enemy.visualHeadingDegrees * DegreesToRadians,wave.at("size").get<float>(),{q,q,q,1}});
        scene.models.back().animationTransform = deathTransform(corpse,wave);
        scene.models.back().overrideMaterialColor = true;
    }
    const auto& enclosure = game.map.at("enclosure");
    for (int index = 0; index < game.lives; ++index) {
        if (enclosure.empty()) break;
        const auto& cell = enclosure.at((index * 2) % enclosure.size());
        scene.models.push_back({"sheep_anim_graze" + std::to_string((game.timeMs / SheepFrameMs + index) % 4),
            {cell[0].get<float>() + 0.5f,cell[1].get<float>() + 0.5f,0},index * 0.7f,0.6f});
    }
    for (const auto& ufo : game.ufos)
        if (ufo.state != Ufo::State::Idle) scene.models.push_back({"ufo",{ufo.x,ufo.y,ufo.z},0,1});
    // Native models and age formulas retain the desktop simulation-clock adaptation.
    for (const auto& beam : game.beams) if (!ended(game)) {
        const Vec3 from{beam.fromX,beam.fromY,beam.fromZ};
        const float u = std::clamp(static_cast<float>(game.timeMs - beam.bornAt) / std::max(1,beam.expiresAt - beam.bornAt),0.0f,1.0f);
        const float pitch = EffectHalfPi - beam.elevation, length = beam.length;
        const std::uint32_t seed = visualSeed(beam.bornAt,from);
        if (beam.kind == "beam") {
            effectModel(scene,"beam2",from,{0.5f * u,0.5f * u,length},beamColor(beam),beam.heading,pitch);
        } else if (beam.kind == "railgun") {
            const float remaining = (1 - u) * (1 - u) * (1 - u), progress = 1 - remaining;
            if (u <= 0.1f) {
                const float width = u * (0.5f * beam.radius + 6);
                effectModel(scene,"beam2",from,{width,width,10 * u * length},{1,1,1,1 - 10 * u},beam.heading,pitch);
            }
            Color color = beamColor(beam); color.a = 2 * remaining;
            const float width = progress * (0.15f * beam.radius + 0.25f);
            effectModel(scene,"railspiral",from,{width,width,(u <= 0.1f ? 10 * u : 1) * length},color,beam.heading,pitch);
        } else if (beam.kind == "lightning" || beam.kind == "slow") {
            const bool slow = beam.kind == "slow";
            const bool tesla = !slow && game.technology.at("towers").at(beam.sourceType).value("istesla",0) != 0;
            const float width = slow ? 0.35f + 0.15f * visualRandom(seed) : (0.75f + 0.25f * visualRandom(seed)) * std::clamp(0.25f * length,1.0f,2.5f);
            for (int layer = 0; layer < 2; ++layer) {
                const Color color = layer == 0 ? (tesla ? Color{1,1,136.0f / 255,1} : Color{195.0f / 255,217.0f / 255,1,1})
                    : slow ? Color{64.0f / 255,150.0f / 255,238.0f / 255,1} : tesla ? Color{1,1,0,1} : Color{};
                auto& object = effectModel(scene,tesla ? "$effect:lightning2" : "$effect:lightning",from,
                    {width * (layer == 0 ? 1.25f : 1),width * (layer == 0 ? 1.25f : 1),length},color,beam.heading,pitch);
                object.atlasFrames = 4; object.atlasLayer = layer; object.visualSeed = seed;
            }
        } else if (beam.kind == "flamethrower") {
            const float phase = u * u * u, yaw = beam.heading - EffectHalfPi;
            const Vec3 velocity{length * std::cos(beam.elevation) * std::cos(yaw) + visualRandom(seed + 3),
                length * std::cos(beam.elevation) * std::sin(yaw) + visualRandom(seed + 2),length * std::sin(beam.elevation) + visualRandom(seed + 1)};
            const float growth = 1 - std::pow(1 - 1.1f * phase,3.0f);
            const float alpha = u < 0.2f ? u / 0.2f : u < 0.7f ? 1 : 1 - (u - 0.7f) / 0.3f;
            scene.sprites.push_back({"fireball.pvr.png",from + velocity * phase,1,1,{1,1,1,alpha},false,true,128 + 1024 * growth});
        }
    }
    for (const auto& projectile : game.projectiles) {
        if (!projectile.model.empty()) {
            scene.models.push_back({projectile.model,projectile.position,std::atan2(projectile.velocity.y,projectile.velocity.x) + TowerHeadingOffset,0.5f});
            scene.models.back().pitch = -std::atan2(projectile.velocity.z,std::hypot(projectile.velocity.x,projectile.velocity.y));
        }
        if (projectile.kind == "plasma") {
            const int frame = static_cast<int>(game.timeMs * 0.01f) % 4;
            scene.sprites.push_back({frame == 0 ? "plasmaball.pvr.png" : "plasmaball" + std::to_string(frame + 1) + ".pvr.png",projectile.position,1,1,{},false,false,
                projectile.damage < 450 ? 164 + 0.2f * projectile.damage : 254});
        }
        else scene.sprites.push_back({"smokepuff.pvr.png",projectile.previous,0.32f,0.32f,{0.85f,0.85f,0.85f,0.45f}});
    }
    for (const auto& effect : game.effects) if (!ended(game)) {
        const float age = std::clamp(static_cast<float>(game.timeMs - effect.bornAt) / std::max(1,effect.expiresAt - effect.bornAt),0.0f,1.0f);
        const float radius = effect.radius > 0 ? effect.radius : 0.4f,diameter = radius * (0.5f + age * 1.5f);
        if (effect.kind == "death") scene.sprites.push_back({"smokepuff.pvr.png",effect.position + Vec3{0,0,age * 0.7f},diameter,diameter,{0.65f,0.7f,0.6f,1 - age}});
        else if (effect.kind == "sheepLeak") scene.sprites.push_back({"life.pvr.png",effect.position + Vec3{0,0,age * 1.5f},1,1,{1,0.2f,0.2f,1 - age}});
        else if (effect.kind == "plasmaFlash" || effect.kind == "mortarFlash") {
            auto& object = effectModel(scene,"$effect:muzzlefx",effect.position,{0.5f * age,0.5f * age,age},{1,1,1,1 - age * age * age},effect.heading,EffectHalfPi - effect.elevation);
            object.textureScale = {1,0.25f}; object.textureOffset = {0,effect.kind == "plasmaFlash" ? 0.5f : 0.75f};
        } else if (effect.kind == "groundDamage") {
            effectModel(scene,"crater",{effect.position.x,effect.position.y,0.02f},{radius,radius,1},{1,1,1,1 - age * age * age},visualRandom(visualSeed(effect.bornAt,effect.position)) * 2 * EffectPi);
        } else if (effect.kind == "flak") {
            const std::uint32_t seed = visualSeed(effect.bornAt,effect.position);
            const float scale = (0.5f + visualRandom(seed)) * radius;
            scene.sprites.push_back({visualRandom(seed + 1) < 0.5f ? "flakburst1.pvr.png" : "flakburst2.pvr.png",effect.position,1,1,{1,1,1,1 - age * age * age},false,false,
                (age < 0.1f ? 2560 * age : 256 + 128 * age) * scale});
        } else if (effect.kind == "nuke") {
            const float yaw = visualRandom(visualSeed(effect.bornAt,effect.position)) * 2 * EffectPi;
            const bool hydrogen = effect.sourceType == "H Bomb";
            const Vec3 ground{effect.position.x,effect.position.y,0};
            if (age < 0.333f) effectModel(scene,"nukeshockwave",ground + Vec3{0,0,0.13f},{0.1f + 28 * age,0.1f + 28 * age,1},{1,1,1,1 - 3 * age});
            if (age < 0.5f) effectModel(scene,"debris",ground + Vec3{0,0,0.08f},{1,1,1},{1,1,1,1 - 2 * age}).scale = 0.7f * radius * std::sin(EffectPi * age);
            const float dust = 2 * radius * std::sin(EffectPi * age / 2);
            effectModel(scene,"nukedust",ground + Vec3{0,0,0.08f},{dust,dust,dust},{68.0f / 255,68.0f / 255,68.0f / 255,age < 0.5f ? 4 * age * age : 2 - 2 * age});
            const float light = std::sin(std::sqrt(EffectPi * EffectPi * age));
            auto& cloud = effectModel(scene,hydrogen ? "mushroomcloud_hbomb" : "mushroomcloud",ground,{2 + age,2 + age,0.1f + 5 * age},{light,light,light,1},
                yaw + (hydrogen ? 2 * EffectPi * (0.5f - 0.5f * std::cos(age * 3.141592502593994f)) : 0));
            const float uvScale = 1 - 0.7f * (1 - age);
            cloud.textureScale = {uvScale,uvScale}; cloud.textureOffset = {(1 - uvScale) / 2,(1 - uvScale) / 2};
        }
        else if (effect.kind == "bait" || effect.kind == "split")
            effectModel(scene,"rangecircle",{effect.position.x,effect.position.y,0.07f},{diameter,diameter,1},{0.65f,0.3f,1,1 - age});
        else if (effect.kind == "mine" || effect.kind == "mortar") {
            const Vec3 ground{effect.position.x,effect.position.y,0};
            const float a = radius * (1 - 10 * age) + 0.7f * radius * (10 * age), b = 0.1f * radius * (1 - age) + radius * age;
            if (age <= 0.1f) {
                effectModel(scene,"blast",ground + Vec3{0,0,0.02f},{4 * a,4 * a,4 * a},{1,1,1,1 - 10 * age});
                effectModel(scene,"flash",ground + Vec3{0,0,0.3f},{a,a,a},{1,1,1,1 - 10 * age});
            }
            if (age < 0.5f) effectModel(scene,"rangecircle",ground + Vec3{0,0,0.2f},{4 * b,4 * b,1},{1,1,1,1 - 2 * age});
            effectModel(scene,"dustcone",ground + Vec3{0,0,0.02f},{2 * age * radius,2 * age * radius,std::sin(EffectPi * age) * radius},
                {17.0f / 255,17.0f / 255,17.0f / 255,1 - age * age * age});
        } else {
            scene.sprites.push_back({"fireball.pvr.png",effect.position,diameter,diameter,{1,0.8f,0.6f,1 - age},false,true});
            scene.sprites.push_back({"smokepuff.pvr.png",effect.position + Vec3{0,0,age},diameter,diameter,{0.85f,0.8f,0.75f,(1 - age) * 0.55f}});
        }
    }
    if (hover) scene.selectedCell = Vec2{static_cast<float>(hover->x),static_cast<float>(hover->y)};
    if (selected) {
        scene.selectedCell = Vec2{static_cast<float>(selected->x),static_cast<float>(selected->y)};
        const float range = game.technology.at("towers").at(selected->type).at("range").get<float>();
        // This ring is a desktop selection aid; the simulation uses the original radius.
        constexpr int RingSegments = 64;
        constexpr float FullCircle = 6.2831853f;
        for (int segment = 0; segment < RingSegments; ++segment) {
            const float a = segment * FullCircle / RingSegments,b = (segment + 1) * FullCircle / RingSegments;
            scene.lines.push_back({{selected->x + 1 + std::cos(a) * range,selected->y + 1 + std::sin(a) * range,0.05f},
                                   {selected->x + 1 + std::cos(b) * range,selected->y + 1 + std::sin(b) * range,0.05f},{0.4f,0.85f,1,0.8f}});
        }
    }
    const std::string state = game.outcome == Outcome::Won ? "VICTORY" : game.outcome == Outcome::Lost ? "DEFEAT" : game.paused ? "PAUSED" : game.outcome == Outcome::Preparing ? "PREPARE" : "DEFEND";
    scene.texts.push_back({"TOWERMADNESS REBORN  /  " + game.modeId + "  /  " + state,18,14});
    scene.texts.push_back({"Gold: " + std::to_string(game.money) + "    Sheep: " + std::to_string(game.lives) + "    Wave: " + std::to_string(game.wavesSent) + "/" + (game.endless ? "Endless" : std::to_string(game.waves.size())) + "    Speed: " + std::to_string(speed) + "x",18,40});
    const float buttonY = 70;
    scene.buttons = {
        {"Send wave [N]",18,buttonY,ButtonWidth,ButtonHeight,false,game.outcome != Outcome::Won && game.outcome != Outcome::Lost && (game.endless || game.wavesSent < static_cast<int>(game.waves.size()))},
        {game.paused ? "Resume [Space]" : "Pause [Space]",18 + ButtonWidth + ButtonGap,buttonY,ButtonWidth,ButtonHeight,game.paused},
        {"Speed [F]",18 + 2 * (ButtonWidth + ButtonGap),buttonY,ButtonWidth,ButtonHeight},
        {"Restart [R]",18 + 3 * (ButtonWidth + ButtonGap),buttonY,ButtonWidth,ButtonHeight},
        {"Upgrade [U]",18 + 4 * (ButtonWidth + ButtonGap),buttonY,ButtonWidth,ButtonHeight,false,selected != nullptr},
        {"Sell [Del]",18 + 5 * (ButtonWidth + ButtonGap),buttonY,ButtonWidth,ButtonHeight,false,selected != nullptr}
    };
    scene.texts.push_back({selected ? selected->type + " | invested " + std::to_string(selected->refund) + " gold" : "Kilowatt Laser: 10 gold | Left click free grass to build, existing laser to select",18,112});
    scene.texts.push_back({game.message,18,static_cast<float>(windowHeight - 56),{1,0.92f,0.7f,1}});
    scene.texts.push_back({"WASD / right drag: camera   Wheel: zoom   C: reset   Esc: exit",18,static_cast<float>(windowHeight - 30),{0.75f,0.8f,0.85f,1}});
    (void)windowWidth;
    return scene;
}

// Translate one shared button or keyboard command into ordinary game actions.
void command(Game& game, int action, int& selectedId, int& speed) {
    switch (action) {
    case 0: game.sendWave(); break;
    case 1: if (!ended(game)) game.paused = !game.paused; break;
    case 2: if (!ended(game)) speed = speed == 1 ? 2 : speed == 2 ? 4 : 1; break;
    case 3: game.restart(); selectedId = -1; speed = 1; break;
    case 4: game.upgrade(selectedId); break;
    case 5: game.sell(selectedId); break;
    default: break;
    }
}

// Retain the original four campaign gates; packaged extras are a separate desktop library.
bool campaignAvailable(const CatalogEntry& entry, const Profile& profile) {
    const auto& completed = profile.data.at("completed");
    if (entry.id == "PlainPlane") return true;
    if (entry.id == "DividedAttention") return completed.contains("PlainPlane");
    if (entry.id == "MuttonsFolly") return completed.contains("DividedAttention");
    if (entry.id == "Madness") return completed.contains("MuttonsFolly");
    return false;
}

// Apply the same progression gate to previews, start buttons, and keyboard actions.
bool modeAvailable(const std::string& id, const Desktop& desktop, const Profile& profile) {
    if (id.starts_with("Endless:") || id.starts_with("Sandbox:"))
        return profile.data.at("completed").contains("DividedAttention");
    const auto entry = std::find_if(desktop.catalog.begin(),desktop.catalog.end(),[&](const CatalogEntry& value) { return value.id == id; });
    return entry != desktop.catalog.end() && (!entry->selectableKnown || campaignAvailable(*entry,profile));
}

// Return the immediate campaign successor without changing any unlock rules.
std::string nextCampaign(const std::string& id) {
    if (id == "PlainPlane") return "DividedAttention";
    if (id == "DividedAttention") return "MuttonsFolly";
    if (id == "MuttonsFolly") return "Madness";
    return {};
}

// Record completion once and retain only unlocks newly earned by this match.
void desktopCompletion(const Game& game, Desktop& desktop, Profile& profile) {
    if (game.outcome != Outcome::Won) { desktop.completedMode.clear(); desktop.newUnlocks.clear(); return; }
    if (desktop.completedMode == game.modeId) return;
    const bool first = !profile.data.at("completed").contains(game.modeId);
    desktop.completedMode = game.modeId; desktop.newUnlocks.clear();
    if (first) {
        const std::string next = nextCampaign(game.modeId);
        if (!next.empty()) desktop.newUnlocks.push_back(levelName(next));
        if (game.modeId == "PlainPlane") desktop.newUnlocks.push_back("进阶科技");
        if (game.modeId == "DividedAttention") desktop.newUnlocks.push_back("困难科技 / 无限挑战 / 沙盒");
        if (game.modeId == "MuttonsFolly") desktop.newUnlocks.push_back("疯狂科技");
    }
    profile.complete(game.modeId,game.lives,game.finalScore());
}

// Describe the real saved session without loading it or changing the current match.
std::string savedSummary(const Profile& profile) {
    if (!profile.data.contains("session")) return "暂无保存的游戏";
    const auto& session = profile.data.at("session");
    if (!session.is_object()) return "保存内容无法读取";
    try {
        const int outcome = session.value("outcome",0);
        const std::string state = outcome == 2 ? "胜利" : outcome == 3 ? "失败" : session.value("paused",false) ? "暂停" : outcome == 0 ? "准备" : "战斗中";
        return levelName(session.value("mode",std::string("PlainPlane"))) + " · 第" + std::to_string(session.value("wavesSent",0)) + "波 · " + state;
    } catch (const std::exception&) { return "保存内容无法读取 · 可开始新关卡"; }
}

// Load preview metadata separately so browsing never replaces the live defense.
void selectPreview(Desktop& desktop, const std::string& id) {
    desktop.previewMode = id; desktop.preview.reset(); desktop.menuMessage.clear();
    try { desktop.preview = std::make_unique<Game>(executableDirectory() / "assets",id); }
    catch (const std::exception& error) { desktop.menuMessage = "关卡预览无法载入：" + std::string(error.what()); }
}

// Show a compact, consistent desktop HUD while reusing the original scene and art.
SceneView desktopScene(Renderer& renderer, const Game& game, Desktop& desktop, const Profile& profile, int selectedId,
                       std::optional<Cell> hover, int speed, int width, int height) {
    SceneView scene = makeScene(game,selectedId,std::nullopt,speed,width,height);
    scene.texts.clear(); scene.buttons.clear();
    const float right = width - DetailsWidth, bottom = paletteTop(game,width,height);
    renderer.setBoardViewport(0,108,width - DetailsWidth - 12,bottom - 116);
    if (desktop.menu) {
        scene.models.clear(); scene.lines.clear(); scene.sprites.clear(); scene.ribbons.clear(); scene.selectedCell.reset();
        scene.panels.push_back({12,12,width - 24.0f,height - 24.0f,PanelColor});
        scene.texts.push_back({"TOWERMADNESS",32,26,AccentColor,34});
        scene.texts.push_back({"保卫羊群",34,70,{},22});
        scene.texts.push_back({desktop.category == 0 ? "战役 · 完成关卡解锁下一挑战" :
            desktop.category == 1 ? "附加关卡" : "无限挑战 · 使用当前地图",32,124,{},20});
        scene.buttons = {{"战役",32,166,130,36,desktop.category == 0,true,20},
                         {"附加关卡",172,166,130,36,desktop.category == 1,true,21},
                         {"无限挑战",312,166,130,36,desktop.category == 2,true,22},
                         {"返回当前局",width - 372.0f,166,340,36,false,true,10}};
        scene.buttons.push_back({"音效 " + std::to_string(static_cast<int>(profile.data.at("settings").value("volume",0.0f) * 100)) + "%",width - 372.0f,110,164,36,false,true,16});
        scene.buttons.push_back({"音乐 " + std::to_string(static_cast<int>(profile.data.at("settings").value("musicVolume",0.0f) * 100)) + "%",width - 196.0f,110,164,36,false,true,17});
        scene.buttons.push_back({desktop.exitSaveFailed ? "仍然退出（未保存）" : "退出游戏",width - 372.0f,height - 80.0f,340,36,false,true,18});
        desktop.visibleModes.clear();
        if (desktop.category == 0) desktop.visibleModes = {"PlainPlane","DividedAttention","MuttonsFolly","Madness"};
        else if (desktop.category == 1)
            for (const auto& entry : desktop.catalog) if (!entry.selectableKnown) desktop.visibleModes.push_back(entry.id);
        else for (const char* set : {"Endless_Easy","Endless_Medium","Endless_Hard","Sandbox"})
            desktop.visibleModes.push_back(std::string(set == std::string("Sandbox") ? "Sandbox:Endless_Hard:" : "Endless:" + std::string(set) + ':') + game.mode.at("map").get<std::string>());
        const int pages = std::max(1,(static_cast<int>(desktop.visibleModes.size()) + ModesPerPage - 1) / ModesPerPage);
        desktop.page = std::clamp(desktop.page,0,pages - 1);
        const float rowHeight = std::min(42.0f,(height - 320.0f) / ModesPerPage);
        for (int row = 0; row < ModesPerPage; ++row) {
            const int index = desktop.page * ModesPerPage + row;
            if (index >= static_cast<int>(desktop.visibleModes.size())) break;
            const std::string& id = desktop.visibleModes[index];
            const bool allowed = modeAvailable(id,desktop,profile);
            const std::string label = levelName(id) + (profile.data.at("completed").contains(id) ? " · 已完成" : allowed ? "" : " · 未解锁");
            scene.buttons.push_back({label,32,220 + row * rowHeight,width - 420.0f,rowHeight - 3,desktop.previewMode == id,true,ModeActionBase + index});
        }
        if (!desktop.visibleModes.empty() && std::find(desktop.visibleModes.begin(),desktop.visibleModes.end(),desktop.previewMode) == desktop.visibleModes.end())
            selectPreview(desktop,desktop.visibleModes[desktop.page * ModesPerPage]);
        const float previewX = width - 372.0f;
        if (desktop.preview) {
            const Game& preview = *desktop.preview;
            const std::string thumbnail = renderer.mapPreview(preview.mode.at("map").get<std::string>());
            if (!thumbnail.empty()) scene.images.push_back({thumbnail,previewX + 36,220,268,151});
            else scene.texts.push_back({"地图预览暂时不可用",previewX,286,{1,0.72f,0.5f,1},18});
            scene.texts.push_back({levelName(desktop.previewMode),previewX,382,AccentColor,22});
            const std::string tech = preview.mode.at("tech").get<std::string>();
            scene.texts.push_back({"科技 " + std::string(tech == "Easy" ? "普通" : tech == "Medium" ? "进阶" : tech == "Hard" ? "困难" : "疯狂") +
                " · " + (preview.endless ? "无限波次" : std::to_string(preview.waves.size()) + "波"),previewX,414,{},18});
            scene.texts.push_back({"初始 " + std::to_string(preview.money) + " 金币 · " + std::to_string(preview.lives) + " 只羊",previewX,442,{},18});
            const auto& completed = profile.data.at("completed");
            const std::string best = completed.contains(desktop.previewMode) ? "最佳 " + std::to_string(completed.at(desktop.previewMode).value("score",0)) + " 分 · " +
                std::to_string(completed.at(desktop.previewMode).value("sheep",0)) + " 只羊" : "最佳成绩：尚未完成";
            scene.texts.push_back({best,previewX,470,{},18});
            const bool allowed = modeAvailable(desktop.previewMode,desktop,profile);
            scene.buttons.push_back({allowed ? "开始所选关卡" : "完成前一战役后解锁",previewX,502,340,38,false,allowed,27});
        }
        scene.buttons.push_back({"继续保存的游戏",previewX,552,340,36,false,profile.data.contains("session"),12});
        scene.texts.push_back({savedSummary(profile),previewX,594,{0.8f,0.85f,0.9f,1},16});
        scene.buttons.push_back({"上一页",32,height - 100.0f,120,36,false,desktop.page > 0,14});
        scene.buttons.push_back({"下一页",162,height - 100.0f,120,36,false,desktop.page + 1 < pages,15});
        scene.texts.push_back({std::to_string(desktop.page + 1) + " / " + std::to_string(pages),302,height - 92.0f});
        scene.texts.push_back({!profile.warning.empty() ? profile.warning : !desktop.menuMessage.empty() ? desktop.menuMessage : "浏览关卡后点击开始 · 当前局保持不变",32,height - 42.0f,{1,0.85f,0.5f,1},16});
        return scene;
    }
    scene.panels.push_back({0,0,static_cast<float>(width),104,PanelColor});
    scene.panels.push_back({right,112,DetailsWidth - 12,bottom - 120,PanelColor});
    scene.panels.push_back({0,bottom - 8,static_cast<float>(width),height - bottom + 8,PanelColor});
    // Show health only when wounded; color also identifies active slow or stun status.
    for (const auto& enemy : game.enemies) if (enemy.alive && enemy.health < enemy.maxHealth && !ended(game)) {
        const auto& wave = game.waveFor(enemy);
        const auto anchor = renderer.project({enemy.x,enemy.y,wave.value("height",0.0f) + wave.value("offset",0.0f) + 1.0f});
        if (!anchor || !onBoard(game,width,height,anchor->x,anchor->y)) continue;
        const float fraction = static_cast<float>(static_cast<double>(enemy.health) / std::max(std::int64_t{1},enemy.maxHealth));
        const Color health = enemy.stunMs > 0 ? Color{0.8f,0.45f,1,1} : enemy.slowMs > 0 ? Color{0.3f,0.85f,1,1} : Color{0.4f,0.95f,0.35f,1};
        scene.panels.push_back({anchor->x - 15,anchor->y - 5,30,5,{0.05f,0.07f,0.09f,0.9f}});
        scene.panels.push_back({anchor->x - 14,anchor->y - 4,28 * std::clamp(fraction,0.0f,1.0f),3,health});
    }
    const std::string state = ended(game) ? (game.outcome == Outcome::Won ? "胜利" : "失败") : game.paused ? "已暂停" : game.wavesSent == 0 ? "准备防御" : "保卫羊群";
    scene.texts.push_back({levelName(game.modeId) + " · " + state,18,12,AccentColor,22});
    scene.texts.push_back({"金币 " + std::to_string(game.money) + "     羊群 " + std::to_string(game.lives) + "     波次 " + std::to_string(game.wavesSent) + "/" +
        (game.endless ? "∞" : std::to_string(game.waves.size())),18,42,{},20});
    const bool wavesRemain = game.endless || game.wavesSent < static_cast<int>(game.waves.size());
    const bool canSend = !ended(game) && wavesRemain && std::any_of(game.ufos.begin(),game.ufos.end(),[](const Ufo& ufo) { return ufo.state == Ufo::State::Idle; });
    scene.buttons = {{"发波 [N]",18,70,116,28,false,canSend,0},
                     {game.paused ? "继续 [空格]" : "暂停 [空格]",144,70,132,28,game.paused,!ended(game),1},
                     {std::to_string(speed) + "倍速 [F]",286,70,112,28,false,!ended(game),2},
                     {"重开 [R]",408,70,112,28,false,true,3},
                     {"关卡 [M]",530,70,112,28,false,true,10},
                     {"镜头 [C]",652,70,112,28,false,true,25}};
    scene.texts.push_back({!wavesRemain ? "所有波次已发出" : game.wavesSent == 0 ? "布防完成后点击发波" :
        "下一波 " + number(game.nextWaveDelayMs() / 1000.0f) + " 秒 · 提前奖励 " + std::to_string(game.earlyWaveBonus()) + " 分",width - 366.0f,42,{0.82f,0.87f,0.92f,1},16});
    const auto roots = game.baseTowers();
    const int columns = std::max(1,static_cast<int>((width - 36) / PaletteCardWidth));
    for (int index = 0; index < static_cast<int>(roots.size()); ++index) {
        const int cost = game.technology.at("towers").at(roots[index]).at("cost").get<int>();
        const std::string family = weaponName(game,roots[index]);
        scene.buttons.push_back({family + "  " + std::to_string(cost) + " 金",18.0f + (index % columns) * PaletteCardWidth,
            bottom + (index / columns) * PaletteRowHeight,PaletteCardWidth - 8,50,desktop.buildType == roots[index],!ended(game),PaletteActionBase + index,towerIcon(family)});
    }
    scene.buttons.push_back({"保存 [F5]",18,height - 44.0f,128,30,false,true,11});
    scene.buttons.push_back({"取消 [Esc]",156,height - 44.0f,132,30,false,!desktop.buildType.empty() || selectedId >= 0,19});
    scene.texts.push_back({"右键拖动 / WASD 移动 · 滚轮缩放",308,height - 36.0f,{0.72f,0.78f,0.85f,1},16});
    const auto selected = std::find_if(game.towers.begin(),game.towers.end(),[selectedId](const Tower& tower) { return tower.id == selectedId; });
    const std::string type = !desktop.buildType.empty() ? desktop.buildType : selected != game.towers.end() ? selected->type : "";
    if (!type.empty()) {
        const auto& definition = game.technology.at("towers").at(type);
        const auto& weapon = game.technology.at("weapons").at(definition.at("weapon").get<std::string>());
        const float x = right + 14;
        scene.texts.push_back({weaponName(game,type) + (desktop.buildType.empty() ? " · 已选中" : " · 建造"),x,128,AccentColor,22});
        scene.texts.push_back({type,x,162,{},16});
        scene.texts.push_back({"射程 " + number(definition.at("range").get<float>()) + "   " +
            (definition.value("antiground",false) ? "地面" : "") + (definition.value("antiair",false) ? " / 空中" : ""),x,190,{},16});
        scene.texts.push_back({"伤害 " + number(weapon.value("damage",0.0f)) + "   间隔 " + number(definition.value("reload",0) / 1000.0f) + "秒",x,215,{},16});
        if (!desktop.buildType.empty()) {
            scene.texts.push_back({"费用 " + std::to_string(definition.at("cost").get<int>()) + " 金币",x,250,{},20});
            scene.texts.push_back({"左键草地放置 · Esc 取消",x,284,{},16});
            if (hover) {
                const std::string reason = game.buildBlockReason(hover->x,hover->y,type);
                const Color tint = reason.empty() ? Color{0.3f,1,0.5f,0.55f} : Color{1,0.28f,0.25f,0.5f};
                scene.selectedCell = Vec2{static_cast<float>(hover->x),static_cast<float>(hover->y)};
                scene.cellColor = tint;
                scene.models.push_back({definition.at("model").get<std::string>(),{hover->x + 1.0f,hover->y + 1.0f,TowerGroundOffset},TowerHeadingOffset,1,tint});
                if (definition.contains("turret")) {
                    scene.models.push_back({definition.at("turret").get<std::string>(),{hover->x + 1.0f,hover->y + 1.0f,TowerGroundOffset},TowerHeadingOffset,1,tint});
                    if (definition.contains("rotationoffset")) {
                        const auto& pivot = definition.at("rotationoffset");
                        scene.models.back().pitchPivot = {pivot[0].get<float>(),pivot[1].get<float>(),pivot[2].get<float>()};
                    }
                }
                scene.models.back().pitch = -definition.value("elevation",0.0f) * DegreesToRadians;
                // Preview geometry is visual only and never consumes combat randomness.
                constexpr int PreviewSegments = 48;
                constexpr float FullCircle = 6.2831853f;
                const float range = definition.at("range").get<float>();
                for (int segment = 0; segment < PreviewSegments; ++segment) {
                    const float a = segment * FullCircle / PreviewSegments,b = (segment + 1) * FullCircle / PreviewSegments;
                    scene.lines.push_back({{hover->x + 1 + std::cos(a) * range,hover->y + 1 + std::sin(a) * range,0.06f},
                        {hover->x + 1 + std::cos(b) * range,hover->y + 1 + std::sin(b) * range,0.06f},tint});
                }
                scene.texts.push_back({actionReason(reason),x,318,reason.empty() ? AccentColor : Color{1,0.62f,0.5f,1},16});
            }
        } else if (selected != game.towers.end()) {
            scene.texts.push_back({selected->selling ? "正在拆除" : game.timeMs <= selected->readyAt ? "正在建造 / 升级" : "可以升级或出售",x,248,{},16});
            if (game.timeMs <= selected->readyAt)
                scene.texts.push_back({"剩余 " + number((selected->readyAt - game.timeMs) / 1000.0f) + " 秒",x,274,AccentColor,16});
            else if (selected->damageBoost > 1) scene.texts.push_back({"伤害增幅 " + number(selected->damageBoost) + " 倍",x,274,AccentColor,16});
            const auto& upgrades = definition.at("upgrades");
            float rowY = 305;
            for (int index = 0; index < static_cast<int>(upgrades.size()); ++index) {
                const std::string name = upgrades[index].get<std::string>();
                const auto& next = game.technology.at("towers").at(name);
                const auto& nextWeapon = game.technology.at("weapons").at(next.at("weapon").get<std::string>());
                scene.buttons.push_back({"升级 " + std::to_string(next.at("cost").get<int>()) + " 金币 [U]",x,rowY,DetailsWidth - 40,32,false,
                    game.upgradeBlockReason(selectedId,name).empty(),UpgradeActionBase + index});
                scene.texts.push_back({name,x,rowY + 35,{},14});
                scene.texts.push_back({"伤害 " + number(weapon.value("damage",0.0f)) + " → " + number(nextWeapon.value("damage",0.0f)) +
                    "  射程 " + number(next.at("range").get<float>()),x,rowY + 56,AccentColor,14});
                rowY += 84;
            }
            if (upgrades.empty()) scene.texts.push_back({"已升至最高级",x,rowY,AccentColor,16});
            rowY = std::max(rowY + (upgrades.empty() ? 28 : 0),350.0f);
            scene.buttons.push_back({"出售 +" + std::to_string(game.sellRefund(selectedId)) + " 金币",x,rowY,DetailsWidth - 40,32,false,game.sellBlockReason(selectedId).empty(),5});
            scene.texts.push_back({"拆除完成后退款 · 扣最多10分",x,rowY + 38,{0.8f,0.82f,0.85f,1},14});
        }
    } else {
        scene.texts.push_back({"布置防御",right + 14,128,AccentColor,22});
        scene.texts.push_back({"先在下方选择防御塔",right + 14,174,{},18});
        scene.texts.push_back({"点击已有塔查看详情",right + 14,204,{},18});
        scene.texts.push_back({"保护羊群，阻止外星人",right + 14,252,{0.8f,0.85f,0.9f,1},16});
    }
    scene.texts.push_back({profile.warning.empty() ? actionReason(game.message) : profile.warning,18,bottom - 34,{1,0.88f,0.64f,1},16});
    if (desktop.restartConfirm || game.paused || (ended(game) && !desktop.resultsDismissed)) {
        // The modal replaces controls so no hidden button can receive its click.
        scene.buttons.clear();
        scene.panels.push_back({0,0,static_cast<float>(width),static_cast<float>(height),{0.01f,0.03f,0.05f,0.58f}});
        const float modalHeight = ended(game) && !desktop.restartConfirm ? 390.0f : 290.0f;
        const float x = (width - 460) / 2.0f,y = (height - modalHeight) / 2.0f;
        scene.panels.push_back({x,y,460,modalHeight,PanelColor});
        // Modal text is appended last, after the normal interface text.
        scene.texts.clear();
        scene.texts.push_back({desktop.restartConfirm ? "重新开始这一关？" : ended(game) ? (game.outcome == Outcome::Won ? "羊群保卫成功！" : "羊群被带走了") : "暂停",x + 26,y + 24,AccentColor,28});
        if (desktop.restartConfirm) {
            scene.texts.push_back({"本局的防御布局和战斗进度将重置。",x + 26,y + 80,{},18});
            scene.buttons.push_back({"确认重开",x + 26,y + 158,190,40,false,true,23});
            scene.buttons.push_back({"取消",x + 244,y + 158,190,40,false,true,24});
        } else if (ended(game)) {
            scene.texts.push_back({"总分 " + std::to_string(game.finalScore()) + "    剩余羊群 " + std::to_string(game.lives),x + 26,y + 82,{},22});
            scene.texts.push_back({"击杀 " + std::to_string(game.killed) + "    完成波次 " + std::to_string(game.wavesSent),x + 26,y + 118,{},18});
            const std::string next = game.outcome == Outcome::Won ? nextCampaign(game.modeId) : "";
            scene.texts.push_back({desktop.newUnlocks.empty() ? (game.outcome == Outcome::Won ? "成绩已记录" : "调整防御布局，再来挑战") : "新解锁：" + desktop.newUnlocks.front(),x + 26,y + 153,AccentColor,18});
            if (desktop.newUnlocks.size() > 1) scene.texts.push_back({desktop.newUnlocks[1],x + 26,y + 180,AccentColor,16});
            if (!next.empty()) scene.buttons.push_back({"下一战役 · " + levelName(next),x + 26,y + 214,408,38,false,modeAvailable(next,desktop,profile),28});
            else if (game.modeId == "Madness" && game.outcome == Outcome::Won) scene.texts.push_back({"战役全部完成 · 可继续无限挑战",x + 26,y + 214,AccentColor,18});
            scene.buttons.push_back({"再试一次",x + 26,y + 266,190,38,false,true,23});
            scene.buttons.push_back({"选择关卡",x + 244,y + 266,190,38,false,true,10});
            scene.buttons.push_back({"查看战场",x + 26,y + 318,408,34,false,true,26});
        } else {
            scene.buttons.push_back({"继续游戏",x + 26,y + 86,408,38,false,true,1});
            scene.buttons.push_back({"重新开始",x + 26,y + 138,190,38,false,true,3});
            scene.buttons.push_back({"关卡 / 设置",x + 244,y + 138,190,38,false,true,10});
            scene.buttons.push_back({"保存",x + 26,y + 190,190,38,false,true,11});
            scene.buttons.push_back({desktop.exitSaveFailed ? "仍然退出（未保存）" : "退出游戏",x + 244,y + 190,190,38,false,true,18});
        }
        if (!profile.warning.empty()) scene.texts.push_back({profile.warning,x + 26,y + modalHeight - 27,{1,0.62f,0.5f,1},14});
    }
    return scene;
}

// Report a failed exit save before an explicit second close discards unsaved progress.
void requestExit(const Game& game, Desktop& desktop, Profile& profile, int speed) {
    if (desktop.exitSaveFailed) { desktop.quitRequested = true; return; }
    profile.data["session"] = game.saveState(); profile.data["lastMode"] = game.modeId;
    profile.data["settings"]["speed"] = speed;
    if (profile.save()) desktop.quitRequested = true;
    else {
        desktop.exitSaveFailed = true; desktop.menu = true; desktop.restartConfirm = false;
        desktop.menuMessage = "保存失败。再次退出将放弃未保存的进度。";
    }
}

// Dispatch buttons and keys through one production input path, respecting modal layers.
bool desktopInput(const SDL_Event& event, Game& game, Renderer& renderer, Desktop& desktop,
                  Profile& profile, const SceneView& scene, int& selectedId, int& speed,
                  int width, int height, Audio* audio = nullptr) {
    if (selectedId >= 0 && std::none_of(game.towers.begin(),game.towers.end(),[selectedId](const Tower& tower) { return tower.id == selectedId; })) selectedId = -1;
    int action = -1;
    const bool modal = desktop.restartConfirm || game.paused || (ended(game) && !desktop.resultsDismissed);
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT && !desktop.buildType.empty()) {
        desktop.buildType.clear(); return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_ESCAPE) {
            if (desktop.restartConfirm) desktop.restartConfirm = false;
            else if (desktop.menu) desktop.menu = false;
            else if (!desktop.buildType.empty()) desktop.buildType.clear();
            else if (selectedId >= 0) selectedId = -1;
            else if (ended(game)) desktop.resultsDismissed = !desktop.resultsDismissed;
            else game.paused = !game.paused;
            return true;
        }
        if (desktop.restartConfirm) return true;
        if (event.key.key == SDLK_M) action = 10;
        else if (event.key.key == SDLK_F5) action = 11;
        else if (event.key.key == SDLK_F9 && !modal) action = 12;
        else if (!desktop.menu) {
            switch (event.key.key) {
            case SDLK_SPACE: action = 1; break;
            case SDLK_R: action = 3; break;
            case SDLK_N: if (!modal) action = 0; break;
            case SDLK_F: if (!modal) action = 2; break;
            case SDLK_U: if (!modal) action = 4; break;
            case SDLK_DELETE: if (!modal) action = 5; break;
            case SDLK_C: if (!modal) action = 25; break;
            default: break;
            }
        }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        for (const auto& button : scene.buttons) if (contains(button,event.button.x,event.button.y)) {
            // Disabled controls still consume the click so it cannot reach the board.
            if (!button.enabled) {
                if (button.action == 5) game.message = actionReason(game.sellBlockReason(selectedId));
                else if (button.action >= UpgradeActionBase) {
                    const auto tower = std::find_if(game.towers.begin(),game.towers.end(),[selectedId](const Tower& value) { return value.id == selectedId; });
                    if (tower != game.towers.end()) {
                        const auto& upgrades = game.technology.at("towers").at(tower->type).at("upgrades");
                        game.message = actionReason(game.upgradeBlockReason(selectedId,upgrades.at(button.action - UpgradeActionBase).get<std::string>()));
                    }
                }
                return true;
            }
            action = button.action; break;
        }
        if (action < 0) {
            if (!desktop.menu && !modal && onBoard(game,width,height,event.button.x,event.button.y)) {
                const auto point = renderer.pick(event.button.x,event.button.y);
                if (point) {
                    const int x = static_cast<int>(std::floor(point->x)),y = static_cast<int>(std::floor(point->y));
                    if (!desktop.buildType.empty()) {
                        selectedId = -1;
                        if (game.build(x,y,desktop.buildType)) game.message = "防御塔开始建造。";
                        else game.message = actionReason(game.message);
                    } else selectedId = game.towerAt(x,y) ? game.towerAt(x,y)->id : -1;
                }
            }
            return true;
        }
    }
    if (action < 0) return desktop.menu || modal;
    if (action <= 5) {
        if (action == 3) desktop.restartConfirm = true;
        else {
            command(game,action,selectedId,speed);
            if (action == 4 || action == 5) game.message = actionReason(game.message);
        }
    } else if (action == 10) {
        desktop.menu = !desktop.menu; desktop.restartConfirm = false; desktop.buildType.clear();
    } else if (action == 11) {
        profile.data["session"] = game.saveState(); profile.data["lastMode"] = game.modeId;
        profile.data["settings"]["speed"] = speed;
        if (profile.save()) { game.message = "游戏已保存。"; desktop.exitSaveFailed = false; }
        else game.message = profile.warning;
        desktop.menuMessage = game.message;
    } else if (action == 12 && profile.data.contains("session")) {
        try {
            Game restored = game;
            restored.restoreState(profile.data.at("session"));
            if (!renderer.changeMap(executableDirectory() / "assets/data" / (restored.mode.at("map").get<std::string>() + ".map.json"))) {
                desktop.menuMessage = game.message = "地图载入失败：" + renderer.error(); return true;
            }
            game = std::move(restored);
            selectedId = -1; desktop.menu = false; desktop.restartConfirm = false; desktop.resultsDismissed = false;
            speed = profile.data.at("settings").value("speed",1);
            if (speed != 1 && speed != 2 && speed != 4) speed = 1;
            desktop.buildType.clear(); desktop.completedMode.clear(); desktop.newUnlocks.clear(); desktop.menuMessage.clear();
        } catch (const std::exception& error) { desktop.menuMessage = game.message = "无法继续保存的游戏：" + std::string(error.what()); }
    } else if (action == 14) --desktop.page;
    else if (action == 15) ++desktop.page;
    else if (action == 16 || action == 17) {
        const std::string setting = action == 16 ? "volume" : "musicVolume";
        const float before = profile.data["settings"].value(setting,0.0f);
        profile.data["settings"][setting] = before >= 1 ? 0 : before + 0.25f;
        if (audio) audio->setVolumes(profile.data["settings"].value("volume",0.0f),profile.data["settings"].value("musicVolume",0.0f));
        profile.save();
    } else if (action == 18) requestExit(game,desktop,profile,speed);
    else if (action == 19) { desktop.buildType.clear(); selectedId = -1; }
    else if (action >= 20 && action <= 22) { desktop.category = action - 20; desktop.page = 0; }
    else if (action == 23) {
        command(game,3,selectedId,speed); desktop.restartConfirm = false; desktop.resultsDismissed = false; desktop.buildType.clear();
    } else if (action == 24) desktop.restartConfirm = false;
    else if (action == 25) renderer.resetCamera();
    else if (action == 26) desktop.resultsDismissed = true;
    else if (action == 27 || action == 28) {
        try {
            const std::string id = action == 28 ? nextCampaign(game.modeId) : desktop.previewMode;
            if (id.empty() || !modeAvailable(id,desktop,profile) || (action == 28 && game.outcome != Outcome::Won)) return true;
            Game next(executableDirectory() / "assets",id);
            if (!renderer.changeMap(executableDirectory() / "assets/data" / (next.mode.at("map").get<std::string>() + ".map.json"))) {
                desktop.menuMessage = game.message = "地图载入失败：" + renderer.error(); return true;
            }
            game = std::move(next);
            desktop.menu = false; selectedId = -1; speed = 1; desktop.restartConfirm = false; desktop.resultsDismissed = false; desktop.buildType.clear();
            desktop.completedMode.clear(); desktop.newUnlocks.clear(); desktop.menuMessage.clear();
            profile.data["lastMode"] = id; profile.save();
        } catch (const std::exception& error) { desktop.menuMessage = game.message = "无法载入关卡：" + std::string(error.what()); }
    } else if (action >= ModeActionBase && action < EndlessActionBase) {
        selectPreview(desktop,desktop.visibleModes.at(action - ModeActionBase));
    } else if (action >= PaletteActionBase && action < UpgradeActionBase) {
        desktop.buildType = game.baseTowers().at(action - PaletteActionBase); selectedId = -1;
    } else if (action >= UpgradeActionBase) {
        for (const auto& tower : game.towers) if (tower.id == selectedId) {
            const auto upgrades = game.technology.at("towers").at(tower.type).at("upgrades");
            game.upgrade(selectedId,upgrades.at(action - UpgradeActionBase).get<std::string>()); game.message = actionReason(game.message); break;
        }
    }
    return true;
}

// Apply camera events only after the visible UI has consumed its own input.
void desktopEvent(const SDL_Event& event, Game& game, Renderer& renderer, Desktop& desktop,
                  Profile& profile, const SceneView& scene, int& selectedId, int& speed,
                  int width, int height, bool& rightDrag, Audio* audio = nullptr) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { rightDrag = false; return; }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) rightDrag = false;
    if (desktopInput(event,game,renderer,desktop,profile,scene,selectedId,speed,width,height,audio)) {
        rightDrag = false; return;
    }
    if (desktop.menu || game.paused || desktop.restartConfirm || (ended(game) && !desktop.resultsDismissed)) {
        rightDrag = false; return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL && onBoard(game,width,height,event.wheel.mouse_x,event.wheel.mouse_y)) renderer.zoom(event.wheel.y);
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
        rightDrag = onBoard(game,width,height,event.button.x,event.button.y);
    else if (event.type == SDL_EVENT_MOUSE_MOTION && rightDrag) {
        if (onBoard(game,width,height,event.motion.x,event.motion.y)) renderer.pan(-event.motion.xrel * 0.025f,event.motion.yrel * 0.025f);
        else rightDrag = false;
    }
}

// Verify level switching and save/continue through actual desktop SDL events, without audio.
int desktopVerification(Renderer& renderer, SDL_Window* window, Game& game,
                        const std::filesystem::path& assets, const std::filesystem::path& output) {
    Desktop desktop; desktop.catalog = loadCatalog(assets);
    Profile profile(output / "desktop-profile");
    int selected = -1,speed = 1,width = 0,height = 0;
    bool rightDrag = false;
    SDL_GetWindowSize(window,&width,&height);
    nlohmann::json report;
    // Queue ordinary events and dispatch through the production desktop handler.
    const auto dispatch = [&](SDL_Event event) {
        if (!SDL_PushEvent(&event)) throw std::runtime_error("Cannot queue desktop verification event.");
        while (SDL_PollEvent(&event)) {
            const SceneView scene = desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height);
            desktopEvent(event,game,renderer,desktop,profile,scene,selected,speed,width,height,rightDrag);
        }
    };
    // Click by drawn action rectangle so responsive layout is part of verification.
    const auto button = [&](int action) {
        const SceneView scene = desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height);
        const auto found = std::find_if(scene.buttons.begin(),scene.buttons.end(),[&](const HudButton& value) { return value.action == action && value.enabled; });
        if (found == scene.buttons.end()) throw std::runtime_error("Missing enabled desktop action " + std::to_string(action));
        SDL_Event event{}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT; event.button.down = true;
        event.button.x = found->x + found->width / 2; event.button.y = found->y + found->height / 2; dispatch(event);
    };
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height));
    renderer.capture(output / "level-menu.png");
    // Exercise the menu at its minimum size, including the actual preview and start controls.
    SDL_SetWindowSize(window,900,700); SDL_PumpEvents(); renderer.resize(); SDL_GetWindowSize(window,&width,&height);
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height));
    renderer.capture(output / "level-menu-900x700.png");
    SDL_SetWindowSize(window,1280,900); SDL_PumpEvents(); renderer.resize(); SDL_GetWindowSize(window,&width,&height);
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height));
    const auto cameraBefore = renderer.project({7.5f,10.5f,0});
    SDL_Event wheel{}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 3;
    wheel.wheel.mouse_x = width / 2.0f; wheel.wheel.mouse_y = height / 2.0f; dispatch(wheel);
    if (!cameraBefore || renderer.project({7.5f,10.5f,0})->x != cameraBefore->x || renderer.project({7.5f,10.5f,0})->y != cameraBefore->y)
        throw std::runtime_error("Menu wheel changed the hidden camera.");
    report["menuBlocksCamera"] = true;
    button(21); // Open the explicit packaged library and select its first original mode.
    const auto browsingBefore = game.saveState();
    button(ModeActionBase);
    if (!desktop.menu || game.saveState() != browsingBefore || !desktop.preview) throw std::runtime_error("Browsing a preview replaced the current defense.");
    report["previewPreservesCurrentGame"] = true;
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height));
    renderer.capture(output / "level-preview.png");
    button(27);
    if (desktop.menu || game.modeId == "PlainPlane") throw std::runtime_error("Packaged level selection did not reach play.");
    button(0); for (int tick = 0; tick < 21; ++tick) game.tick(); button(1);
    button(11);
    if (savedSummary(profile).find("第1波 · 暂停") == std::string::npos) throw std::runtime_error("The saved-game card omitted its real wave and pause state.");
    const auto before = game.saveState(); game.restart(); button(10); button(12);
    if (game.saveState() != before) throw std::runtime_error("Desktop save/continue altered the stored simulation.");
    report["selectionAndContinue"] = game.modeId;
    // Return to PlainPlane through campaign selection and exercise its ordinary game controls.
    button(10); button(20);
    const SceneView campaign = desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height);
    button(ModeActionBase + 3);
    const SceneView locked = desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height);
    const auto start = std::find_if(locked.buttons.begin(),locked.buttons.end(),[](const HudButton& value) { return value.action == 27; });
    if (start == locked.buttons.end() || start->enabled != modeAvailable("Madness",desktop,profile)) throw std::runtime_error("Preview start ignored its campaign gate.");
    if (!start->enabled) {
        const auto gatedState = game.saveState();
        SDL_Event blocked{}; blocked.type = SDL_EVENT_MOUSE_BUTTON_DOWN; blocked.button.button = SDL_BUTTON_LEFT;
        blocked.button.x = start->x + start->width / 2; blocked.button.y = start->y + start->height / 2; dispatch(blocked);
        if (!desktop.menu || game.saveState() != gatedState) throw std::runtime_error("A locked preview started a campaign through its disabled button.");
    }
    report["previewRespectsCampaignGate"] = true;
    const auto plain = std::find(desktop.visibleModes.begin(),desktop.visibleModes.end(),"PlainPlane");
    button(ModeActionBase + static_cast<int>(plain - desktop.visibleModes.begin()));
    button(27);
    if (game.modeId != "PlainPlane" || desktop.menu) throw std::runtime_error("Campaign selection failed.");
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height)); renderer.capture(output / "desktop-game.png");
    const auto roots = game.baseTowers();
    const int laserAction = PaletteActionBase + static_cast<int>(std::find(roots.begin(),roots.end(),"Kilowatt Laser") - roots.begin());
    button(laserAction);
    // Capture and click the real preview under the same layout at all supported sizes.
    report["layouts"] = nlohmann::json::array();
    for (const Cell size : {Cell{900,700},Cell{1280,900},Cell{1920,1080}}) {
        SDL_SetWindowSize(window,size.x,size.y); SDL_PumpEvents(); renderer.resize(); SDL_GetWindowSize(window,&width,&height);
        const SceneView preview = desktopScene(renderer,game,desktop,profile,selected,Cell{7,10},speed,width,height);
        renderer.draw(preview);
        renderer.capture(output / ("preview-" + std::to_string(width) + "x" + std::to_string(height) + ".png"));
        const auto point = renderer.project({7.5f,10.5f,0});
        if (!point || !onBoard(game,width,height,point->x,point->y)) throw std::runtime_error("Legal first placement is covered by the HUD.");
        button(19); button(laserAction);
        report["layouts"].push_back({{"width",width},{"height",height},{"paletteClickable",true}});
    }
    SDL_SetWindowSize(window,1280,900); SDL_PumpEvents(); renderer.resize(); SDL_GetWindowSize(window,&width,&height);
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height));
    const auto point = renderer.project({7.5f,10.5f,0});
    SDL_Event click{}; click.type = SDL_EVENT_MOUSE_BUTTON_DOWN; click.button.button = SDL_BUTTON_LEFT;
    click.button.x = point->x; click.button.y = point->y; dispatch(click);
    if (game.towers.size() != 1 || game.money != 40) throw std::runtime_error("Palette preview did not place its ordinary tower.");
    SDL_Event cancel{}; cancel.type = SDL_EVENT_MOUSE_BUTTON_DOWN; cancel.button.button = SDL_BUTTON_RIGHT; dispatch(cancel);
    if (!desktop.buildType.empty() || rightDrag) throw std::runtime_error("Right-click cancel incorrectly started a camera drag.");
    dispatch(click); for (int tick = 0; tick < 21; ++tick) game.tick();
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height)); renderer.capture(output / "tower-details.png");
    button(5); for (int tick = 0; tick < 21; ++tick) game.tick();
    if (game.money != 50 || !game.towers.empty()) throw std::runtime_error("Selected tower sale did not refund its actual preparation investment.");
    button(1);
    renderer.draw(desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height)); renderer.capture(output / "pause-menu.png");
    const auto frozen = game.saveState(); wheel.wheel.y = -3; dispatch(wheel); game.tick();
    if (frozen != game.saveState()) throw std::runtime_error("Paused UI changed the simulation.");
    button(1);
    button(3);
    SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.key = SDLK_ESCAPE; dispatch(escape);
    if (desktop.restartConfirm || !game.towers.empty() || game.money != 50) throw std::runtime_error("Restart cancellation changed the match.");
    report["placementCancelSalePause"] = true;
    // A real file in place of a profile directory forces a safe, isolated save failure.
    const auto blockedDirectory = output / "exit-save-blocker";
    { std::ofstream marker(blockedDirectory); marker << "Verification file, not a directory."; }
    Profile failedProfile(blockedDirectory);
    Desktop failedExit; failedExit.catalog = desktop.catalog;
    const auto beforeExit = game.saveState();
    SceneView exitScene = desktopScene(renderer,game,failedExit,failedProfile,-1,std::nullopt,speed,width,height);
    const auto exitButton = std::find_if(exitScene.buttons.begin(),exitScene.buttons.end(),[](const HudButton& value) { return value.action == 18; });
    SDL_Event quit{}; quit.type = SDL_EVENT_MOUSE_BUTTON_DOWN; quit.button.button = SDL_BUTTON_LEFT;
    quit.button.x = exitButton->x + exitButton->width / 2; quit.button.y = exitButton->y + exitButton->height / 2;
    desktopInput(quit,game,renderer,failedExit,failedProfile,exitScene,selected,speed,width,height);
    if (failedExit.quitRequested || !failedExit.exitSaveFailed || failedProfile.warning.empty() || game.saveState() != beforeExit)
        throw std::runtime_error("Failed exit save closed the window or changed the defense.");
    exitScene = desktopScene(renderer,game,failedExit,failedProfile,-1,std::nullopt,speed,width,height);
    renderer.draw(exitScene); renderer.capture(output / "exit-save-error.png");
    desktopInput(quit,game,renderer,failedExit,failedProfile,exitScene,selected,speed,width,height);
    if (!failedExit.quitRequested) throw std::runtime_error("Explicit unsaved exit was not available after the warning.");
    report["exitSaveFailureKeepsGameUsable"] = true;
    // Render all 116 complete original maps using the real loader and OpenGL driver.
    report["maps"] = nlohmann::json::array();
    int failures = 0;
    for (const auto& entry : desktop.catalog) {
        game.loadMode(entry.id);
        const bool loaded = renderer.changeMap(assets / "data" / (entry.map + ".map.json"));
        renderer.draw(makeScene(game,-1,std::nullopt,1,width,height));
        const bool okay = loaded && renderer.error().empty();
        report["maps"].push_back({{"id",entry.id},{"width",game.width},{"height",game.height},{"rendered",okay},{"warning",renderer.error()}});
        if (!okay) ++failures;
    }
    report["mapFailures"] = failures; report["audioInitialized"] = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    report["passed"] = failures == 0 && !report["audioInitialized"].get<bool>();
    std::ofstream(output / "desktop-checks.json") << report.dump(2);
    return report.at("passed").get<bool>() ? 0 : 1;
}

// Replay recorded legal commands through SDL mouse/key events and the real desktop handler.
int inputVerification(Renderer& renderer, SDL_Window* window, Game& game, const std::filesystem::path& output) {
    const auto assets = executableDirectory() / "assets";
    std::ifstream stream(output / "simulation-checks.json");
    if (!stream) throw std::runtime_error("Run --self-test before --input-test in the same output directory.");
    const auto simulation = nlohmann::json::parse(stream);
    const auto commands = simulation.at("defended15fps").at("commands");
    nlohmann::json results;
    int selectedId = -1,speed = 1;
    Desktop desktop; desktop.menu = false; desktop.catalog = loadCatalog(assets);
    Profile profile(output / "input-profile");
    bool rightDrag = false;
    int width = 0,height = 0; SDL_GetWindowSize(window,&width,&height);
    // Poll normal queued SDL events instead of calling simulation build/upgrade directly.
    const auto drain = [&]() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const SceneView scene = desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height);
            desktopEvent(event,game,renderer,desktop,profile,scene,selectedId,speed,width,height,rightDrag);
        }
    };
    // Construct a complete ordinary keydown event for the common input handler.
    const auto key = [&](SDL_Keycode code) {
        SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.key = code; event.key.down = true;
        if (!SDL_PushEvent(&event)) throw std::runtime_error("Cannot queue verification key event.");
        drain();
    };
    // Hit the center of a navigation cell through the renderer's world projection.
    const auto click = [&](float x,float y) {
        const auto pixel = renderer.project({x,y,0});
        if (!pixel) throw std::runtime_error("The replay build location is outside the camera.");
        SDL_Event event{}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true; event.button.x = pixel->x; event.button.y = pixel->y;
        if (!SDL_PushEvent(&event)) throw std::runtime_error("Cannot queue verification mouse event.");
        drain();
    };
    // Use the actual palette and modal button rectangles, with no default auto-building.
    const auto button = [&](int action) {
        const SceneView scene = desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height);
        const auto found = std::find_if(scene.buttons.begin(),scene.buttons.end(),[action](const HudButton& value) { return value.action == action && value.enabled; });
        if (found == scene.buttons.end()) throw std::runtime_error("Missing enabled input action " + std::to_string(action));
        SDL_Event event{}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT;
        event.button.x = found->x + found->width / 2; event.button.y = found->y + found->height / 2;
        SDL_PushEvent(&event); drain();
    };
    click(7.5f,10.5f);
    if (!game.towers.empty() || game.money != 50) throw std::runtime_error("Browsing an empty cell built a tower.");
    results["browseDoesNotBuild"] = true;
    const auto roots = game.baseTowers();
    const int laserAction = PaletteActionBase + static_cast<int>(std::find(roots.begin(),roots.end(),"Kilowatt Laser") - roots.begin());
    std::size_t nextCommand = 0;
    // Feed identical legal commands at their recorded fixed-step time, drawing periodically.
    const auto replayDue = [&]() {
        while (nextCommand < commands.size() && commands[nextCommand].at("timeMs").get<int>() <= game.timeMs) {
            const auto& action = commands[nextCommand++];
            if (action.at("action") == "build") {
                button(laserAction);
                click(action.at("x").get<float>() + 0.5f,action.at("y").get<float>() + 0.5f);
                key(SDLK_ESCAPE);
            }
            else {
                const int id = action.at("towerId").get<int>();
                const auto tower = std::find_if(game.towers.begin(),game.towers.end(),[id](const Tower& value) { return value.id == id; });
                if (tower == game.towers.end()) throw std::runtime_error("Desktop replay failed to select the recorded tower.");
                click(tower->x + 0.5f,tower->y + 0.5f); key(SDLK_U);
            }
        }
    };
    replayDue();
    if (game.towers.size() != 5 || game.money != 0) throw std::runtime_error("Mouse build did not spend exactly the original 50 opening gold.");
    // Pause and both desktop speed transitions use the same keys as interactive play.
    key(SDLK_SPACE); const auto paused = game.snapshot(); game.tick();
    if (paused != game.snapshot()) throw std::runtime_error("Desktop pause failed."); key(SDLK_SPACE);
    key(SDLK_F); if (speed != 2) throw std::runtime_error("Desktop 2x control failed.");
    key(SDLK_F); if (speed != 4) throw std::runtime_error("Desktop 4x control failed."); key(SDLK_F);
    for (int tick = 0; tick < 21; ++tick) game.tick(); key(SDLK_N);
    bool capturedAttack = false,capturedDeath = false;
    int firstDeathAt = -1, deathSample = 0;
    // Capture both recovered death phases during the unchanged natural input replay.
    constexpr std::array<int,3> DeathSamplesMs{300,900,1800};
    while (game.outcome == Outcome::Running && game.timeMs < 3600000) {
        game.tick(); replayDue();
        if ((!capturedAttack && !game.beams.empty()) || (!capturedDeath && std::any_of(game.effects.begin(),game.effects.end(),[](const Effect& effect) { return effect.kind == "death"; }))) {
            const auto scene = desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height);
            renderer.draw(scene);
            if (!capturedAttack && !game.beams.empty()) {
                // The original potatogun_i mesh tip, after its packaged scale-two geoset.
                constexpr std::array<float,4> LaserTip{0,-1.15f,0.258f,1};
                const auto& beam = game.beams.front();
                float gap = 100;
                for (const auto& object : scene.models) if (object.model == "potatogun_i") {
                    const auto tip = transform(modelMatrix(object.position,object.rotation,object.scale,object.pitch,object.pitchPivot),LaserTip);
                    gap = std::min(gap,std::sqrt((tip[0] - beam.fromX) * (tip[0] - beam.fromX)
                        + (tip[1] - beam.fromY) * (tip[1] - beam.fromY) + (tip[2] - beam.fromZ) * (tip[2] - beam.fromZ)));
                }
                if (gap > 0.01f) throw std::runtime_error("The original laser mesh tip and actual shot muzzle disagree.");
                results["firstLaserMuzzleGap"] = gap;
                renderer.capture(output / "combat-first-hit.png"); capturedAttack = true;
            }
            if (!capturedDeath && std::any_of(game.effects.begin(),game.effects.end(),[](const Effect& effect) { return effect.kind == "death"; })) {
                renderer.capture(output / "combat-first-kill.png"); capturedDeath = true; firstDeathAt = game.timeMs;
            }
        }
        if (firstDeathAt >= 0 && deathSample < static_cast<int>(DeathSamplesMs.size()) && game.timeMs - firstDeathAt >= DeathSamplesMs[deathSample]) {
            renderer.draw(desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height));
            renderer.capture(output / ("combat-death-" + std::to_string(DeathSamplesMs[deathSample++]) + "ms.png"));
        }
        if (game.timeMs % 1000 == 0) renderer.draw(desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height));
    }
    results["combatScreenshots"] = capturedAttack && capturedDeath;
    results["deathPhaseScreenshots"] = deathSample == static_cast<int>(DeathSamplesMs.size());
    results["desktopVictory"] = game.snapshot();
    if (game.outcome != Outcome::Won || game.wavesSent != 26) throw std::runtime_error("The mouse/keyboard defense did not complete the original level.");
    desktopCompletion(game,desktop,profile);
    renderer.draw(desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height));
    if (!renderer.capture(output / "desktop-victory.png")) throw std::runtime_error(renderer.error());
    const auto terminal = game.saveState(); key(SDLK_U); key(SDLK_DELETE);
    if (terminal != game.saveState()) throw std::runtime_error("Terminal keyboard actions changed the defense.");
    results["terminalKeysPreserveDefense"] = true;
    // Use the naturally completed production replay to check real unlock and next-level flow.
    desktopCompletion(game,desktop,profile);
    if (!profile.data.at("completed").contains("PlainPlane") || !modeAvailable("DividedAttention",desktop,profile))
        throw std::runtime_error("Natural victory did not record progress or unlock the next campaign.");
    renderer.draw(desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height));
    renderer.capture(output / "victory-unlock.png");
    button(28);
    if (game.modeId != "DividedAttention" || game.outcome != Outcome::Preparing || !game.towers.empty())
        throw std::runtime_error("The real victory next-campaign button did not start a fresh defense.");
    results["victoryNextCampaign"] = game.modeId;
    game.restoreState(terminal); renderer.changeMap(assets / "data/PlainPlane.map.json");
    desktopCompletion(game,desktop,profile);
    if (!desktop.newUnlocks.empty()) throw std::runtime_error("An already recorded victory reported new unlocks again.");
    results["repeatCompletionHasNoNewUnlocks"] = true;
    key(SDLK_R);
    if (game.outcome != Outcome::Won || !desktop.restartConfirm) throw std::runtime_error("Restart bypassed its confirmation.");
    button(23);
    if (game.outcome != Outcome::Preparing || game.money != 50 || game.lives != 10 || !game.towers.empty()) throw std::runtime_error("Desktop restart failed.");
    key(SDLK_N);
    while (game.outcome == Outcome::Running && game.timeMs < 3600000) {
        game.tick();
        if (game.timeMs % 1000 == 0) renderer.draw(desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height));
    }
    results["desktopDefeat"] = game.snapshot();
    if (game.outcome != Outcome::Lost || game.lives != 0) throw std::runtime_error("The desktop undefended run did not fail naturally.");
    renderer.draw(desktopScene(renderer,game,desktop,profile,-1,std::nullopt,speed,width,height));
    if (!renderer.capture(output / "desktop-defeat.png")) throw std::runtime_error(renderer.error());
    results["passed"] = true; results["rendererWarnings"] = renderer.error();
    std::ofstream(output / "input-checks.json") << results.dump(2);
    return 0;
}

// Measure natural peak-scene drawing capacity and cache memory after a complete match.
int benchmarkVerification(Renderer& renderer, SDL_Window* window, Game& game,
                          const std::filesystem::path& output) {
    std::ifstream stream(output / "simulation-checks.json");
    if (!stream) throw std::runtime_error("Run --self-test before --benchmark-test.");
    const auto commands = nlohmann::json::parse(stream).at("defended15fps").at("commands");
    game.restart();
    std::size_t nextCommand = 0;
    // Replay ordinary recorded construction and upgrades, retaining the busiest natural scene.
    const auto due = [&]() {
        while (nextCommand < commands.size() && commands[nextCommand].at("timeMs").get<int>() <= game.timeMs) {
            const auto& command = commands[nextCommand++];
            const bool okay = command.at("action") == "build" ? game.build(command.at("x").get<int>(),command.at("y").get<int>())
                : game.upgrade(command.at("towerId").get<int>());
            if (!okay) throw std::runtime_error("Performance replay command failed: " + game.message);
        }
    };
    due(); for (int tick = 0; tick < 21; ++tick) game.tick(); game.sendWave();
    Game busiest = game;
    while (game.outcome == Outcome::Running && game.timeMs < 3600000) {
        game.tick(); due();
        if (game.enemies.size() + game.projectiles.size() > busiest.enemies.size() + busiest.projectiles.size()) busiest = game;
    }
    if (game.outcome != Outcome::Won) throw std::runtime_error("Performance replay did not finish its real 26-wave match.");
    int width = 0,height = 0; SDL_GetWindowSize(window,&width,&height);
    // Disable present throttling so this reports the driver's actual drawing capacity.
    SDL_GL_SetSwapInterval(0);
    constexpr int WarmupFrames = 300, MeasuredFrames = 12000, SampleInterval = 3000;
    SceneView scene = makeScene(busiest,-1,std::nullopt,1,width,height);
    for (int frame = 0; frame < WarmupFrames; ++frame) renderer.draw(scene);
    glFinish();
    nlohmann::json report{{"graphics",renderer.graphicsInfo()},{"width",width},{"height",height},
        {"naturalMatch",game.snapshot()},{"peakScene",busiest.snapshot()},{"renderFrames",MeasuredFrames},
        {"scope","A complete natural match plus repeated peak-scene drawing; not a multi-hour interactive soak or Android comparison."}};
    // Sample committed process memory after asset/cache warmup instead of startup allocation.
    const auto memory = []() {
        PROCESS_MEMORY_COUNTERS_EX values{}; values.cb = sizeof(values);
        if (!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&values),sizeof(values))) return std::uint64_t{0};
        return static_cast<std::uint64_t>(values.PrivateUsage);
    };
    report["memory"].push_back({{"frame",0},{"privateBytes",memory()}});
    const Uint64 start = SDL_GetTicksNS();
    double longestFrameMs = 0;
    for (int frame = 1; frame <= MeasuredFrames; ++frame) {
        const Uint64 before = SDL_GetTicksNS();
        // Changing HUD text exercises the existing bounded text cache over a long frame sequence.
        scene.texts.back().text = "Performance frame " + std::to_string(frame);
        renderer.draw(scene); SDL_PumpEvents();
        // Wait for GPU completion so asynchronous submission is not reported as rendering FPS.
        glFinish();
        longestFrameMs = std::max(longestFrameMs,(SDL_GetTicksNS() - before) / 1e6);
        if (frame % SampleInterval == 0) report["memory"].push_back({{"frame",frame},{"privateBytes",memory()}});
    }
    const double seconds = (SDL_GetTicksNS() - start) / 1e9;
    report["elapsedSeconds"] = seconds; report["averageFps"] = MeasuredFrames / seconds;
    report["longestFrameMs"] = longestFrameMs; report["rendererWarnings"] = renderer.error();
    report["meetsAverage60fps"] = MeasuredFrames / seconds >= 60;
    renderer.capture(output / "peak-scene.png");
    std::ofstream(output / "performance-checks.json") << report.dump(2);
    return renderer.error().empty() ? 0 : 1;
}

// Exercise every weapon through original sandbox construction, waves, and scene rendering.
int combatVisualVerification(Renderer& renderer, const std::filesystem::path& assets, const std::filesystem::path& output) {
    Game catalog(assets,"Sandbox:Endless_Hard:PlainPlane");
    auto types = catalog.baseTowers();
    types.insert(types.end(),{"Nuke","H Bomb","Proximity Mine III","Tesla Coil","Railgun VI"});
    Profile profile(output / "visual-profile");
    nlohmann::json report{{"passed",true},{"scope","Isolated original sandbox runs with programmatic legal construction and dispatch. No funds, HP, wave parameters or combat RNG are altered. Advanced types are direct API construction, not UI upgrade walkthroughs. Screenshots verify Windows rendering, not Android equivalence or natural campaign wins."},{"cases",nlohmann::json::array()}};
    for (const auto& type : types) {
        nlohmann::json result{{"type",type},{"passed",false}};
        try {
            Game game(assets,"Sandbox:Endless_Hard:PlainPlane");
            Desktop desktop; desktop.menu = false;
            const auto& definition = game.technology.at("towers").at(type);
            const std::string kind = game.technology.at("weapons").at(definition.at("weapon").get<std::string>()).at("type").get<std::string>();
            result["kind"] = kind;
            if (!game.build(7,10,type)) throw std::runtime_error(game.message);
            const int selected = game.towers.front().id;
            // Ordinary upper-field lasers keep the real infinite stream alive for air/support cases.
            for (const Cell cell : {Cell{7,22},Cell{11,18},Cell{7,16}})
                if (!game.build(cell.x,cell.y,"Terawatt Laser")) throw std::runtime_error(game.message);
            if (kind == "boost" && !game.build(9,10,"Kilowatt Laser")) throw std::runtime_error(game.message);
            for (int tick = 0; tick < 21; ++tick) game.tick();
            if (!game.sendWave()) throw std::runtime_error(game.message);
            bool observed = false;
            // A bounded real wave run may fail honestly if its required effect never appears.
            constexpr int MaximumVisualRunMs = 240000;
            while (game.timeMs < MaximumVisualRunMs && !ended(game)) {
                game.tick();
                observed = std::any_of(game.beams.begin(),game.beams.end(),[&](const Beam& beam) { return beam.sourceType == type; })
                    || std::any_of(game.projectiles.begin(),game.projectiles.end(),[&](const Projectile& projectile) { return projectile.source.id == selected; })
                    || std::any_of(game.effects.begin(),game.effects.end(),[&](const Effect& effect) { return effect.sourceType == type; });
                if (type == "Nuke" || type == "H Bomb" || type == "Proximity Mine III")
                    observed = std::any_of(game.effects.begin(),game.effects.end(),[&](const Effect& effect) { return effect.sourceType == type && effect.kind == "nuke"; });
                if (kind == "boost") observed = std::any_of(game.towers.begin(),game.towers.end(),[](const Tower& tower) { return tower.damageBoost > 1; });
                if (observed) break;
            }
            if (!observed) throw std::runtime_error("No actual attack/support event before the natural run ended.");
            const int sampleDelay = type == "Nuke" || type == "H Bomb" || type == "Proximity Mine III" ? 1000
                : kind == "railgun" ? 250 : kind == "flamethrower" ? 400 : kind == "mortar" || kind == "homing" || kind == "plasma" ? 125 : 50;
            for (int elapsed = 0; elapsed < sampleDelay && !ended(game); elapsed += SimulationStepMs) game.tick();
            if (!renderer.changeMap(assets / "data/PlainPlane.map.json")) throw std::runtime_error(renderer.error());
            renderer.setBoardViewport(0,108,990,paletteTop(game,1280,900) - 116); renderer.resetCamera(); renderer.zoom(2);
            const SceneView scene = desktopScene(renderer,game,desktop,profile,selected,std::nullopt,1,1280,900);
            renderer.draw(scene);
            std::string filename = type;
            std::replace(filename.begin(),filename.end(),' ','-'); filename += ".png";
            if (!renderer.capture(output / filename) || !renderer.error().empty()) throw std::runtime_error(renderer.error());
            result.update({{"passed",true},{"snapshot",game.snapshot()},{"modelCount",scene.models.size()},{"spriteCount",scene.sprites.size()},{"screenshot",filename}});
        } catch (const std::exception& error) { result["failure"] = error.what(); report["passed"] = false; }
        report["cases"].push_back(result);
    }
    std::ofstream(output / "combat-visual-checks.json") << report.dump(2);
    return report.at("passed").get<bool>() ? 0 : 1;
}

// Verify actual GL rendering and picking at two drawable sizes without audio output.
int graphicsSmoke(Renderer& renderer, SDL_Window* window, Game& game, const std::filesystem::path& output) {
    game.build(7,10); game.build(11,14); game.sendWave();
    for (int tick = 0; tick < 450; ++tick) game.tick();
    nlohmann::json checks;
    checks["graphics"] = renderer.graphicsInfo();
    checks["simulation"] = game.snapshot();
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt) SDL_SetWindowSize(window,1000,700);
        SDL_PumpEvents(); renderer.resize();
        int width = 0,height = 0; SDL_GetWindowSize(window,&width,&height);
        const SceneView scene = makeScene(game,-1,Cell{7,10},1,width,height);
        renderer.draw(scene);
        double maxError = 0;
        for (const Vec3 world : {Vec3{8.5f,4.5f,0},Vec3{8.5f,27.5f,0},Vec3{11,16,0}}) {
            const auto pixel = renderer.project(world);
            const auto point = pixel ? renderer.pick(pixel->x,pixel->y) : std::nullopt;
            if (!point) throw std::runtime_error("Map picking failed during graphics smoke check.");
            maxError = std::max(maxError,static_cast<double>(std::hypot(point->x - world.x,point->y - world.y)));
        }
        checks["viewport"].push_back({{"width",width},{"height",height},{"maxPickError",maxError}});
        if (maxError > 0.001) throw std::runtime_error("Resize/picking round-trip exceeded 0.001 world units.");
        if (!renderer.capture(output / ("scene-" + std::to_string(attempt) + ".png"))) throw std::runtime_error("Screenshot capture failed: " + renderer.error());
    }
    checks["rendererWarnings"] = renderer.error();
    std::ofstream(output / "graphics-checks.json") << checks.dump(2);
    return 0;
}

} // namespace
} // namespace tmreborn

// Run the silent interactive game or explicit developer-only verification modes.
int main(int argc, char** argv) {
    using namespace tmreborn;
    SDL_Window* window = nullptr;
    try {
        const auto directory = executableDirectory();
        const auto assets = directory / "assets";
        const bool selfTest = hasArgument(argc,argv,"--self-test"),smoke = hasArgument(argc,argv,"--smoke-test"),inputTest = hasArgument(argc,argv,"--input-test");
        const bool coverage = hasArgument(argc,argv,"--coverage-test"),audioTest = hasArgument(argc,argv,"--audio-test"),resourceTest = hasArgument(argc,argv,"--resource-test"),desktopTest = hasArgument(argc,argv,"--desktop-test");
        const bool benchmark = hasArgument(argc,argv,"--benchmark-test"),visualTest = hasArgument(argc,argv,"--visual-test");
        std::filesystem::path output = directory / "verification";
        for (int index = 1; index + 1 < argc; ++index) if (std::string(argv[index]) == "--output") output = utf8Path(argv[index + 1]);
        if (selfTest || smoke || inputTest || coverage || audioTest || resourceTest || desktopTest || benchmark || visualTest) std::filesystem::create_directories(output);
        if (selfTest) return runSimulationChecks(assets,output);
        if (coverage) return runCoverageChecks(assets,output);
        if (audioTest) { Audio audio(assets); std::ofstream report(output / "audio-checks.txt"); return audio.verifyAll(report) ? 0 : 1; }
        // VIDEO alone deliberately leaves audio hardware and speakers unopened.
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        // Request the actual core profile before SDL chooses the window pixel format.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        window = SDL_CreateWindow("TowerMadness Reborn",1280,900,SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window,900,700);
        int result = 0;
        {
            Renderer renderer;
            if (!renderer.initialize(window,assets,assets / "data/PlainPlane.map.json")) throw std::runtime_error(renderer.error());
            Game game(assets);
            if (smoke) result = graphicsSmoke(renderer,window,game,output);
            else if (inputTest) result = inputVerification(renderer,window,game,output);
            else if (resourceTest) result = renderer.verifyResources(output) ? 0 : 1;
            else if (desktopTest) result = desktopVerification(renderer,window,game,assets,output);
            else if (benchmark) result = benchmarkVerification(renderer,window,game,output);
            else if (visualTest) result = combatVisualVerification(renderer,assets,output);
            else {
                Profile profile(Profile::userDirectory());
                Desktop desktop; desktop.catalog = loadCatalog(assets);
                Audio audio(assets);
                // The default zero gains never initialize the audio subsystem.
                audio.setVolumes(profile.data.at("settings").value("volume",0.0f),profile.data.at("settings").value("musicVolume",0.0f));
                bool running = true,rightDrag = false;
                int selectedId = -1,speed = 1;
                double accumulator = 0;
                Uint64 previous = SDL_GetTicksNS();
                Uint64 lastSaveAt = SDL_GetTicks();
                while (running) {
                    int width = 0,height = 0; SDL_GetWindowSize(window,&width,&height);
                    float mouseX = 0,mouseY = 0; SDL_GetMouseState(&mouseX,&mouseY);
                    renderer.setBoardViewport(0,108,width - DetailsWidth - 12,paletteTop(game,width,height) - 116);
                    auto picked = renderer.pick(mouseX,mouseY);
                    const auto hover = !desktop.menu && !game.paused && !desktop.restartConfirm && !ended(game) && picked && onBoard(game,width,height,mouseX,mouseY) ?
                        std::optional<Cell>{{static_cast<int>(std::floor(picked->x)),static_cast<int>(std::floor(picked->y))}} : std::nullopt;
                    SceneView scene = desktopScene(renderer,game,desktop,profile,selectedId,hover,speed,width,height);
                    SDL_Event event;
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_EVENT_QUIT) requestExit(game,desktop,profile,speed);
                        else if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) renderer.resize();
                        else {
                            const int beforeTime = game.timeMs;
                            desktopEvent(event,game,renderer,desktop,profile,scene,selectedId,speed,width,height,rightDrag,&audio);
                            if (game.timeMs < beforeTime || desktop.menu || game.paused || desktop.restartConfirm) accumulator = 0;
                        }
                        if (desktop.quitRequested) running = false;
                        scene = desktopScene(renderer,game,desktop,profile,selectedId,hover,speed,width,height);
                    }
                    const Uint64 now = SDL_GetTicksNS();
                    const double elapsed = std::min(MaximumFrameSeconds,(now - previous) / 1e9); previous = now;
                    const bool* keys = SDL_GetKeyboardState(nullptr);
                    if (!desktop.menu && !game.paused && !desktop.restartConfirm && (!ended(game) || desktop.resultsDismissed) && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
                        renderer.pan(static_cast<float>((keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * elapsed * 10),
                                     static_cast<float>((keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W]) * elapsed * 10));
                    if (!desktop.menu && !game.paused && !desktop.restartConfirm && !ended(game)) {
                        accumulator += elapsed * speed;
                        while (accumulator >= SimulationStepMs / 1000.0 && !ended(game)) {
                            game.tick(); accumulator -= SimulationStepMs / 1000.0;
                            for (const auto& sound : game.soundEvents) audio.play(sound);
                        }
                    }
                    desktopCompletion(game,desktop,profile);
                    // Keep at most one current session, saving every thirty real seconds and on exit.
                    constexpr Uint64 AutosaveIntervalMs = 30000;
                    if (SDL_GetTicks() - lastSaveAt > AutosaveIntervalMs && !desktop.menu) {
                        profile.data["session"] = game.saveState(); profile.data["settings"]["speed"] = speed;
                        profile.save(); lastSaveAt = SDL_GetTicks();
                    }
                    audio.play(desktop.menu ? "menu" : game.outcome == Outcome::Preparing ? "build" : "game"); audio.update();
                    scene = desktopScene(renderer,game,desktop,profile,selectedId,hover,speed,width,height);
                    if (!renderer.error().empty()) scene.texts.push_back({"Resource warning: " + renderer.error(),18,136,{1,0.5f,0.4f,1}});
                    if (!audio.warning().empty()) scene.texts.push_back({"Audio warning: " + audio.warning(),18,height - 155.0f,{1,0.5f,0.4f,1}});
                    renderer.draw(scene);
                    SDL_Delay(1);
                }
                profile.data["session"] = game.saveState(); profile.data["settings"]["speed"] = speed; profile.save();
            }
        }
        SDL_DestroyWindow(window); SDL_Quit();
        return result;
    } catch (const std::exception& error) {
        // Persist diagnostics even for a windowless developer check; no audio notification.
        try { std::ofstream(executableDirectory() / "last-error.txt") << error.what(); } catch (...) {}
        bool verification = false;
        for (int index = 1; index < argc; ++index) if (std::string(argv[index]).ends_with("-test")) verification = true;
        if (!verification) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"TowerMadness Reborn",error.what(),window);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 1;
    }
}
