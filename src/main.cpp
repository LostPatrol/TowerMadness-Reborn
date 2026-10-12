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
#include <stdexcept>
#include <windows.h>
#include <psapi.h>

namespace tmreborn {
namespace {
// HUD geometry is in SDL logical pixels so mouse interaction follows DPI scaling.
constexpr float ButtonWidth = 132, ButtonHeight = 34, ButtonGap = 10;
// At most four simulation seconds are processed per frame after a desktop stall.
constexpr double MaximumFrameSeconds = 1.0;
// Enemy walk frames follow distance; exact Android animation interpolation is pending.
constexpr float WalkFramesPerUnit = 6;
// Imported models face local positive Y, while simulation headings use positive X.
constexpr float ModelHeadingOffset = -1.5707963f;
// Sheep grazing uses an approximate desktop cadence pending original animation timing.
constexpr int SheepFrameMs = 400;
// Selection pages keep large packaged catalogs manageable at ordinary window sizes.
constexpr int ModesPerPage = 12;
// Menu, palette, and upgrade actions use separate ranges from the legacy six controls.
constexpr int ModeActionBase = 1000, EndlessActionBase = 2000, PaletteActionBase = 3000, UpgradeActionBase = 4000;

struct Desktop {
    bool menu = true;
    int page = 0, category = 0;
    std::string buildType = "Kilowatt Laser";
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

// Assemble original mesh instances and a minimal usable desktop command HUD.
SceneView makeScene(const Game& game, int selectedId, std::optional<Cell> hover, int speed, int windowWidth, int windowHeight) {
    SceneView scene;
    const Tower* selected = nullptr;
    for (const auto& tower : game.towers) {
        const auto& definition = game.technology.at("towers").at(tower.type);
        const bool waiting = game.timeMs <= tower.readyAt;
        scene.models.push_back({definition.at("model").get<std::string>(),{tower.x + 1.0f,tower.y + 1.0f,0},definition.contains("turret") ? 0 : tower.heading + ModelHeadingOffset,1,
                                tower.selling ? Color{1,0.45f,0.4f,0.7f} : waiting ? Color{0.6f,0.85f,1,0.65f} : Color{}});
        if (definition.contains("turret")) scene.models.push_back({definition.at("turret").get<std::string>(),
            {tower.x + 1.0f,tower.y + 1.0f,0},tower.heading + ModelHeadingOffset});
        if (definition.contains("towerbase")) {
            const int base = definition.at("towerbase").get<int>();
            scene.models.push_back({base == 0 ? "turretbase" : "turretbase_" + std::to_string(base),{tower.x + 1.0f,tower.y + 1.0f,0}});
        }
        if (tower.id == selectedId) selected = &tower;
    }
    for (const auto& enemy : game.enemies) {
        const auto& wave = game.waveFor(enemy);
        const auto& frames = wave.at(enemy.generation ? "subwalk" : "walk");
        const int frame = frames.empty() ? 0 : static_cast<int>(enemy.traveled * WalkFramesPerUnit * wave.value("walkanimfactor",1.0f)) % static_cast<int>(frames.size());
        const std::string model = frames.empty() ? enemy.model : frames[frame].get<std::string>();
        scene.models.push_back({model,{enemy.x,enemy.y,wave.value("height",0.0f)},enemy.heading + ModelHeadingOffset,wave.at("size").get<float>()});
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
    for (const auto& beam : game.beams) scene.lines.push_back({{beam.fromX,beam.fromY,1.15f},{beam.toX,beam.toY,beam.toZ},{1,0.25f,0.15f,1}});
    for (const auto& projectile : game.projectiles) {
        if (!projectile.model.empty()) scene.models.push_back({projectile.model,projectile.position,
            std::atan2(projectile.velocity.y,projectile.velocity.x) + ModelHeadingOffset,0.5f});
        scene.lines.push_back({projectile.previous,projectile.position,projectile.kind == "plasma" ? Color{0.2f,0.9f,1,1} : Color{1,0.8f,0.3f,1}});
    }
    for (const auto& effect : game.effects) {
        constexpr int Segments = 32;
        constexpr float Circle = 6.2831853f;
        const float radius = effect.radius > 0 ? effect.radius : 0.4f;
        for (int index = 0; index < Segments; ++index) {
            const float a = index * Circle / Segments,b = (index + 1) * Circle / Segments;
            scene.lines.push_back({effect.position + Vec3{std::cos(a) * radius,std::sin(a) * radius,0},
                effect.position + Vec3{std::cos(b) * radius,std::sin(b) * radius,0},{1,0.65f,0.1f,0.8f}});
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
    case 1: game.paused = !game.paused; break;
    case 2: speed = speed == 1 ? 2 : speed == 2 ? 4 : 1; break;
    case 3: game.restart(); selectedId = -1; speed = 1; break;
    case 4: game.upgrade(selectedId); break;
    case 5: game.sell(selectedId); break;
    default: break;
    }
}

// Share the actual desktop build, button, and keyboard dispatch with input verification.
bool gameInput(const SDL_Event& event, Game& game, Renderer& renderer, const SceneView& scene,
               int& selectedId, int& speed, int windowHeight, const std::string& buildType = "Kilowatt Laser", int boardTop = 145, int boardBottom = 70) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        for (int action = 0; action < static_cast<int>(scene.buttons.size()); ++action)
            if (inside(scene.buttons[action],event.button.x,event.button.y)) {
                if (scene.buttons[action].action < 0) command(game,action,selectedId,speed);
                return true;
            }
        if (event.button.y > boardTop && event.button.y < windowHeight - boardBottom) {
            const auto point = renderer.pick(event.button.x,event.button.y);
            if (point) {
                const int x = static_cast<int>(std::floor(point->x)),y = static_cast<int>(std::floor(point->y));
                if (const Tower* tower = game.towerAt(x,y)) selectedId = tower->id;
                else { selectedId = -1; game.build(x,y,buildType); }
            }
        }
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return false;
    switch (event.key.key) {
    case SDLK_N: command(game,0,selectedId,speed); return true;
    case SDLK_SPACE: command(game,1,selectedId,speed); return true;
    case SDLK_F: command(game,2,selectedId,speed); return true;
    case SDLK_R: command(game,3,selectedId,speed); return true;
    case SDLK_U: command(game,4,selectedId,speed); return true;
    case SDLK_DELETE: command(game,5,selectedId,speed); return true;
    default: return false;
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

// Assemble an ordinary level menu, responsive toolbar, and original build palettes.
SceneView desktopScene(const Game& game, Desktop& desktop, const Profile& profile, int selectedId,
                       std::optional<Cell> hover, int speed, int width, int height) {
    SceneView scene = makeScene(game,selectedId,hover,speed,width,height);
    if (desktop.menu) {
        scene.models.clear(); scene.lines.clear(); scene.selectedCell.reset(); scene.texts.clear(); scene.buttons.clear();
        scene.texts.push_back({"TOWERMADNESS REBORN",24,18});
        scene.texts.push_back({desktop.category == 0 ? "Campaign / original technology progression" : desktop.category == 1 ?
            "Packaged maps / desktop library; original access gates are unverified" : "Endless / complete DividedAttention to unlock",24,48});
        scene.buttons = {{"Campaign",24,80,140,34,desktop.category == 0,true,20},
                         {"Packaged maps",174,80,160,34,desktop.category == 1,true,21},
                         {"Endless",344,80,130,34,desktop.category == 2,true,22},
                         {"Back to game",484,80,150,34,false,true,10},
                         {"Continue saved",644,80,160,34,false,profile.data.contains("session"),12}};
        scene.buttons.push_back({"Sound " + std::to_string(static_cast<int>(profile.data.at("settings").value("volume",0.0f) * 100)) + "%",24,height - 85.0f,160,30,false,true,16});
        scene.buttons.push_back({"Music " + std::to_string(static_cast<int>(profile.data.at("settings").value("musicVolume",0.0f) * 100)) + "%",194,height - 85.0f,160,30,false,true,17});
        desktop.visibleModes.clear();
        if (desktop.category < 2) {
            if (desktop.category == 0) desktop.visibleModes = {"PlainPlane","DividedAttention","MuttonsFolly","Madness"};
            else for (const auto& entry : desktop.catalog) if (!entry.selectableKnown) desktop.visibleModes.push_back(entry.id);
        } else {
            for (const char* set : {"Endless_Easy","Endless_Medium","Endless_Hard","Sandbox"})
                desktop.visibleModes.push_back(std::string(set == std::string("Sandbox") ? "Sandbox:Endless_Hard:" : "Endless:" + std::string(set) + ':') + game.mode.at("map").get<std::string>());
        }
        const int pages = std::max(1,(static_cast<int>(desktop.visibleModes.size()) + ModesPerPage - 1) / ModesPerPage);
        desktop.page = std::clamp(desktop.page,0,pages - 1);
        const float rowHeight = std::min(42.0f,(height - 290.0f) / ModesPerPage);
        for (int row = 0; row < ModesPerPage; ++row) {
            const int index = desktop.page * ModesPerPage + row;
            if (index >= static_cast<int>(desktop.visibleModes.size())) break;
            const std::string& id = desktop.visibleModes[index];
            const auto found = std::find_if(desktop.catalog.begin(),desktop.catalog.end(),[&](const CatalogEntry& entry) { return entry.id == id; });
            const bool allowed = desktop.category == 1 || (desktop.category == 0 && found != desktop.catalog.end() && campaignAvailable(*found,profile))
                || (desktop.category == 2 && profile.data.at("completed").contains("DividedAttention"));
            const std::string label = id + (found == desktop.catalog.end() ? " / Insane" : " / " + found->tech)
                + (profile.data.at("completed").contains(id) ? " / completed" : allowed ? "" : " / locked");
            scene.buttons.push_back({label,24,130 + row * rowHeight,std::min(740.0f,width - 48.0f),rowHeight - 3,false,allowed,ModeActionBase + index});
        }
        scene.buttons.push_back({"Previous",24,height - 130.0f,140,34,false,desktop.page > 0,14});
        scene.buttons.push_back({"Next",174,height - 130.0f,140,34,false,desktop.page + 1 < pages,15});
        scene.texts.push_back({"Page " + std::to_string(desktop.page + 1) + "/" + std::to_string(pages) + "   Endless uses the current map.",340,height - 124.0f});
        scene.texts.push_back({profile.warning.empty() ? game.message : profile.warning,24,height - 48.0f,{1,0.85f,0.5f,1}});
        return scene;
    }
    // Fit six combat commands in one row even on the minimum supported window width.
    const float widthPerButton = std::min(ButtonWidth,(width - 46.0f) / 6 - 6);
    for (int index = 0; index < 6; ++index) {
        scene.buttons[index].x = 18 + index * (widthPerButton + 6); scene.buttons[index].width = widthPerButton;
    }
    scene.texts[2].text = "Build: " + desktop.buildType + " | choose below, then click grass";
    const auto roots = game.baseTowers();
    const int columns = std::max(1,(width - 36) / 155);
    for (int index = 0; index < static_cast<int>(roots.size()); ++index) {
        const int cost = game.technology.at("towers").at(roots[index]).at("cost").get<int>();
        scene.buttons.push_back({roots[index] + " " + std::to_string(cost),18.0f + (index % columns) * 155,
            140.0f + (index / columns) * 32,149,29,desktop.buildType == roots[index],true,PaletteActionBase + index});
    }
    const float controlsY = height - 98.0f;
    scene.buttons.push_back({"Levels [M]",18,controlsY,130,30,false,true,10});
    scene.buttons.push_back({"Save [F5]",158,controlsY,130,30,false,true,11});
    scene.buttons.push_back({"Load [F9]",298,controlsY,130,30,false,profile.data.contains("session"),12});
    for (const auto& tower : game.towers) if (tower.id == selectedId) {
        const auto& upgrades = game.technology.at("towers").at(tower.type).at("upgrades");
        for (int index = 0; index < static_cast<int>(upgrades.size()); ++index) {
            const std::string name = upgrades[index].get<std::string>();
            const auto& definition = game.technology.at("towers").at(name);
            scene.buttons.push_back({name + " " + std::to_string(definition.at("cost").get<int>()),438.0f + index * 185,controlsY,179,30,false,
                !definition.value("disabled",false),UpgradeActionBase + index});
        }
        scene.texts[2].text = tower.type + " | invested " + std::to_string(tower.refund) + " | boost " + std::to_string(tower.damageBoost);
    }
    if (!profile.warning.empty()) scene.texts.push_back({profile.warning,18,height - 154.0f,{1,0.5f,0.4f,1}});
    return scene;
}

// Handle menu and persistence actions through the same buttons used by desktop play.
bool desktopInput(const SDL_Event& event, Game& game, Renderer& renderer, Desktop& desktop,
                  Profile& profile, const SceneView& scene, int& selectedId, int& speed, Audio* audio = nullptr) {
    int action = -1;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
        for (const auto& button : scene.buttons) if (inside(button,event.button.x,event.button.y)) { action = button.action; break; }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_M || (event.key.key == SDLK_ESCAPE && desktop.menu)) action = 10;
        else if (event.key.key == SDLK_F5) action = 11;
        else if (event.key.key == SDLK_F9) action = 12;
    }
    if (action < 0) return desktop.menu;
    if (action == 10) desktop.menu = !desktop.menu;
    else if (action == 11) {
        profile.data["session"] = game.saveState(); profile.data["lastMode"] = game.modeId;
        profile.data["settings"]["speed"] = speed; game.message = profile.save() ? "Game saved." : profile.warning;
    } else if (action == 12 && profile.data.contains("session")) {
        try {
            game.restoreState(profile.data.at("session"));
            if (!renderer.changeMap(utf8Path(SDL_GetBasePath()) / "assets/data" / (game.mode.at("map").get<std::string>() + ".map.json")))
                game.message = renderer.error();
            selectedId = -1; desktop.menu = false;
            speed = profile.data.at("settings").value("speed",1);
            if (speed != 1 && speed != 2 && speed != 4) speed = 1;
            const auto roots = game.baseTowers(); desktop.buildType = roots.empty() ? "" : roots.front();
        } catch (const std::exception& error) { game.message = "Cannot continue saved game: " + std::string(error.what()); }
    } else if (action == 14) --desktop.page;
    else if (action == 15) ++desktop.page;
    else if (action == 16 || action == 17) {
        const std::string setting = action == 16 ? "volume" : "musicVolume";
        const float before = profile.data["settings"].value(setting,0.0f);
        profile.data["settings"][setting] = before >= 1 ? 0 : before + 0.25f;
        if (audio) audio->setVolumes(profile.data["settings"].value("volume",0.0f),profile.data["settings"].value("musicVolume",0.0f));
        profile.save();
    }
    else if (action >= 20 && action <= 22) { desktop.category = action - 20; desktop.page = 0; }
    else if (action >= ModeActionBase && action < EndlessActionBase) {
        try {
            const std::string id = desktop.visibleModes.at(action - ModeActionBase);
            game.loadMode(id);
            if (!renderer.changeMap(utf8Path(SDL_GetBasePath()) / "assets/data" / (game.mode.at("map").get<std::string>() + ".map.json")))
                game.message = renderer.error();
            desktop.menu = false; selectedId = -1; speed = 1;
            const auto roots = game.baseTowers(); desktop.buildType = roots.empty() ? "" : roots.front();
            profile.data["lastMode"] = id; profile.save();
        } catch (const std::exception& error) { game.message = "Cannot load level: " + std::string(error.what()); }
    } else if (action >= PaletteActionBase && action < UpgradeActionBase) desktop.buildType = game.baseTowers().at(action - PaletteActionBase);
    else if (action >= UpgradeActionBase) {
        for (const auto& tower : game.towers) if (tower.id == selectedId) {
            const auto upgrades = game.technology.at("towers").at(tower.type).at("upgrades");
            game.upgrade(selectedId,upgrades.at(action - UpgradeActionBase).get<std::string>()); break;
        }
    }
    return true;
}

// Verify level switching and save/continue through actual desktop SDL events, without audio.
int desktopVerification(Renderer& renderer, SDL_Window* window, Game& game,
                        const std::filesystem::path& assets, const std::filesystem::path& output) {
    Desktop desktop; desktop.catalog = loadCatalog(assets);
    Profile profile(output / "desktop-profile");
    int selected = -1,speed = 1,width = 0,height = 0;
    SDL_GetWindowSize(window,&width,&height);
    nlohmann::json report;
    // Queue ordinary events and dispatch through the production desktop handler.
    const auto dispatch = [&](SDL_Event event) {
        if (!SDL_PushEvent(&event)) throw std::runtime_error("Cannot queue desktop verification event.");
        while (SDL_PollEvent(&event)) {
            const SceneView scene = desktopScene(game,desktop,profile,selected,std::nullopt,speed,width,height);
            if (!desktopInput(event,game,renderer,desktop,profile,scene,selected,speed))
                gameInput(event,game,renderer,scene,selected,speed,height,desktop.buildType);
        }
    };
    // Click by drawn action rectangle so responsive layout is part of verification.
    const auto button = [&](int action) {
        const SceneView scene = desktopScene(game,desktop,profile,selected,std::nullopt,speed,width,height);
        const auto found = std::find_if(scene.buttons.begin(),scene.buttons.end(),[&](const HudButton& value) { return value.action == action && value.enabled; });
        if (found == scene.buttons.end()) throw std::runtime_error("Missing enabled desktop action " + std::to_string(action));
        SDL_Event event{}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT; event.button.down = true;
        event.button.x = found->x + found->width / 2; event.button.y = found->y + found->height / 2; dispatch(event);
    };
    renderer.draw(desktopScene(game,desktop,profile,selected,std::nullopt,speed,width,height));
    renderer.capture(output / "level-menu.png");
    button(21); // Open the explicit packaged library and select its first original mode.
    button(ModeActionBase);
    if (desktop.menu || game.modeId == "PlainPlane") throw std::runtime_error("Packaged level selection did not reach play.");
    button(11);
    const auto before = game.saveState(); game.restart(); button(12);
    if (game.saveState() != before) throw std::runtime_error("Desktop save/continue altered the stored simulation.");
    report["selectionAndContinue"] = game.modeId;
    // Return to PlainPlane through campaign selection and exercise its ordinary game controls.
    button(10); button(20);
    const SceneView campaign = desktopScene(game,desktop,profile,selected,std::nullopt,speed,width,height);
    const auto plain = std::find(desktop.visibleModes.begin(),desktop.visibleModes.end(),"PlainPlane");
    button(ModeActionBase + static_cast<int>(plain - desktop.visibleModes.begin()));
    if (game.modeId != "PlainPlane" || desktop.menu) throw std::runtime_error("Campaign selection failed.");
    renderer.draw(desktopScene(game,desktop,profile,selected,std::nullopt,speed,width,height)); renderer.capture(output / "desktop-game.png");
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
    int width = 0,height = 0; SDL_GetWindowSize(window,&width,&height);
    // Poll normal queued SDL events instead of calling simulation build/upgrade directly.
    const auto drain = [&]() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const SceneView scene = makeScene(game,selectedId,std::nullopt,speed,width,height);
            gameInput(event,game,renderer,scene,selectedId,speed,height);
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
    std::size_t nextCommand = 0;
    // Feed identical legal commands at their recorded fixed-step time, drawing periodically.
    const auto replayDue = [&]() {
        while (nextCommand < commands.size() && commands[nextCommand].at("timeMs").get<int>() <= game.timeMs) {
            const auto& action = commands[nextCommand++];
            if (action.at("action") == "build") click(action.at("x").get<float>() + 0.5f,action.at("y").get<float>() + 0.5f);
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
    while (game.outcome == Outcome::Running && game.timeMs < 3600000) {
        game.tick(); replayDue();
        if (game.timeMs % 1000 == 0) renderer.draw(makeScene(game,selectedId,std::nullopt,speed,width,height));
    }
    results["desktopVictory"] = game.snapshot();
    if (game.outcome != Outcome::Won || game.wavesSent != 26) throw std::runtime_error("The mouse/keyboard defense did not complete the original level.");
    renderer.draw(makeScene(game,selectedId,std::nullopt,speed,width,height));
    if (!renderer.capture(output / "desktop-victory.png")) throw std::runtime_error(renderer.error());
    key(SDLK_R);
    if (game.outcome != Outcome::Preparing || game.money != 50 || game.lives != 10 || !game.towers.empty()) throw std::runtime_error("Desktop restart failed.");
    key(SDLK_N);
    while (game.outcome == Outcome::Running && game.timeMs < 3600000) {
        game.tick();
        if (game.timeMs % 1000 == 0) renderer.draw(makeScene(game,selectedId,std::nullopt,speed,width,height));
    }
    results["desktopDefeat"] = game.snapshot();
    if (game.outcome != Outcome::Lost || game.lives != 0) throw std::runtime_error("The desktop undefended run did not fail naturally.");
    renderer.draw(makeScene(game,-1,std::nullopt,speed,width,height));
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
                    auto picked = renderer.pick(mouseX,mouseY);
                    const int paletteRows = (static_cast<int>(game.baseTowers().size()) + std::max(1,(width - 36) / 155) - 1) / std::max(1,(width - 36) / 155);
                    const int boardTop = 140 + paletteRows * 32;
                    const auto hover = !desktop.menu && picked && mouseY > boardTop && mouseY < height - 100 ? std::optional<Cell>{{static_cast<int>(std::floor(picked->x)),static_cast<int>(std::floor(picked->y))}} : std::nullopt;
                    SceneView scene = desktopScene(game,desktop,profile,selectedId,hover,speed,width,height);
                    SDL_Event event;
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_EVENT_QUIT) running = false;
                        else if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) renderer.resize();
                        else if (event.type == SDL_EVENT_MOUSE_WHEEL) renderer.zoom(event.wheel.y);
                        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) rightDrag = true;
                        else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) rightDrag = false;
                        else if (event.type == SDL_EVENT_MOUSE_MOTION && rightDrag) renderer.pan(-event.motion.xrel * 0.025f,event.motion.yrel * 0.025f);
                        else if (desktopInput(event,game,renderer,desktop,profile,scene,selectedId,speed,&audio)) {
                            accumulator = 0; completionSaved = false;
                        } else if (gameInput(event,game,renderer,scene,selectedId,speed,height,desktop.buildType,boardTop,100)) {
                            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R) accumulator = 0;
                        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                            switch (event.key.key) {
                            case SDLK_ESCAPE: running = false; break;
                            case SDLK_C: renderer.resetCamera(); break;
                            default: break;
                            }
                        }
                        scene = desktopScene(game,desktop,profile,selectedId,hover,speed,width,height);
                    }
                    const Uint64 now = SDL_GetTicksNS();
                    const double elapsed = std::min(MaximumFrameSeconds,(now - previous) / 1e9); previous = now;
                    const bool* keys = SDL_GetKeyboardState(nullptr);
                    renderer.pan(static_cast<float>((keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * elapsed * 10),
                                 static_cast<float>((keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W]) * elapsed * 10));
                    if (!desktop.menu && !game.paused && game.outcome != Outcome::Won && game.outcome != Outcome::Lost) {
                        accumulator += elapsed * speed;
                        while (accumulator >= SimulationStepMs / 1000.0) {
                            game.tick(); accumulator -= SimulationStepMs / 1000.0;
                            for (const auto& sound : game.soundEvents) audio.play(sound);
                        }
                    }
                    if (game.outcome == Outcome::Won && !completionSaved) { profile.complete(game.modeId,game.lives,game.finalScore()); completionSaved = true; }
                    // Keep at most one current session, saving every thirty real seconds and on exit.
                    constexpr Uint64 AutosaveIntervalMs = 30000;
                    if (SDL_GetTicks() - lastSaveAt > AutosaveIntervalMs && !desktop.menu) {
                        profile.data["session"] = game.saveState(); profile.data["settings"]["speed"] = speed;
                        profile.save(); lastSaveAt = SDL_GetTicks();
                    }
                    audio.play(desktop.menu ? "menu" : game.outcome == Outcome::Preparing ? "build" : "game"); audio.update();
                    scene = desktopScene(game,desktop,profile,selectedId,hover,speed,width,height);
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
