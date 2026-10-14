// Focused public-API checks for packaged modes, distinct combat rules, and persistence.
#include "game.hpp"
#include "catalog.hpp"
#include "profile.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tmreborn {
namespace {
// Fixture funds bypass affordability only; all original costs and definitions remain intact.
constexpr int FixtureMoney = 1000000;
// Durable targets expose several hits without turning a fixture into a match outcome test.
constexpr std::int64_t FixtureHealth = 1000000;
// Freeze fixture targets with public status fields while preserving configured movement speed.
constexpr int FreezeMs = 100000;
// These PlainPlane grass cells leave its original ground route open.
constexpr Cell FixtureTower{7,10};
// Fifteen seconds reaches the original landing/release sequence without editing match state.
constexpr int OpeningSmokeTicks = 600;
// Finite resources and technology variants recovered from the original package.
constexpr int PackagedModeCount = 116, TechnologyCount = 4;
// Float comparisons cover roundoff of float32 operations, never gameplay acceptance thresholds.
constexpr float FloatTolerance = 0.00001f;
// Recovered sheep allocation consumes two integer cells, four offsets, then three appearance draws.
constexpr int SheepAllocationDraws = 9;
// The original capture turns for 600 ms and hides the sheep at 2600 ms.
constexpr int SheepTurnMs = 600, SheepHideMs = 2600;
// Offline fixtures intentionally use a fixed session seed; interactive sessions supply native microseconds.
constexpr std::uint32_t SheepFixtureSeed = 1;

// Fail one focused case with a useful concrete reason.
void require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}

// Read unchanged prepared data for a fixture or provenance record.
nlohmann::json readData(const std::filesystem::path& path) {
    std::ifstream stream(path);
    require(static_cast<bool>(stream),"Cannot read " + path.string());
    return nlohmann::json::parse(stream);
}

// Compare only float quantities whose expected arithmetic is known.
bool near(float value, float expected) {
    return std::abs(value - expected) < FloatTolerance;
}

// Advance the real fixed-step simulation for an exact tick count.
void advance(Game& game, int ticks) {
    for (int tick = 0; tick < ticks; ++tick) game.tick();
}

// Use an original endless mode's Insane tech and verbatim finite waves for isolated rules.
Game fixture(const std::filesystem::path& assets, const std::string& towerType) {
    Game game(assets,"Endless:Endless_Hard:PlainPlane");
    game.waves = readData(assets / "data/PlainPlane.waves.json");
    game.money = FixtureMoney;
    require(game.build(FixtureTower.x,FixtureTower.y,towerType),"Fixture construction refused: " + game.message);
    advance(game,21);
    game.outcome = Outcome::Running;
    return game;
}

// Add a stable public target with an unchanged original wave definition.
Enemy& target(Game& game, float x = 10, float y = 11, int wave = 0, bool frozen = true) {
    Enemy enemy;
    enemy.id = 1000 + static_cast<int>(game.enemies.size());
    enemy.wave = wave; enemy.x = x; enemy.y = y; enemy.previous = {x,y};
    enemy.moveTarget = {static_cast<int>(x),static_cast<int>(y)};
    enemy.health = enemy.maxHealth = FixtureHealth;
    enemy.model = game.waves.at(wave).at("model").get<std::string>();
    enemy.lastRegenAt = game.timeMs;
    if (frozen) { enemy.slowMs = FreezeMs; enemy.slowFactor = 1; }
    game.enemies.push_back(enemy);
    return game.enemies.back();
}

// Align the first shot and prevent later reloads after it has fired.
void singleShot(Game& game) {
    require(!game.towers.empty() && !game.enemies.empty(),"A single-shot fixture is empty.");
    auto& tower = game.towers.front();
    tower.target = game.enemies.front().id;
    tower.heading = std::atan2(game.enemies.front().y - tower.y - 1,game.enemies.front().x - tower.x - 1);
    game.tick();
    if (!game.towers.empty()) game.towers.front().nextShotAt = FreezeMs;
}

// Resolve a verbatim wave with the required property and record its source.
int importWave(Game& game, const std::filesystem::path& assets, const std::string& set,
               const std::function<bool(const nlohmann::json&)>& predicate) {
    const auto source = readData(assets / ("data/" + set + ".waves.json"));
    for (const auto& wave : source) if (predicate(wave)) {
        game.waves.push_back(wave);
        return static_cast<int>(game.waves.size()) - 1;
    }
    throw std::runtime_error("No required original wave in " + set);
}

// Run all cases even when one fails so coverage output never conceals later failures.
void runCase(nlohmann::json& report, const std::string& name, const std::function<void()>& check) {
    try { check(); report["cases"][name] = {{"passed",true}}; }
    catch (const std::exception& error) {
        report["cases"][name] = {{"passed",false},{"failure",error.what()}};
        report["passed"] = false;
    }
}

