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
// Match the existing desktop beam event lifetime for presentation-only fading.
constexpr float BeamFadeLifetimeMs = 125;
// Native death animation uses a three-second countdown split at thirty percent.
constexpr float CorpseSeconds = 3, DeathFirstPhase = 0.3f, DeathLastPhase = 0.7f;
// Original ENEMIES_draw hop literal differs slightly from mathematical pi.
constexpr float DeathHopPi = 3.141f;
// Original living slow/stun tint, independent of the desktop status rings.
constexpr Color EnemyStatusTint{0.7647059f,0.8509804f,1,1};

struct Desktop {
    bool menu = true;
    bool restartConfirm = false, quitRequested = false, resultsDismissed = false;
    int page = 0, category = 0;
    std::string buildType;
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
            scene.sprites.push_back({"nukeshockwave.pvr.png",{tower.x + 1.0f,tower.y + 1.0f,0.06f},radius * 2,radius * 2,
                {0.35f,1,0.6f,0.18f + 0.06f * std::sin(game.timeMs * 0.006f)},true,true});
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
            scene.sprites.push_back({"nukeshockwave.pvr.png",{enemy.x,enemy.y,0.055f},size * 1.9f,size * 1.9f,color,true,true});
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
    // Widths and fades are desktop approximations; attachment coordinates are recovered.
    for (const auto& beam : game.beams) if (!ended(game)) {
        const Vec3 from{beam.fromX,beam.fromY,beam.fromZ},to{beam.toX,beam.toY,beam.toZ};
        const float alpha = std::clamp((beam.expiresAt - game.timeMs) / BeamFadeLifetimeMs,0.0f,1.0f);
        if (beam.kind == "lightning") {
            scene.ribbons.push_back({"lightning.pvr.png",from,to,0.45f,{0.6f,0.8f,1,alpha},(game.timeMs / 25) % 4,4});
        } else if (beam.kind == "flamethrower") {
            constexpr int FlamePuffs = 5;
            const Vec3 direction = to - from;
            for (int index = 0; index < FlamePuffs; ++index) {
                const float phase = (index + 0.5f) / FlamePuffs,diameter = 0.3f + phase * 0.9f;
                scene.sprites.push_back({"fireball.pvr.png",from + direction * phase,diameter,diameter,{1,0.85f,0.6f,alpha * (1 - phase)},false,true});
            }
        } else {
            const Color color = beam.kind == "slow" ? Color{0.2f,0.7f,1,alpha} : beam.kind == "railgun" ? Color{0.5f,1,0.7f,alpha} : Color{1,0.18f,0.08f,alpha};
            scene.ribbons.push_back({"beam.pvr.png",from,to,beam.kind == "railgun" ? 0.12f : 0.07f,color});
        }
    }
    for (const auto& projectile : game.projectiles) {
        if (!projectile.model.empty()) {
            scene.models.push_back({projectile.model,projectile.position,std::atan2(projectile.velocity.y,projectile.velocity.x) + TowerHeadingOffset,0.5f});
            scene.models.back().pitch = -std::atan2(projectile.velocity.z,std::hypot(projectile.velocity.x,projectile.velocity.y));
        }
        if (projectile.kind == "plasma") scene.sprites.push_back({"plasmaball.pvr.png",projectile.position,0.5f,0.5f,{},false,true});
        else scene.sprites.push_back({"smokepuff.pvr.png",projectile.previous,0.32f,0.32f,{0.85f,0.85f,0.85f,0.45f}});
    }
    for (const auto& effect : game.effects) if (!ended(game)) {
        const float age = std::clamp(static_cast<float>(game.timeMs - effect.bornAt) / std::max(1,effect.expiresAt - effect.bornAt),0.0f,1.0f);
        const float radius = effect.radius > 0 ? effect.radius : 0.4f,diameter = radius * (0.5f + age * 1.5f);
        if (effect.kind == "death") scene.sprites.push_back({"smokepuff.pvr.png",effect.position + Vec3{0,0,age * 0.7f},diameter,diameter,{0.65f,0.7f,0.6f,1 - age}});
        else if (effect.kind == "sheepLeak") scene.sprites.push_back({"life.pvr.png",effect.position + Vec3{0,0,age * 1.5f},1,1,{1,0.2f,0.2f,1 - age}});
        else if (effect.kind == "plasma") scene.sprites.push_back({"plasmaball.pvr.png",effect.position,diameter,diameter,{0.5f,0.8f,1,1 - age},false,true});
        else if (effect.kind == "bait" || effect.kind == "split")
            scene.sprites.push_back({"nukeshockwave.pvr.png",{effect.position.x,effect.position.y,0.07f},diameter,diameter,{0.65f,0.3f,1,1 - age},true,true});
        else {
            scene.sprites.push_back({"fireball.pvr.png",effect.position,diameter,diameter,{1,0.8f,0.6f,1 - age},false,true});
            scene.sprites.push_back({"smokepuff.pvr.png",effect.position + Vec3{0,0,age},diameter,diameter,{0.85f,0.8f,0.75f,(1 - age) * 0.55f}});
            if (effect.kind == "nuke") scene.sprites.push_back({"nukeshockwave.pvr.png",{effect.position.x,effect.position.y,0.07f},diameter * 2,diameter * 2,{1,0.85f,0.6f,1 - age},true});
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
                         {"返回当前局",width - 262.0f,166,220,36,false,true,10},
                         {"继续保存的游戏",width - 262.0f,214,220,40,false,profile.data.contains("session"),12}};
        scene.buttons.push_back({"音效 " + std::to_string(static_cast<int>(profile.data.at("settings").value("volume",0.0f) * 100)) + "%",width - 262.0f,280,220,36,false,true,16});
        scene.buttons.push_back({"音乐 " + std::to_string(static_cast<int>(profile.data.at("settings").value("musicVolume",0.0f) * 100)) + "%",width - 262.0f,326,220,36,false,true,17});
        scene.buttons.push_back({"退出游戏",width - 262.0f,height - 104.0f,220,36,false,true,18});
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
            const auto found = std::find_if(desktop.catalog.begin(),desktop.catalog.end(),[&](const CatalogEntry& entry) { return entry.id == id; });
            const bool allowed = desktop.category == 1 || (desktop.category == 0 && found != desktop.catalog.end() && campaignAvailable(*found,profile))
                || (desktop.category == 2 && profile.data.at("completed").contains("DividedAttention"));
            const std::string label = levelName(id) + (profile.data.at("completed").contains(id) ? " · 已完成" : allowed ? "" : " · 未解锁");
            scene.buttons.push_back({label,32,220 + row * rowHeight,width - 326.0f,rowHeight - 3,false,allowed,ModeActionBase + index});
        }
        scene.buttons.push_back({"上一页",32,height - 100.0f,120,36,false,desktop.page > 0,14});
        scene.buttons.push_back({"下一页",162,height - 100.0f,120,36,false,desktop.page + 1 < pages,15});
        scene.texts.push_back({std::to_string(desktop.page + 1) + " / " + std::to_string(pages),302,height - 92.0f});
        scene.texts.push_back({profile.warning.empty() ? "选择关卡，布置防御塔，保护羊群。" : profile.warning,32,height - 48.0f,{1,0.85f,0.5f,1}});
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
        const float x = (width - 460) / 2.0f,y = (height - 290) / 2.0f;
        scene.panels.push_back({x,y,460,290,PanelColor});
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
            scene.buttons.push_back({"再试一次",x + 26,y + 166,190,38,false,true,23});
            scene.buttons.push_back({"选择关卡",x + 244,y + 166,190,38,false,true,10});
            scene.buttons.push_back({"查看战场",x + 26,y + 218,408,34,false,true,26});
        } else {
            scene.buttons.push_back({"继续游戏",x + 26,y + 86,408,38,false,true,1});
            scene.buttons.push_back({"重新开始",x + 26,y + 138,190,38,false,true,3});
            scene.buttons.push_back({"关卡 / 设置",x + 244,y + 138,190,38,false,true,10});
            scene.buttons.push_back({"保存",x + 26,y + 190,190,38,false,true,11});
            scene.buttons.push_back({"退出游戏",x + 244,y + 190,190,38,false,true,18});
        }
    }
    return scene;
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
        profile.data["settings"]["speed"] = speed; game.message = profile.save() ? "游戏已保存。" : profile.warning;
    } else if (action == 12 && profile.data.contains("session")) {
        try {
            game.restoreState(profile.data.at("session"));
            if (!renderer.changeMap(utf8Path(SDL_GetBasePath()) / "assets/data" / (game.mode.at("map").get<std::string>() + ".map.json")))
                game.message = renderer.error();
            selectedId = -1; desktop.menu = false; desktop.restartConfirm = false; desktop.resultsDismissed = false;
            speed = profile.data.at("settings").value("speed",1);
            if (speed != 1 && speed != 2 && speed != 4) speed = 1;
            desktop.buildType.clear();
        } catch (const std::exception& error) { game.message = "无法继续保存的游戏：" + std::string(error.what()); }
    } else if (action == 14) --desktop.page;
    else if (action == 15) ++desktop.page;
    else if (action == 16 || action == 17) {
        const std::string setting = action == 16 ? "volume" : "musicVolume";
        const float before = profile.data["settings"].value(setting,0.0f);
        profile.data["settings"][setting] = before >= 1 ? 0 : before + 0.25f;
        if (audio) audio->setVolumes(profile.data["settings"].value("volume",0.0f),profile.data["settings"].value("musicVolume",0.0f));
        profile.save();
    } else if (action == 18) desktop.quitRequested = true;
    else if (action == 19) { desktop.buildType.clear(); selectedId = -1; }
    else if (action >= 20 && action <= 22) { desktop.category = action - 20; desktop.page = 0; }
    else if (action == 23) {
        command(game,3,selectedId,speed); desktop.restartConfirm = false; desktop.resultsDismissed = false; desktop.buildType.clear();
    } else if (action == 24) desktop.restartConfirm = false;
    else if (action == 25) renderer.resetCamera();
    else if (action == 26) desktop.resultsDismissed = true;
    else if (action >= ModeActionBase && action < EndlessActionBase) {
        try {
            const std::string id = desktop.visibleModes.at(action - ModeActionBase);
            game.loadMode(id);
            if (!renderer.changeMap(utf8Path(SDL_GetBasePath()) / "assets/data" / (game.mode.at("map").get<std::string>() + ".map.json")))
                game.message = renderer.error();
            desktop.menu = false; selectedId = -1; speed = 1; desktop.restartConfirm = false; desktop.resultsDismissed = false; desktop.buildType.clear();
            profile.data["lastMode"] = id; profile.save();
        } catch (const std::exception& error) { game.message = "无法载入关卡：" + std::string(error.what()); }
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
    const auto cameraBefore = renderer.project({7.5f,10.5f,0});
    SDL_Event wheel{}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 3;
    wheel.wheel.mouse_x = width / 2.0f; wheel.wheel.mouse_y = height / 2.0f; dispatch(wheel);
    if (!cameraBefore || renderer.project({7.5f,10.5f,0})->x != cameraBefore->x || renderer.project({7.5f,10.5f,0})->y != cameraBefore->y)
        throw std::runtime_error("Menu wheel changed the hidden camera.");
    report["menuBlocksCamera"] = true;
    button(21); // Open the explicit packaged library and select its first original mode.
    button(ModeActionBase);
    if (desktop.menu || game.modeId == "PlainPlane") throw std::runtime_error("Packaged level selection did not reach play.");
    button(11);
    const auto before = game.saveState(); game.restart(); button(10); button(12);
    if (game.saveState() != before) throw std::runtime_error("Desktop save/continue altered the stored simulation.");
    report["selectionAndContinue"] = game.modeId;
    // Return to PlainPlane through campaign selection and exercise its ordinary game controls.
    button(10); button(20);
    const SceneView campaign = desktopScene(renderer,game,desktop,profile,selected,std::nullopt,speed,width,height);
    const auto plain = std::find(desktop.visibleModes.begin(),desktop.visibleModes.end(),"PlainPlane");
    button(ModeActionBase + static_cast<int>(plain - desktop.visibleModes.begin()));
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
    std::ifstream stream(output / "simulation-checks.json");
    if (!stream) throw std::runtime_error("Run --self-test before --input-test in the same output directory.");
    const auto simulation = nlohmann::json::parse(stream);
    const auto commands = simulation.at("defended15fps").at("commands");
    nlohmann::json results;
    int selectedId = -1,speed = 1;
    Desktop desktop; desktop.menu = false;
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
    renderer.draw(desktopScene(renderer,game,desktop,profile,selectedId,std::nullopt,speed,width,height));
    if (!renderer.capture(output / "desktop-victory.png")) throw std::runtime_error(renderer.error());
    const auto terminal = game.saveState(); key(SDLK_U); key(SDLK_DELETE);
    if (terminal != game.saveState()) throw std::runtime_error("Terminal keyboard actions changed the defense.");
    results["terminalKeysPreserveDefense"] = true;
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
        const bool benchmark = hasArgument(argc,argv,"--benchmark-test");
        std::filesystem::path output = directory / "verification";
        for (int index = 1; index + 1 < argc; ++index) if (std::string(argv[index]) == "--output") output = utf8Path(argv[index + 1]);
        if (selfTest || smoke || inputTest || coverage || audioTest || resourceTest || desktopTest || benchmark) std::filesystem::create_directories(output);
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
                bool completionSaved = false;
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
                        if (event.type == SDL_EVENT_QUIT) running = false;
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
                    if (game.outcome != Outcome::Won) completionSaved = false;
                    if (game.outcome == Outcome::Won && !completionSaved) { profile.complete(game.modeId,game.lives,game.finalScore()); completionSaved = true; }
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
