// Focused checks exercise the actual simulation through normal player commands.
#include "game.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace tmreborn {
namespace {
// This defense uses the same 50 opening gold and ordinary build/upgrade commands.
constexpr std::array<Cell,5> OpeningDefense{{{7,10},{11,12},{7,16},{11,18},{7,22}}};
// A normal complete match is bounded at one hour of simulation for test diagnosis.
constexpr int MatchLimitMs = 3600000;
// Upgrade decisions run once per simulation second, independent of drawing rate.
constexpr int DecisionIntervalMs = 1000;

// Fail with a concrete violated gameplay invariant instead of altering test state.
void require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}

// Invest earned gold into the available laser upgrade with greatest damage per cost.
void improveDefense(Game& game, nlohmann::json& commands) {
    int chosen = -1;
    float best = -1;
    for (const auto& tower : game.towers) {
        if (tower.selling || game.timeMs <= tower.readyAt) continue;
        const auto& current = game.technology.at("towers").at(tower.type);
        if (current.at("upgrades").empty()) continue;
        const std::string nextName = current.at("upgrades")[0].get<std::string>();
        const auto& next = game.technology.at("towers").at(nextName);
        const int cost = next.at("cost").get<int>();
        if (cost > game.money) continue;
        const auto& weapons = game.technology.at("weapons");
        const float before = weapons.at(current.at("weapon").get<std::string>()).at("damage").get<float>() / current.at("reload").get<float>();
        const float after = weapons.at(next.at("weapon").get<std::string>()).at("damage").get<float>() / next.at("reload").get<float>();
        const float benefit = (after - before) / cost;
        if (benefit > best) { best = benefit; chosen = tower.id; }
    }
    if (chosen >= 0 && game.upgrade(chosen)) commands.push_back({{"timeMs",game.timeMs},{"action","upgrade"},{"towerId",chosen}});
}

// Play a real complete match with no edited health, rewards, waves, or outcome flags.
nlohmann::json playMatch(const std::filesystem::path& assets, bool defended, int framesPerSecond) {
    Game game(assets);
    nlohmann::json commands = nlohmann::json::array();
    if (defended) for (const Cell cell : OpeningDefense) {
        require(game.build(cell.x,cell.y),"Opening defense placement failed: " + game.message);
        commands.push_back({{"timeMs",0},{"action","build"},{"x",cell.x},{"y",cell.y}});
    }
    // Let preparation construction finish, using normal fixed ticks.
    for (int tick = 0; tick < 21; ++tick) game.tick();
    require(game.sendWave(),"The first wave did not dispatch.");
    int nextDecision = DecisionIntervalMs;
    // Integer microseconds make the 15/144 FPS verification independent of rounding drift.
    std::int64_t accumulator = 0, frame = 0;
    const std::int64_t frameMicroseconds = 1000000 / framesPerSecond;
    while (game.outcome == Outcome::Running && game.timeMs < MatchLimitMs) {
        accumulator += frameMicroseconds; ++frame;
        while (accumulator >= SimulationStepMs * 1000 && game.outcome == Outcome::Running) {
            game.tick(); accumulator -= SimulationStepMs * 1000;
            if (defended && game.timeMs >= nextDecision) {
                improveDefense(game,commands); nextDecision += DecisionIntervalMs;
            }
        }
    }
    auto result = game.snapshot(); result["commands"] = commands; result["renderFrames"] = frame;
    result["laserTypes"] = nlohmann::json::array();
    for (const auto& tower : game.towers) result["laserTypes"].push_back(tower.type);
    return result;
}

} // namespace

// Check funds, blockage, pause, refund, complete loss/win, and 15/144 FPS equivalence.
int runSimulationChecks(const std::filesystem::path& assets, const std::filesystem::path& output) {
    nlohmann::json report;
    try {
        Game sample(assets);
        require(sample.width == 22 && sample.height == 32,"Navigation dimensions do not match the original 2x terrain mapping.");
        require(sample.money == 50 && sample.lives == 10 && sample.waves.size() == 26,"Opening values or the complete wave list changed.");
        require(!sample.build(0,0) && sample.money == 50,"An illegal terrain placement charged gold.");
        require(sample.build(7,10),"Legal laser placement failed.");
        require(sample.money == 40 && !sample.build(7,10) && sample.money == 40,"Occupied footprint or money accounting failed.");
        for (int tick = 0; tick < 21; ++tick) sample.tick();
        sample.paused = true; const auto paused = sample.snapshot();
        for (int tick = 0; tick < 100; ++tick) sample.tick();
        require(paused == sample.snapshot(),"Pause changed the simulation state."); sample.paused = false;
        require(sample.sell(sample.towers.front().id),"Preparation sale failed.");
        for (int tick = 0; tick < 21; ++tick) sample.tick();
        require(sample.money == 50 && sample.towers.empty() && sample.canBuild(7,10),"Sale failed to restore funds or route footprint.");
        // Four towers leave the entry open; the final footprint seals its two-cell mouth.
        Game blocked(assets);
        for (int x = 2; x < 10; x += 2) require(blocked.build(x,8),"Legal partial-wall placement was refused.");
        require(!blocked.build(10,8) && blocked.money == 10 && blocked.towers.size() == 4,"A complete wall was accepted or refused placement charged gold.");
        report["coreInvariants"] = "passed";
        report["undefended"] = playMatch(assets,false,60);
        require(report["undefended"]["outcome"] == static_cast<int>(Outcome::Lost) && report["undefended"]["leaked"] == 10,"An undefended game did not lose naturally after ten sheep leaks.");
        report["defended15fps"] = playMatch(assets,true,15);
        report["defended144fps"] = playMatch(assets,true,144);
        auto slow = report["defended15fps"],fast = report["defended144fps"];
        slow.erase("renderFrames"); fast.erase("renderFrames");
        require(slow == fast,"Render rate changed fixed-step results or normal player commands.");
        require(slow["outcome"] == static_cast<int>(Outcome::Won) && slow["wavesSent"] == 26,"The ordinary defense did not clear all 26 original waves.");
        report["passed"] = true;
    } catch (const std::exception& error) { report["passed"] = false; report["failure"] = error.what(); }
    std::ofstream(output / "simulation-checks.json") << report.dump(2);
    return report.value("passed",false) ? 0 : 1;
}

} // namespace tmreborn