// Check original mode resources and natural opening release without a synthetic defense.
void checkModes(const std::filesystem::path& assets, nlohmann::json& report) {
    const auto catalog = loadCatalog(assets);
    require(catalog.size() == PackagedModeCount,"Packaged catalog is not 116 entries.");
    std::set<std::string> technologies;
    std::vector<std::string> failures;
    report["modes"] = nlohmann::json::array();
    for (const auto& entry : catalog) {
        nlohmann::json result{{"id",entry.id},{"tech",entry.tech},{"passed",false}};
        technologies.insert(entry.tech);
        try {
            Game game(assets,entry.id);
            result.update({{"width",game.width},{"height",game.height},{"fixedpath",game.map.value("fixedpath",0)},{"waves",game.waves.size()}});
            require(!game.endless && !game.waves.empty(),entry.id + ": finite wave list absent.");
            require(game.width == game.map.at("width").get<int>() * 2 &&
                    game.height == game.map.at("height").get<int>() * 2,entry.id + ": grid scaling changed.");
            require(game.map.at("terrain").size() == static_cast<std::size_t>(game.width * game.height / 4),entry.id + ": terrain dimensions inconsistent.");
            require(!game.map.at("pads").empty() && !game.map.at("entrance").empty(),entry.id + ": pads or gates absent.");
            for (const auto& gate : game.map.at("entrance"))
                require(game.distanceAt(gate[0].get<int>(),gate[1].get<int>()) == 0,entry.id + ": gate distance is not zero.");
            for (const auto& pad : game.map.at("pads")) {
                const int x = pad[0].get<int>(),y = pad[1].get<int>();
                // Fixed-path maps may place their non-walking UFO anchor on blocked grass.
                require(std::isfinite(game.distanceAt(x + 2,y + 2)),entry.id + ": original released spawn has no route.");
            }
            const auto definitions = game.waves;
            const int originalMoney = game.money;
            require(game.sendWave(),entry.id + ": first natural wave refused.");
            advance(game,OpeningSmokeTicks);
            require(game.spawned > 0,entry.id + ": natural opening released no enemy in 15 seconds.");
            require(game.waves == definitions && game.money == originalMoney && game.towers.empty(),entry.id + ": undefended smoke changed definitions or funds.");
            result.update({{"passed",true},{"spawned",game.spawned},{"leaked",game.leaked}});
        } catch (const std::exception& error) {
            result["failure"] = error.what(); failures.push_back(error.what());
        }
        report["modes"].push_back(result);
    }
    require(technologies.size() == TechnologyCount,"Catalog does not cover all four technologies.");
    require(failures.empty(),std::to_string(failures.size()) + " original modes failed; see the complete mode ledger.");
}

// Verify palette visibility and actual disabled/enabled construction with original tech files.
void checkTechnologies(const std::filesystem::path& assets, nlohmann::json& report) {
    for (const char* name : {"Easy","Medium","Hard","Insane"}) {
        Game game(assets);
        game.technology = readData(assets / (std::string("data/") + name + ".tech.json"));
        const auto roots = game.baseTowers();
        int enabled = 0,disabled = 0;
        for (const char* palette : {"basetowersenergy","basetowersballistic","basetowerslightning","basetowersspecial"})
            for (const auto& root : game.technology.at(palette)) {
                const auto type = root.get<std::string>();
                game.restart(); game.money = FixtureMoney;
                const bool unavailable = game.technology.at("towers").at(type).value("disabled",false);
                const bool visible = std::find(roots.begin(),roots.end(),type) != roots.end();
                require(visible != unavailable,std::string(name) + ": palette availability differs for " + type);
                require(game.build(FixtureTower.x,FixtureTower.y,type) != unavailable,std::string(name) + ": construction availability differs for " + type);
                if (unavailable) { ++disabled; require(game.money == FixtureMoney,"Disabled construction charged funds."); }
                else ++enabled;
            }
        report["technology"][name] = {{"enabledRoots",enabled},{"disabledRoots",disabled}};
    }
}
} // namespace

