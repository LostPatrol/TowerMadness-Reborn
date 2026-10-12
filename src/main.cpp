// Silent native Windows entry point, desktop controls, and original-resource scene.
#include "game.hpp"
#include "renderer.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

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

// Find a command-line switch without treating a following path as another switch.
bool hasArgument(int argc, char** argv, const std::string& wanted) {
    for (int index = 1; index < argc; ++index) if (argv[index] == wanted) return true;
    return false;
}

// Anchor runtime assets to the executable regardless of the launch working directory.
std::filesystem::path executableDirectory() {
    const char* base = SDL_GetBasePath();
    if (!base) throw std::runtime_error(SDL_GetError());
    return std::filesystem::path(base);
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
        scene.models.push_back({definition.at("model").get<std::string>(),{tower.x + 1.0f,tower.y + 1.0f,0},tower.heading + ModelHeadingOffset,1,
                                tower.selling ? Color{1,0.45f,0.4f,0.7f} : waiting ? Color{0.6f,0.85f,1,0.65f} : Color{}});
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
        const auto& cell = enclosure.at((index * 2) % enclosure.size());
        scene.models.push_back({"sheep_anim_graze" + std::to_string((game.timeMs / SheepFrameMs + index) % 4),
            {cell[0].get<float>() + 0.5f,cell[1].get<float>() + 0.5f,0},index * 0.7f,0.6f});
    }
    for (const auto& ufo : game.ufos)
        if (ufo.state != Ufo::State::Idle) scene.models.push_back({"ufo",{ufo.x,ufo.y,ufo.z},0,1});
    for (const auto& beam : game.beams) scene.lines.push_back({{beam.fromX,beam.fromY,1.15f},{beam.toX,beam.toY,beam.toZ},{1,0.25f,0.15f,1}});
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
    scene.texts.push_back({"TOWERMADNESS REBORN  /  PlainPlane  /  " + state,18,14});
    scene.texts.push_back({"Gold: " + std::to_string(game.money) + "    Sheep: " + std::to_string(game.lives) + "    Wave: " + std::to_string(game.wavesSent) + "/" + std::to_string(game.waves.size()) + "    Speed: " + std::to_string(speed) + "x",18,40});
    const float buttonY = 70;
    scene.buttons = {
        {"Send wave [N]",18,buttonY,ButtonWidth,ButtonHeight,false,game.outcome != Outcome::Won && game.outcome != Outcome::Lost && game.wavesSent < static_cast<int>(game.waves.size())},
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
               int& selectedId, int& speed, int windowHeight) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        for (int action = 0; action < static_cast<int>(scene.buttons.size()); ++action)
            if (inside(scene.buttons[action],event.button.x,event.button.y)) { command(game,action,selectedId,speed); return true; }
        if (event.button.y > 145 && event.button.y < windowHeight - 70) {
            const auto point = renderer.pick(event.button.x,event.button.y);
            if (point) {
                const int x = static_cast<int>(std::floor(point->x)),y = static_cast<int>(std::floor(point->y));
                if (const Tower* tower = game.towerAt(x,y)) selectedId = tower->id;
                else { selectedId = -1; game.build(x,y); }
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
        std::filesystem::path output = directory / "verification";
        for (int index = 1; index + 1 < argc; ++index) if (std::string(argv[index]) == "--output") output = std::filesystem::path(argv[index + 1]);
        if (selfTest || smoke || inputTest) std::filesystem::create_directories(output);
        if (selfTest) return runSimulationChecks(assets,output);
        // VIDEO alone deliberately leaves audio hardware and speakers unopened.
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        // Request the actual core profile before SDL chooses the window pixel format.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        window = SDL_CreateWindow("TowerMadness Reborn - PlainPlane",1280,900,SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) throw std::runtime_error(SDL_GetError());
        int result = 0;
        {
            Renderer renderer;
            if (!renderer.initialize(window,assets,assets / "data/PlainPlane.map.json")) throw std::runtime_error(renderer.error());
            Game game(assets);
            if (smoke) result = graphicsSmoke(renderer,window,game,output);
            else if (inputTest) result = inputVerification(renderer,window,game,output);
            else {
                bool running = true,rightDrag = false;
                int selectedId = -1,speed = 1;
                double accumulator = 0;
                Uint64 previous = SDL_GetTicksNS();
                while (running) {
                    int width = 0,height = 0; SDL_GetWindowSize(window,&width,&height);
                    float mouseX = 0,mouseY = 0; SDL_GetMouseState(&mouseX,&mouseY);
                    auto picked = renderer.pick(mouseX,mouseY);
                    const auto hover = picked && mouseY > 145 && mouseY < height - 70 ? std::optional<Cell>{{static_cast<int>(std::floor(picked->x)),static_cast<int>(std::floor(picked->y))}} : std::nullopt;
                    SceneView scene = makeScene(game,selectedId,hover,speed,width,height);
                    SDL_Event event;
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_EVENT_QUIT) running = false;
                        else if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) renderer.resize();
                        else if (event.type == SDL_EVENT_MOUSE_WHEEL) renderer.zoom(event.wheel.y);
                        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) rightDrag = true;
                        else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) rightDrag = false;
                        else if (event.type == SDL_EVENT_MOUSE_MOTION && rightDrag) renderer.pan(-event.motion.xrel * 0.025f,event.motion.yrel * 0.025f);
                        else if (gameInput(event,game,renderer,scene,selectedId,speed,height)) {
                            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R) accumulator = 0;
                        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                            switch (event.key.key) {
                            case SDLK_ESCAPE: running = false; break;
                            case SDLK_C: renderer.resetCamera(); break;
                            default: break;
                            }
                        }
                    }
                    const Uint64 now = SDL_GetTicksNS();
                    const double elapsed = std::min(MaximumFrameSeconds,(now - previous) / 1e9); previous = now;
                    const bool* keys = SDL_GetKeyboardState(nullptr);
                    renderer.pan(static_cast<float>((keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * elapsed * 10),
                                 static_cast<float>((keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W]) * elapsed * 10));
                    if (!game.paused && game.outcome != Outcome::Won && game.outcome != Outcome::Lost) {
                        accumulator += elapsed * speed;
                        while (accumulator >= SimulationStepMs / 1000.0) { game.tick(); accumulator -= SimulationStepMs / 1000.0; }
                    }
                    scene = makeScene(game,selectedId,hover,speed,width,height);
                    if (!renderer.error().empty()) scene.texts.push_back({"Resource warning: " + renderer.error(),18,136,{1,0.5f,0.4f,1}});
                    renderer.draw(scene);
                    SDL_Delay(1);
                }
            }
        }
        SDL_DestroyWindow(window); SDL_Quit();
        return result;
    } catch (const std::exception& error) {
        // Persist diagnostics even for a windowless developer check; no audio notification.
        try { std::ofstream(executableDirectory() / "last-error.txt") << error.what(); } catch (...) {}
        if (!hasArgument(argc,argv,"--self-test") && !hasArgument(argc,argv,"--smoke-test") && !hasArgument(argc,argv,"--input-test")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"TowerMadness Reborn",error.what(),window);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 1;
    }
}