// Exercise real tick/damage paths; numeric fixtures are explicitly separate from natural matches.
int runCoverageChecks(const std::filesystem::path& assets, const std::filesystem::path& output) {
    nlohmann::json report{{"passed",true},{"fixtureNotice","Combat fixtures use the original endless PlainPlane/Insane mode with extra funds, verbatim imported finite waves, public targets/status timers; they are not natural match victories."}};
    std::filesystem::create_directories(output);
    runCase(report,"all116NaturalOpenings",[&] { checkModes(assets,report); });
    runCase(report,"fourTechnologyPalettes",[&] { checkTechnologies(assets,report); });
    runCase(report,"terminalCommandsPreserveDefense",[&] {
        for (const auto outcome : {Outcome::Won,Outcome::Lost}) {
            Game game(assets);
            const auto opening = game.saveState();
            const std::string openingMessage = game.message;
            require(game.buildBlockReason(FixtureTower.x,FixtureTower.y,"Kilowatt Laser").empty(),"Opening build query refused a legal tower.");
            require(game.saveState() == opening && game.message == openingMessage,"Legal build preview changed occupancy, RNG, funds or the status message.");
            require(game.build(FixtureTower.x,FixtureTower.y),"Terminal command fixture construction failed.");
            advance(game,21);
            const int id = game.towers.front().id;
            require(game.upgradeBlockReason(id).empty() && game.sellBlockReason(id).empty(),"Ready affordable tower commands were incorrectly blocked.");
            game.outcome = outcome;
            const auto before = game.saveState();
            require(!game.buildBlockReason(13,10,"Kilowatt Laser").empty() &&
                    !game.upgradeBlockReason(id).empty() && !game.sellBlockReason(id).empty(),"Terminal legality queries allowed a tower command.");
            require(game.saveState() == before,"Read-only terminal legality queries changed the session.");
            require(!game.build(13,10) && !game.upgrade(id) && !game.sell(id) && !game.sendWave(),"Won/lost match accepted a gameplay command.");
            require(game.saveState() == before,"Rejected terminal command changed funds, score, tower timers or occupancy.");
            game.restart();
            require(game.build(FixtureTower.x,FixtureTower.y),"Restart did not restore build eligibility.");
            advance(game,21);
            require(game.upgrade(game.towers.front().id),"Restart did not restore upgrade eligibility.");
            advance(game,21);
            require(game.sell(game.towers.front().id),"Restart did not restore sale eligibility.");
        }
    });
    runCase(report,"fixedPathBuildSellPreservesRoute",[&] {
        Game game(assets,"AshMountain");
        require(game.map.value("fixedpath",0) == 1,"Fixed-path fixture map changed.");
        const auto originalGrid = game.saveState().at("grid");
        const int openingMoney = game.money;
        Cell placement{-1,-1};
        for (int y = 0; y + 1 < game.height && placement.x < 0; ++y)
            for (int x = 0; x + 1 < game.width; ++x) if (game.canBuild(x,y)) { placement = {x,y}; break; }
        require(placement.x >= 0 && game.build(placement.x,placement.y),"Original fixed-path grass construction refused.");
        require(game.money == openingMoney - game.technology.at("towers").at("Kilowatt Laser").at("cost").get<int>() &&
                !game.canBuild(placement.x,placement.y),"Fixed-path construction charge or overlap failed.");
        require(game.saveState().at("grid") == originalGrid,"Fixed-path construction modified the original route grid.");
        advance(game,21); require(game.sell(game.towers.front().id),"Fixed-path preparation sale refused.");
        advance(game,21);
        require(game.towers.empty() && game.money == openingMoney && game.canBuild(placement.x,placement.y) &&
                game.saveState().at("grid") == originalGrid,"Fixed-path sale changed route bits or failed to restore funds/footprint.");
        for (const auto& pad : game.map.at("pads"))
            require(std::isfinite(game.distanceAt(pad[0].get<int>() + 2,pad[1].get<int>() + 2)),"Fixed-path construction/sale broke a released spawn route.");
    });
    runCase(report,"endlessRuntimeDispatchAndRestart",[&] {
        for (const char* set : {"Endless_Easy","Endless_Medium","Endless_Hard"}) {
            const std::string id = std::string("Endless:") + set + ":PlainPlane";
            Game game(assets,id),same(assets,id);
            require(game.endless && game.waves.empty() && game.money == 100 && game.baseTowers().size() == 12,"Original endless opening fields differ.");
            require(game.sendWave() && same.sendWave() && game.waves == same.waves,"Endless runtime dispatch did not generate a deterministic wave.");
            advance(game,OpeningSmokeTicks);
            require(game.spawned > 0 && game.outcome != Outcome::Won,"Endless opening failed or reported finite victory.");
            game.restart(); require(game.waves.empty() && game.wavesSent == 0 && game.money == 100,"Endless restart retained generated waves.");
            require(game.sendWave() && game.waves == same.waves,"Endless restart changed its first deterministic wave.");
        }
        Game sandbox(assets,"Sandbox:Endless_Hard:PlainPlane");
        require(sandbox.money == 10000 && sandbox.endless,"Original sandbox budget was not selected.");
    });
    runCase(report,"sandboxUsesOwnModeSeed",[&] {
        const auto source = readData(assets / "data/Endless_Hard.waves.json");
        const auto sandboxSeed = endlessModeSeedOffset("Endless_Sandbox","PlainPlane");
        const auto hardSeed = endlessModeSeedOffset("Endless_Hard","PlainPlane");
        require(sandboxSeed != hardSeed,"Sandbox and Hard mode text unexpectedly produced the same seed offset.");
        require(makeEndlessMode("Endless_Sandbox","PlainPlane").at("money") == 10000,"Sandbox mode hash input omitted original money.");
        Game sandbox(assets,"Sandbox:Endless_Hard:PlainPlane");
        require(sandbox.sendWave(),"Sandbox first wave refused.");
        require(sandbox.waves.at(0) == generateEndlessWave(source,0,sandboxSeed),"Sandbox first generated wave used the wrong mode seed.");
        Game hard(assets,"Endless:Endless_Hard:PlainPlane");
        require(hard.sendWave() && hard.waves.at(0) == generateEndlessWave(source,0,hardSeed),"Hard first generated wave used the wrong mode seed.");
    });
    runCase(report,"earlyWaveCompletionBonusAndFinalScore",[&] {
        // Extra funds isolate score timing; original finite waves, health and tower stats remain untouched.
        Game game(assets); game.money = FixtureMoney;
        require(game.build(9,8,"Gatling Laser") && game.build(13,8,"Gatling Laser"),"Original laser scoring defense construction failed.");
        advance(game,21);
        const int moneyBeforeDispatch = game.money;
        require(game.sendWave() && game.sendWave(),"Both idle original UFO pads did not accept early waves.");
        require(game.money == moneyBeforeDispatch && game.bonus == 0 && game.score == 0,"Early dispatch immediately paid gold or completion bonus.");
        const int lastManualDispatchAt = game.timeMs;
        while (game.bonus == 0 && game.timeMs - lastManualDispatchAt < 20000) {
            game.tick();
            const bool secondWavePending = std::any_of(game.ufos.begin(),game.ufos.end(),[](const Ufo& ufo) { return ufo.wave == 1 && ufo.remaining > 0; }) ||
                std::any_of(game.enemies.begin(),game.enemies.end(),[](const Enemy& enemy) { return enemy.wave == 1 && enemy.alive; });
            if (secondWavePending) require(game.bonus == 0,"Early bonus was paid before the complete wave was killed.");
        }
        require(game.bonus == 20 && game.wavesSent == 2 && game.leaked == 0,"Second early wave did not pay its original 20 completion bonus before automatic dispatch.");
        require(game.killed == 20 && game.spawned == 20,"Two original ten-enemy waves were not completely killed.");
        require(game.score == 40 && game.money == moneyBeforeDispatch + 40,"Wave kill gold/score changed or completion bonus became spendable gold.");
        require(game.finalScore() == game.score + game.bonus + 40 * game.lives && game.finalScore() == 460,"Final score omitted raw score, completion bonus or surviving sheep.");
    });
    runCase(report,"strictAcquireInclusiveRetainRange",[&] {
        auto game = fixture(assets,"Kilowatt Laser"); target(game,14,11);
        game.tick(); require(game.beams.empty(),"An enemy exactly at range was acquired.");
        singleShot(game); require(!game.beams.empty(),"A retained target exactly at range was discarded.");
    });
    runCase(report,"beamHeatBeforeArmor",[&] {
        auto game = fixture(assets,"Kilowatt Laser");
        const int armor = importWave(game,assets,"Arcade",[](const auto& wave) { return wave.value("armor",0) == 1 && !wave.value("flying",false); });
        auto& enemy = target(game,10,11,armor); enemy.heat = 2;
        const auto& weapon = game.technology.at("weapons").at("Kilowatt Laser");
        singleShot(game);
        const float heatAtHit = 2 * (1 - 0.05f * SimulationStepMs / 1000.0f);
        const int beforeArmor = static_cast<int>(weapon.at("damage").get<float>() * heatAtHit);
        const int expectedDamage = static_cast<int>(beforeArmor * game.technology.at("towers").at("Kilowatt Laser").at("lightdamage").get<float>());
        require(game.enemies.front().health == FixtureHealth - expectedDamage,"Beam damage did not truncate heat before original armor.");
        require(!game.beams.empty() && near(game.enemies.front().heat,2),"Beam visual or heat cap absent.");
        require(game.beams.front().kind == "beam" && game.beams.front().fromZ > 0,"Beam lost its muzzle height or weapon family.");
    });
    runCase(report,"plasmaMovingDelayAndStun",[&] {
        auto game = fixture(assets,"Plasmatron I"); target(game);
        singleShot(game);
        require(game.projectiles.size() == 1 && game.beams.empty() && game.enemies.front().health == FixtureHealth,"Plasma applied instant beam damage.");
        const auto first = game.projectiles.front().position;
        game.tick();
        require(!game.projectiles.empty() && game.projectiles.front().position.x != first.x,"Plasma projectile did not move.");
        for (int tick = 0; tick < 200 && !game.projectiles.empty(); ++tick) game.tick();
        require(game.projectiles.empty() && game.enemies.front().health == FixtureHealth - 5,"Plasma did not deliver one delayed original hit.");
        auto stun = fixture(assets,"Fusion Cannon"); target(stun);
        bool observed = false;
        for (int shot = 0; shot < 24 && !observed; ++shot) {
            stun.towers.front().nextShotAt = 0;
            singleShot(stun);
            for (int tick = 0; tick < 200 && !stun.projectiles.empty(); ++tick) stun.tick();
            observed = stun.enemies.front().stunMs > 0;
        }
        require(observed,"Original Fusion Cannon stun chance never produced a stun in deterministic fixture.");
        const auto& frozen = stun.enemies.front();
        require(frozen.stunFrameTimeMs > 0 && frozen.stunFrameTimeMs <= stun.timeMs,
            "Applied plasma stun did not capture its animation sample time.");
    });
    runCase(report,"railPenetration",[&] {
        auto game = fixture(assets,"Railgun I"); target(game,10,11); target(game,12,11); target(game,12,12);
        singleShot(game);
        require(game.enemies[0].health == FixtureHealth - 90 && game.enemies[1].health == FixtureHealth - 90,"Railgun failed to penetrate aligned targets.");
        require(game.enemies[2].health == FixtureHealth && game.projectiles.empty(),"Railgun hit an off-line target or created a tracker.");
        require(game.beams.size() == 1 && game.beams.front().kind == "railgun" && game.beams.front().fromZ > 0,
            "Railgun lost its muzzle height or weapon family.");
    });
    runCase(report,"homingMultimuzzleDelayedSplash",[&] {
        auto game = fixture(assets,"Guided Missile IV"); target(game,10,11); target(game,10.3f,11);
        singleShot(game);
        require(game.projectiles.size() == 2 && game.enemies[0].health == FixtureHealth,"Two original missile muzzles did not create delayed shots.");
        require(game.projectiles[0].damage == 92 && game.projectiles[1].damage == 92,"Missile damage division/truncation changed.");
        for (int tick = 0; tick < 400 && !game.projectiles.empty(); ++tick) game.tick();
        require(game.projectiles.empty() && game.enemies[0].health < FixtureHealth && game.enemies[1].health < FixtureHealth,"Homing impact did not damage target and splash neighbor.");
    });
    runCase(report,"mortarArcAndPrediction",[&] {
        auto still = fixture(assets,"Mortar I"); target(still);
        singleShot(still);
        require(still.projectiles.size() == 1 && still.enemies.front().health == FixtureHealth,"Mortar damage was instantaneous.");
        const auto launch = still.projectiles.front();
        still.tick();
        require(!still.projectiles.empty() && still.projectiles.front().position.z > launch.position.z,"Mortar did not begin its ballistic arc.");
        for (int tick = 0; tick < 200 && !still.projectiles.empty(); ++tick) still.tick();
        require(still.projectiles.empty() && still.enemies.front().health < FixtureHealth,"Mortar ground impact did not apply splash.");
        auto moving = fixture(assets,"Mortar I"); target(moving,10,11,0,false);
        singleShot(moving);
        require(moving.projectiles.size() == 1,"Moving mortar target produced no shell.");
        const auto& shell = moving.projectiles.front();
        const auto& enemy = moving.enemies.front();
        const Vec3 direction = normalized(Vec3{enemy.x - shell.origin.x,enemy.y - shell.origin.y,0});
        const Vec3 velocity = normalized(Vec3{shell.velocity.x,shell.velocity.y,0});
        require(std::abs(direction.x * velocity.y - direction.y * velocity.x) > FloatTolerance,"Mortar aim did not lead an actually moving target.");
    });
    runCase(report,"flakAirOnly",[&] {
        auto game = fixture(assets,"Flak I"); target(game,10,11,10); target(game,10,11,0);
        singleShot(game);
        require(game.enemies[0].health < FixtureHealth && game.enemies[1].health == FixtureHealth,"Air-only flak eligibility failed in a shared explosion.");
        require(game.projectiles.empty() && !game.effects.empty(),"Flak did not use its immediate randomized explosion.");
        require(game.effects.back().kind == "flak" && game.effects.back().bornAt == game.timeMs
            && game.effects.back().expiresAt > game.effects.back().bornAt,"Flak effect lost its visual lifecycle.");
    });
    runCase(report,"flamePostArmorHeatAndImmunity",[&] {
        auto game = fixture(assets,"Flamethrower I");
        const int armor = importWave(game,assets,"Arcade",[](const auto& wave) { return wave.value("armor",0) == 1 && !wave.value("flying",false); });
        target(game,10,11,armor).heat = 1.7f;
        singleShot(game);
        const float heatAtHit = 1.7f * (1 - 0.05f * SimulationStepMs / 1000.0f);
        require(game.enemies.front().health == FixtureHealth - static_cast<int>(4 * heatAtHit),"Flame did not apply heat after original armor truncation.");
        auto immune = fixture(assets,"Flamethrower I");
        const int flameArmor = importWave(immune,assets,"Boneyard",[](const auto& wave) { return wave.value("armor",0) == 4 && !wave.value("flying",false); });
        target(immune,10,11,flameArmor);
        singleShot(immune);
        require(immune.enemies.front().health == FixtureHealth && near(immune.enemies.front().heat,1),"Flame-immune armor took damage or accumulated heat.");
    });
    runCase(report,"lightningChain",[&] {
        auto game = fixture(assets,"ElectroShock I"); target(game,10,11); target(game,11,11); target(game,15,14);
        singleShot(game);
        require(game.enemies[0].health == FixtureHealth - 75,"First lightning hit damage changed.");
        const auto secondary = FixtureHealth - game.enemies[1].health;
        require(secondary > 0 && secondary < 75 && game.enemies[2].health == FixtureHealth,"Lightning chain attenuation or jump limit failed.");
        require(game.beams.size() == 2 && game.projectiles.empty(),"Lightning did not render distinct immediate chain segments.");
        require(game.beams.front().kind == "lightning" && game.beams.back().kind == "lightning"
            && game.beams.front().fromZ > 0 && game.beams.back().fromZ >= game.waves.at(0).value("height",0.0f) + 0.5f,
            "Lightning lost its original muzzle or sampled chain start height.");
    });
    runCase(report,"mineTimerRemoval",[&] {
        auto game = fixture(assets,"Proximity Mine I"); target(game);
        singleShot(game);
        const int triggered = game.towers.front().triggeredAt;
        require(triggered == game.timeMs && game.enemies.front().health == FixtureHealth,"Mine failed to arm without immediate damage.");
        advance(game,79);
        require(!game.towers.empty() && game.enemies.front().health == FixtureHealth,"Mine detonated before its original 2000ms timer.");
        game.tick();
        require(game.towers.empty() && game.canBuild(FixtureTower.x,FixtureTower.y),"Consumed mine footprint remained occupied.");
        require(game.enemies.front().health == FixtureHealth - 250,"Mine linear falloff at distance two changed.");
    });
    runCase(report,"slowPrecedence",[&] {
        auto game = fixture(assets,"ElectroSlow I");
        auto& enemy = target(game,10,11,0,false); enemy.slowMs = 5000; enemy.slowFactor = 0.75f; enemy.stunMs = FreezeMs;
        singleShot(game);
        require(near(game.enemies.front().slowFactor,0.75f) && game.enemies.front().slowMs == 4975,"Weaker slow replaced stronger reduction or shortened duration.");
        game.enemies.front().slowMs = 500; game.enemies.front().slowFactor = 0.25f;
        game.towers.front().nextShotAt = 0; singleShot(game);
        require(near(game.enemies.front().slowFactor,0.25f) && game.enemies.front().slowMs == 3000,"Equal slow did not replace duration.");
    });
    runCase(report,"boostAdditiveStrictRange",[&] {
        auto game = fixture(assets,"Kilowatt Laser");
        require(game.build(5,10,"ElectroBoost I") && game.build(7,8,"ElectroBoost I") && game.build(10,10,"ElectroBoost I"),"Support fixture construction failed.");
        target(game); advance(game,101);
        require(near(game.towers.front().damageBoost,1.2f),"Boost did not add two contributions or included the source exactly at range three.");
        for (const auto& tower : game.towers) if (tower.type == "ElectroBoost I")
            require(near(tower.damageBoost,1),"Support tower received a prohibited boost.");
    });
    runCase(report,"baitRouteDetonation",[&] {
        auto game = fixture(assets,"Flash Bait I");
        require(game.distanceAt(FixtureTower.x,FixtureTower.y) == 0 &&
                game.distanceAt(8,27) > 0,"Completed bait did not replace the ground route target.");
        auto& enemy = target(game,10,11,0,false); enemy.stunMs = FreezeMs;
        singleShot(game); advance(game,119);
        require(!game.towers.empty(),"Bait detonated before its original 3000ms timer.");
        game.enemies.front().stunMs = 0; game.tick();
        require(game.towers.empty() && game.canBuild(FixtureTower.x,FixtureTower.y) && game.distanceAt(8,27) == 0,"Bait removal did not restore the sheep gate route.");
        require(near(game.enemies.front().slowFactor,0.25f) && game.enemies.front().slowMs == 3000 && game.enemies.front().stunMs == 1000,"Bait detonation status fields differ from original configuration.");
        require(game.enemies.front().stunFrameTimeMs == game.timeMs,"Bait stun did not refresh the frozen animation time.");
    });
    runCase(report,"regularTickStatusesRegenSplitBoss",[&] {
        Game movement(assets); movement.outcome = Outcome::Running;
        auto& stopped = target(movement,10,11,0,false); stopped.stunMs = SimulationStepMs;
        movement.tick();
        require(movement.enemies.front().x == 10 && movement.enemies.front().y == 11 && movement.enemies.front().stunMs == 0,"Final stun tick did not remain stationary.");
        require(movement.enemies.front().visualHeadingDegrees == 0,"Final stunned tick changed visual heading.");
        movement.tick(); require(movement.enemies.front().traveled > 0,"Expired stun continued to prevent movement.");
        require(movement.enemies.front().visualHeadingDegrees != 0,"Expired stun did not resume visual turning.");
        // Fractional periods distinguish both truncations and slot-as-duration from phase offsets.
        movement.waves.at(0)["speed"] = 1.3f; movement.waves.at(0)["walkanimfactor"] = 1.5f;
        movement.waves.at(0)["walk"] = {"a","b","c","d"};
        movement.waves.at(0)["subwalk"] = {"a","b","c"};
        auto& animated = movement.enemies.front(); animated.nativeSlot = 17;
        movement.timeMs = 189; animated.stunMs = 0;
        require(movement.walkFrame(animated) == 1,"Animation duration lost reciprocal, staged truncation, or slot addition.");
        animated.stunMs = 1000; animated.stunFrameTimeMs = 188;
        require(movement.walkFrame(animated) == 0,"Stunned walk frame did not use its captured sample time.");
        movement.timeMs = 900;
        require(movement.walkFrame(animated) == 0,"Stunned walk frame advanced with game time.");
        animated.stunMs = 0; animated.generation = 1;
        require(movement.walkFrame(animated) == 1,"Generation-one walk did not use its own subwalk count.");
        animated.generation = 2;
        require(movement.walkFrame(animated) == 0,"Later generation incorrectly selected subwalk.");
        Game slowdown(assets); slowdown.outcome = Outcome::Running;
        auto& slowed = target(slowdown,10,11,0,false); slowed.slowMs = SimulationStepMs; slowed.slowFactor = 0.5f;
        slowdown.tick(); require(near(slowdown.enemies.front().traveled,0.0125f) && slowdown.enemies.front().slowMs == 0,"Final slow tick did not retain its speed reduction.");
        slowdown.tick(); require(near(slowdown.enemies.front().traveled,0.0375f),"Expired slow did not restore configured speed.");
        Game regen(assets); regen.outcome = Outcome::Running;
        const int regenWave = importWave(regen,assets,"AliensLanding",[](const auto& wave) { return wave.value("regen",0) > 0; });
        auto& recovering = target(regen,10,11,regenWave,false);
        recovering.maxHealth = regen.waves.at(regenWave).at("health").get<std::int64_t>();
        recovering.health = recovering.maxHealth / 2; recovering.stunMs = FreezeMs;
        const auto before = recovering.health;
        advance(regen,40); require(regen.enemies.front().health == before,"Regeneration crossed a non-strict one-second boundary.");
        regen.tick(); require(regen.enemies.front().health == before + regen.waves.at(regenWave).at("regen").get<std::int64_t>(),"Unslowed regeneration amount changed.");
        regen.enemies.front().slowMs = FreezeMs; regen.enemies.front().slowFactor = 0.25f;
        const auto healed = regen.enemies.front().health;
        advance(regen,41);
        const auto expected = static_cast<std::int64_t>(static_cast<float>(healed) + regen.waves.at(regenWave).at("regen").get<float>() * 0.75f);
        require(regen.enemies.front().health == expected,"Slow-scaled integer regeneration changed.");
        auto split = fixture(assets,"Railgun VI");
        auto& parent = target(split,10,11,23); parent.health = parent.maxHealth = split.waves.at(23).at("health").get<std::int64_t>();
        require(split.waves.at(23).value("boss",false) && split.waves.at(23).value("spawn",0) == 1,"Split-boss fixture lost its original properties.");
        parent.stunMs = 1000; parent.stunFrameTimeMs = split.timeMs; const int originalMoney = split.money;
        // Public fixture targets bypass spawning; restore marks their test slot as occupied.
        parent.nativeSlot = 0;
        auto splitState = split.saveState(); splitState["animation"]["nextSlot"] = 1;
        split.restoreState(splitState);
        singleShot(split);
        require(split.killed == 1 && split.spawned == 2 && split.enemies.size() == 2,"Boss death did not enqueue exactly two children.");
        require(std::any_of(split.effects.begin(),split.effects.end(),[&](const Effect& effect) {
            return effect.kind == "death" && effect.bornAt == split.timeMs;
        }) && std::any_of(split.effects.begin(),split.effects.end(),[&](const Effect& effect) {
            return effect.kind == "split" && effect.bornAt == split.timeMs;
        }),"Boss death lost its independent death or split presentation event.");
        require(split.money == originalMoney + split.waves.at(23).at("gold").get<int>(),"Boss parent reward was not paid exactly once.");
        for (const auto& child : split.enemies)
            require(child.generation == 1 && child.health == split.waves.at(23).at("health").get<std::int64_t>() && child.stunMs == 975 && child.slowFactor == 1
                && child.stunFrameTimeMs == 0 && child.visualHeadingDegrees == 0,"Split child health/status or animation initialization changed.");
        require(split.enemies[0].nativeSlot == 1 && split.enemies[1].nativeSlot == 2
            && split.saveState().at("animation").at("slotHolds").size() == 1,
            "Split children did not allocate in order while their parent still occupied slot zero.");
        require(!split.corpses().front().drawable,"Splitting parent incorrectly retained drawable corpse geometry.");
        split.tick();
        require(split.saveState().at("animation").at("slotHolds").empty(),"Split parent slot survived its next enemy update.");
    });
    runCase(report,"profileReadSaveAndDamage",[&] {
        const auto directory = output / "coverage-profile";
        Profile profile(directory); profile.data["settings"]["speed"] = 4;
        profile.complete("PlainPlane",8,100); profile.complete("PlainPlane",6,90);
        Profile reloaded(directory);
        require(reloaded.warning.empty() && reloaded.data["completed"]["PlainPlane"]["sheep"] == 8 && reloaded.data["completed"]["PlainPlane"]["score"] == 100 && reloaded.data["settings"]["speed"] == 4,"Profile roundtrip or best progress failed.");
        std::ofstream(directory / "profile.json",std::ios::trunc) << "{ damaged";
        Profile damaged(directory);
        require(!damaged.warning.empty() && std::filesystem::exists(directory / "profile.damaged.json") && damaged.data["completed"].empty(),"Damaged profile blocked usable defaults or lost recovery copy.");
        require(damaged.save(),"Usable default profile could not replace damaged input.");
    });
    runCase(report,"sessionExactContinuation",[&] {
        // A natural match includes live UFO release and a normally purchased laser.
        Game natural(assets); require(natural.build(7,10) && natural.sendWave(),"Natural continuation setup failed.");
        while (natural.spawned == 0 && natural.timeMs < OpeningSmokeTicks * SimulationStepMs) natural.tick();
        require(natural.spawned == 1 && !natural.enemies.empty() && natural.enemies.front().nativeSlot == 0,
            "First natural enemy did not allocate native animation slot zero.");
        advance(natural,OpeningSmokeTicks - natural.timeMs / SimulationStepMs);
        Game restored(assets); restored.restoreState(natural.saveState());
        require(natural.saveState() == restored.saveState(),"Natural session did not roundtrip exactly.");
        advance(natural,40); advance(restored,40);
        require(natural.saveState() == restored.saveState(),"Natural resumed state diverged after one second.");
        auto legacy = natural.saveState(); legacy.erase("animation");
        for (auto& enemy : legacy["enemies"]) enemy.erase("animation");
        for (auto& enemy : legacy["pendingChildren"]) enemy.erase("animation");
        Game oldSave(assets); oldSave.restoreState(legacy);
        require(oldSave.snapshot() == natural.snapshot(),"Optional animation fields broke legacy core save compatibility.");
        // A separate live missile fixture verifies projectile geometry and RNG state.
        auto missiles = fixture(assets,"Guided Missile IV"); target(missiles); singleShot(missiles);
        advance(missiles,2);
        require(!missiles.projectiles.empty() && missiles.projectiles.front().trailThreshold > 0,
            "A moving missile did not preserve its native trail sampling threshold.");
        auto legacyProjectiles = missiles.saveState();
        for (auto& projectile : legacyProjectiles["projectiles"]) projectile.erase("trail");
        Game legacyMissiles(assets); legacyMissiles.restoreState(legacyProjectiles);
        require(legacyMissiles.snapshot() == missiles.snapshot() && legacyMissiles.saveState().at("rng") == missiles.saveState().at("rng"),
            "Missing optional trail fields changed core state or combat RNG.");
        auto malformedTrail = missiles.saveState(); malformedTrail["projectiles"][0]["trail"] = "invalid";
        Game trailFallback(assets); trailFallback.restoreState(malformedTrail);
        require(trailFallback.snapshot() == missiles.snapshot(),"Damaged cosmetic trail fields rejected a usable session.");
        Game resumed(assets);
        resumed.restoreState(missiles.saveState());
        require(missiles.saveState() == resumed.saveState(),"Live projectile state did not roundtrip exactly.");
        advance(missiles,20); advance(resumed,20);
        require(missiles.saveState() == resumed.saveState(),"Live projectile resumed state diverged after half a second.");
        // A real projectile kill retains a last-generation corpse's visual slot across saves.
        auto corpseState = missiles.saveState();
        require(!corpseState.at("enemies").empty(),"Projectile continuation fixture lost its durable target.");
        corpseState["enemies"][0]["health"] = 1;
        corpseState["enemies"][0]["animation"]["slot"] = 0;
        corpseState["animation"]["nextSlot"] = 1;
        Game corpse(assets); corpse.restoreState(corpseState);
        corpse.towers.front().nextShotAt = 0; singleShot(corpse);
        for (int tick = 0; tick < 200 && corpse.killed == 0; ++tick) corpse.tick();
        require(corpse.killed == 1 && corpse.saveState().at("animation").at("slotHolds").size() == 1,
            "Projectile death released its visual slot immediately.");
        const auto& visualCorpse = corpse.corpses().front();
        require(visualCorpse.drawable && !visualCorpse.enemy.alive && visualCorpse.slot == visualCorpse.enemy.nativeSlot
            && visualCorpse.frame == corpse.walkFrame(visualCorpse.enemy)
            && visualCorpse.deathAngleDegrees >= -45 && visualCorpse.deathAngleDegrees <= 44,
            "Death did not capture the frozen corpse frame, pose, or approximate direction.");
        Game corpseRestored(assets); corpseRestored.restoreState(corpse.saveState());
        require(corpse.saveState() == corpseRestored.saveState(),"Corpse countdown did not roundtrip exactly.");
        require(corpseRestored.corpses().front().drawable,"Saved drawable corpse became an occupancy-only record.");
        auto occupancyState = corpse.saveState();
        for (auto& hold : occupancyState["animation"]["slotHolds"]) hold.erase("corpse");
        Game occupancyOnly(assets); occupancyOnly.restoreState(occupancyState);
        require(occupancyOnly.saveState() == occupancyState && !occupancyOnly.corpses().front().drawable,
            "Older occupancy-only animation extension invented drawable corpse geometry.");
        auto invalidVisual = corpse.saveState();
        invalidVisual["animation"]["slotHolds"][0]["corpse"]["enemy"]["wave"] = -1;
        Game visualFallback(assets); visualFallback.restoreState(invalidVisual);
        require(visualFallback.snapshot() == corpse.snapshot() && visualFallback.corpses().empty()
            && visualFallback.saveState().at("rng") == corpse.saveState().at("rng"),
            "Damaged optional corpse data corrupted the core match or combat RNG.");
        advance(corpse,40); advance(corpseRestored,40);
        require(corpse.saveState() == corpseRestored.saveState(),"Resumed corpse countdown diverged.");
        advance(corpse,100);
        require(corpse.saveState().at("animation").at("slotHolds").empty(),"Expired corpse did not release its visual slot.");
        auto broken = natural.saveState(); broken["version"] = -1;
        const auto intact = restored.saveState(); bool refused = false;
        try { restored.restoreState(broken); } catch (const std::exception&) { refused = true; }
        require(refused && restored.saveState() == intact,"Invalid restore corrupted the current session.");
    });
    runCase(report,"sheepSharedRngCaptureAndPersistence",[&] {
        Game game(assets,"PlainPlane",SheepFixtureSeed);
        require(game.sheep().size() == static_cast<std::size_t>(game.lives) && game.animalClockMs() == 0,
            "Opening sheep allocation or clock changed.");
        std::mt19937 expected(SheepFixtureSeed);
        expected.discard(6);
        const float sizeSample = static_cast<float>(expected()) * 0x1p-32f;
        const float sizeClass = static_cast<float>(expected()) * 0x1p-32f;
        const float colorSample = static_cast<float>(expected()) * 0x1p-32f;
        const float expectedSize = sizeClass < 0.2f ? 0.6f + 0.1f * sizeSample : 0.9f + 0.2f * sizeSample;
        require(near(game.sheep().front().size,expectedSize) && game.sheep().front().color == (colorSample < 0.1f ? 2 : 0),
            "Sheep appearance did not use its original seventh through ninth shared RNG samples.");
        expected.discard((game.sheep().size() - 1) * SheepAllocationDraws);
        // UFO allocations follow the complete flock and consume one initial heading sample each.
        expected.discard(game.ufos.size());
        std::ostringstream engine; engine << expected;
        require(game.saveState().at("rng") == engine.str(),"Sheep/UFO allocation changed the expected shared MT19937 draw count.");
        for (const auto& sheep : game.sheep())
            require(sheep.state == 1 && !sheep.reserved && ((sheep.size >= 0.6f && sheep.size < 0.7f)
                || (sheep.size >= 0.9f && sheep.size < 1.1f)) && (sheep.color == 0 || sheep.color == 2),
                "Opening sheep state, color, or recovered size range changed.");
        game.tick(); require(game.animalClockMs() == SimulationStepMs,"Idle flock did not advance one fixed tick.");
        game.paused = true; const auto paused = game.saveState(); advance(game,4);
        require(game.saveState() == paused,"Paused tick changed sheep or shared RNG."); game.paused = false;
        Game accelerated = game;
        advance(game,80);
        for (int frame = 0; frame < 20; ++frame) advance(accelerated,4);
        require(game.saveState() == accelerated.saveState(),"Four-tick scheduling changed the natural idle flock.");
        require(game.sendWave(),"Natural undefended sheep capture dispatch failed.");
        const int openingLives = game.lives;
        while (game.leaked == 0 && game.timeMs < 60000) game.tick();
        require(game.leaked > 0 && game.lives < openingLives && !game.intruders().empty()
            && game.sheep().front().reserved && game.sheep().front().state != -1,
            "First natural leak did not deduct life immediately while retaining its reserved visible sheep.");
        while (game.sheep().front().state != 3 && game.timeMs < 90000) game.tick();
        require(game.sheep().front().state == 3,"Natural intruder never caught its first reserved sheep.");
        const int captured = game.sheep().front().captureStartMs;
        const auto capturedState = game.saveState();
        Game resumed(assets); resumed.restoreState(capturedState);
        require(resumed.saveState() == capturedState,"Active sheep capture did not roundtrip every field.");
        while (game.animalClockMs() < captured + SheepTurnMs) { game.tick(); resumed.tick(); }
        const float turnedYaw = game.sheep().front().yawDegrees;
        game.tick(); resumed.tick();
        require(near(game.sheep().front().yawDegrees,turnedYaw),"Capture yaw continued turning at the 600 ms boundary.");
        while (game.animalClockMs() < captured + SheepHideMs) { game.tick(); resumed.tick(); }
        require(game.sheep().front().state == 3,"Captured sheep hid before the 2600 ms update boundary.");
        game.tick(); resumed.tick();
        require(game.sheep().front().state == -1 && std::any_of(game.intruders().begin(),game.intruders().end(),
            [](const SheepIntruder& intruder) { return intruder.sheepIndex == 0; }),
            "2600 ms boundary did not retain the one-tick hidden-sheep/intruder pair.");
        Game hidden(assets); hidden.restoreState(game.saveState());
        require(hidden.saveState() == game.saveState(),"Hidden boundary pair could not roundtrip.");
        game.tick(); resumed.tick(); hidden.tick();
        require(std::none_of(game.intruders().begin(),game.intruders().end(),
            [](const SheepIntruder& intruder) { return intruder.sheepIndex == 0; })
            && game.saveState() == resumed.saveState() && game.saveState() == hidden.saveState(),
            "Post-2600 capture removal or resumed shared RNG diverged.");
        for (const bool malformed : {false,true}) {
            auto legacy = capturedState;
            if (malformed) legacy["animals"]["intruders"][0]["wave"] = -1;
            else legacy.erase("animals");
            Game fallback(assets); fallback.restoreState(legacy);
            auto actual = fallback.saveState(); actual.erase("animals"); legacy.erase("animals");
            require(actual == legacy,"Legacy or invalid animal extension altered valid core fields or restored combat RNG.");
            require(fallback.intruders().empty() && fallback.sheep().front().state == -1,
                "Legacy animal adaptation left lost sheep available for capture.");
        }
        Game seeded(assets,"PlainPlane",742193);
        const auto seededOpening = seeded.saveState(); advance(seeded,12);
        Game seededRestored(assets); seededRestored.restoreState(seeded.saveState()); seededRestored.restart();
        require(seededRestored.saveState() == seededOpening,"Saved native session seed was lost on restart.");
    });
    runCase(report,"realtimeEffectsPreserveCombat",[&] {
        Game ordinary(assets); require(ordinary.build(7,10) && ordinary.sendWave(),"Effect clock defense setup failed.");
        Game accelerated = ordinary; accelerated.advanceEffects(100);
        // Four-times game progression must not accelerate original effect lifetimes or change RNG.
        for (int tick = 1; tick <= 1200; ++tick) {
            ordinary.tick(); accelerated.advanceEffects(100 + tick * SimulationStepMs / 4000.0); accelerated.tick();
        }
        require(ordinary.saveState() == accelerated.saveState(),"Cosmetic realtime changed complete gameplay or combat RNG state.");
        auto rail = fixture(assets,"Railgun I"); target(rail); singleShot(rail);
        require(!rail.beams.empty(),"Real rail shot produced no lifetime record.");
        rail.advanceEffects(200); rail.paused = true;
        const auto frozen = rail.saveState();
        rail.advanceEffects(201);
        require(!rail.beams.empty() && rail.saveState() == frozen,"Paused realtime changed gameplay or expired a two-second beam early.");
        rail.advanceEffects(202.01);
        require(rail.beams.empty() && rail.saveState() == frozen,"Paused visual beam did not expire independently of fixed game time.");
    });
    report["weaponFamiliesChecked"] = {"beam","plasma","railgun","homing","mortar","flak","flamethrower","lightning","mine","slow","boost","bait"};
    const auto path = output / "coverage-checks.json";
    std::ofstream stream(path); stream << report.dump(2); stream.close();
    return report.value("passed",false) && static_cast<bool>(stream) ? 0 : 1;
}
} // namespace tmreborn
