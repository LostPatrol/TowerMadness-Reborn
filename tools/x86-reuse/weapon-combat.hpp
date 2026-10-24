// Observe real original weapon combat only in the dedicated acceptance mode.
// The normal game keeps its original callbacks and gameplay state.
#pragma once

// Original weapon callback ABI and the callbacks temporarily observed by this probe.
using CombatFire = void(__cdecl*)(void*, void*, int, int*);
std::map<void*, CombatFire> combatOriginalFires;
// Instance-to-type attribution and observed counters remain separate from gameplay fields.
std::map<int, int> combatTowerTypes;
std::map<int, nlohmann::json> combatCounters;
// Forward damage notifications to the original replay callback after recording them.
void(__cdecl* combatOriginalDamage)(void*, int, int) = nullptr;

/** Record original delivered damage, then retain the game's existing replay callback. */
void __cdecl combatDamage(void* game, int id, int damage) {
    const auto found = combatTowerTypes.find(id);
    if (found != combatTowerTypes.end()) {
        auto& counter = combatCounters[found->second];
        if (counter.is_null()) counter = nlohmann::json::object();
        counter["damage"] = counter.value("damage", 0) + damage;
        counter["damageEvents"] = counter.value("damageEvents", 0) + 1;
    }
    if (combatOriginalDamage) combatOriginalDamage(game, id, damage);
}

/** Observe real effect allocation and enemy status around an unchanged original fire callback. */
void __cdecl combatFire(void* weapon, void* game, int enemyId, int* tower) {
    const int id = tower[0], type = tower[1];
    combatTowerTypes[id] = type;
    auto& counter = combatCounters[type];
    if (counter.is_null()) counter = nlohmann::json::object();
    const bool started = static_cast<int*>(game)[6] != 0;
    if (started) counter["battleCalls"] = counter.value("battleCalls", 0) + 1;
    auto heapSize = reinterpret_cast<int(__cdecl*)(void*)>(requireExport("HEAP_numElements"));
    auto* raw = static_cast<void**>(game);
    int before = 0;
    for (int index = 0x2d0; index <= 0x2d2; ++index) before += heapSize(raw[index]);
    combatOriginalFires.at(weapon)(weapon, game, enemyId, tower);
    int after = 0;
    for (int index = 0x2d0; index <= 0x2d2; ++index) after += heapSize(raw[index]);
    if (started && after > before) counter["effectAdds"] = counter.value("effectAdds", 0) + after - before;
    auto* enemies = reinterpret_cast<int**>(raw[3]);
    for (int index = 0; index < 1024; ++index) {
        const int* enemy = enemies[index + 5];
        if (!enemy) continue;
        if (enemy[0x5c / 4] > 0) {
            counter["maxSlowMs"] = (std::max)(counter.value("maxSlowMs", 0), enemy[0x5c / 4]);
            counter["slowFactor"] = *reinterpret_cast<const float*>(enemy + 0x60 / 4);
        }
        if (*reinterpret_cast<const float*>(enemy + 0x68 / 4) > 1.0f)
            counter["heatedEnemy"] = true;
    }
}

/** Exercise one real weapon under original waves, without changing gameplay resources or state. */
void originalWeaponCombat(void* game, void* mode, const std::filesystem::path& output) {
    const char* configPath = std::getenv("TOWERMADNESS_COMBAT_SCRIPT");
    if (!configPath) throw std::runtime_error("Combat probe needs TOWERMADNESS_COMBAT_SCRIPT");
    nlohmann::json config; std::ifstream(configPath) >> config;
    if (!config.contains("base")) {
        const char* selected = std::getenv("TOWERMADNESS_COMBAT_CASE");
        if (!selected) throw std::runtime_error("Combat probe needs TOWERMADNESS_COMBAT_CASE");
        config = config.at(selected);
    }
    std::ofstream(output / "combat-script.json") << config.dump(2);
    auto update = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"));
    auto render = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"));
    auto build = reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(requireExport("GAME_buildTower"));
    auto upgrade = reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_upgradeTower"));
    auto towerForId = reinterpret_cast<int*(__cdecl*)(void*, int)>(requireExport("TOWERS_getTowerForId"));
    auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
    auto canBuild = reinterpret_cast<int(__cdecl*)(void*, int, int, int, int)>(requireExport("TOWERS_canBuildHere"));
    auto* towers = reinterpret_cast<void**>(game)[4];
    auto* technology = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint32_t*>(mode)[7]);
    const auto* types = reinterpret_cast<const std::uint32_t* const*>(technology[15]);
    auto** weapons = reinterpret_cast<void**>(technology[17]);
    for (unsigned index = 0; index < technology[16]; ++index) {
        auto* weapon = static_cast<std::uint32_t*>(weapons[index]);
        combatOriginalFires[weapon] = reinterpret_cast<CombatFire>(weapon[5]);
        weapon[5] = reinterpret_cast<std::uint32_t>(&combatFire);
    }
    combatOriginalDamage = reinterpret_cast<decltype(combatOriginalDamage)>(static_cast<std::uint32_t*>(game)[0xb9c / 4]);
    static_cast<std::uint32_t*>(game)[0xb9c / 4] = reinterpret_cast<std::uint32_t>(&combatDamage);
    nlohmann::json result{{"initial", playerState(game)}, {"config", config}, {"samples", nlohmann::json::array()}};
    // Pump ordinary original time and rendering; no synthetic simulation deltas are supplied.
    framePacer = std::make_unique<HostFramePacer>();
    auto tick = [&]() {
        update(game); render(game); SwapBuffers(windowDc);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        // Match normal desktop pacing and incremental cache preparation in this combat-only probe.
        finishHostFrame();
    };
    const std::string name = config.at("base");
    const int base = nativeTowerId(mode, name.c_str());
    const int target = nativeTowerId(mode, config.value("target", name).c_str());
    const auto placements = config.value("placements", nlohmann::json::array());
    if (!placements.empty() && target != base)
        throw std::runtime_error("Multi-placement combat probe requires the original base type");
    // A pad itself may reject construction; search nearby cells using original placement checks.
    constexpr int PlacementSearchRadius = 4;
    auto resolveCell = [&](int requestedX, int requestedY) -> std::pair<int, int> {
        int checked = 0;
        for (int radius = 0; radius <= PlacementSearchRadius; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
                if ((std::max)(std::abs(dx), std::abs(dy)) != radius) continue;
                ++checked;
                const int x = requestedX + dx, y = requestedY + dy;
                if (canBuild(towers, x, y, 0, types[base][34]) == 0) {
                    log("COMBAT_LEGAL_PLACEMENT " + nlohmann::json{{"requested", {requestedX, requestedY}}, {"cell", {x, y}}, {"checked", checked}}.dump());
                    return {x, y};
                }
            }
        }
        throw std::runtime_error("No original-legal combat placement near requested pad");
    };
    const auto primaryCell = placements.empty()
        ? std::make_pair(config.value("x", 12), config.value("y", 10))
        : resolveCell(placements.at(0).at(0), placements.at(0).at(1));
    const int wantedX = primaryCell.first, wantedY = primaryCell.second;
    if (canBuild(towers, wantedX, wantedY, 0, types[base][34]) != 0)
        throw std::runtime_error("Combat probe cell rejected by original terrain/path check");
    build(game, base, wantedX, wantedY);
    int id = -1;
    ULONGLONG until = GetTickCount64() + 70000;
    while (GetTickCount64() < until) {
        tick(); id = idForCell(towers, wantedX, wantedY);
        int* tower = id == -1 ? nullptr : towerForId(towers, id);
        if (tower && !tower[16]) break;
    }
    int* tower = id == -1 ? nullptr : towerForId(towers, id);
    if (!tower || tower[16]) throw std::runtime_error("Combat probe original build did not finish");
    while (tower[1] != target) {
        const auto* type = types[tower[1]];
        if (type[25] != 1) throw std::runtime_error("Combat probe needs one legal original upgrade edge");
        const int next = *reinterpret_cast<const int*>(type[27]);
        upgrade(game, id, next); until = GetTickCount64() + 70000;
        while (GetTickCount64() < until) { tick(); tower = towerForId(towers, id); if (tower && tower[1] == next && !tower[16]) break; }
        if (!tower || tower[1] != next || tower[16]) throw std::runtime_error("Combat probe original upgrade did not finish");
    }
    combatTowerTypes[id] = target;
    if (!placements.empty()) {
        result["placements"] = nlohmann::json::array();
        result["placements"].push_back({{"requested", placements.at(0)}, {"cell", {wantedX, wantedY}},
            {"id", id}, {"moneySpent", result.at("initial").at("money").get<int>() - playerState(game).at("money").get<int>()}});
        for (std::size_t index = 1; index < placements.size(); ++index) {
            const auto cell = resolveCell(placements.at(index).at(0), placements.at(index).at(1));
            const int moneyBefore = playerState(game).at("money");
            build(game, base, cell.first, cell.second);
            until = GetTickCount64() + 70000;
            int extraId = -1;
            int* extra = nullptr;
            while (GetTickCount64() < until) {
                tick(); extraId = idForCell(towers, cell.first, cell.second);
                extra = extraId == -1 ? nullptr : towerForId(towers, extraId);
                if (extra && !extra[16]) break;
            }
            if (!extra || extra[16] || extra[1] != base)
                throw std::runtime_error("Original additional combat tower did not finish");
            combatTowerTypes[extraId] = target;
            result["placements"].push_back({{"requested", placements.at(index)}, {"cell", {cell.first, cell.second}},
                {"id", extraId}, {"moneySpent", moneyBefore - playerState(game).at("money").get<int>()}});
        }
        result["buildMoneySpent"] = result.at("initial").at("money").get<int>() - playerState(game).at("money").get<int>();
        log("COMBAT_ORIGINAL_BUILD_COST " + result.at("placements").dump());
    }
    int companionX = -1, companionY = -1;
    if (config.value("companion", false)) {
        const int laser = nativeTowerId(mode, "Kilowatt Laser");
        // Search original-legal neighboring footprints inside the support's three-cell radius.
        static constexpr int Offsets[][2] = {{2,0}, {-2,0}, {0,2}, {0,-2}, {2,2}, {-2,2}, {2,-2}, {-2,-2}};
        for (const auto& offset : Offsets) {
            const int x = wantedX + offset[0], y = wantedY + offset[1];
            if (canBuild(towers, x, y, 0, types[laser][34]) == 0) { companionX = x; companionY = y; break; }
        }
        if (companionX < 0) throw std::runtime_error("No original-legal neighboring companion footprint");
        result["companionCell"] = {companionX, companionY};
        build(game, laser, companionX, companionY);
        until = GetTickCount64() + 5000;
        while (GetTickCount64() < until) { tick(); const int extraId = idForCell(towers, companionX, companionY);
            int* extra = extraId == -1 ? nullptr : towerForId(towers, extraId);
            if (extra && !extra[16]) { combatTowerTypes[extraId] = laser; break; } }
    }
    result["built"] = playerState(game);
    reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_togglePlaySpeed"))(game);
    reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_togglePlaySpeed"))(game);
    const ULONGLONG start = GetTickCount64();
    const ULONGLONG duration = config.value("durationMs", 45000);
    unsigned sent = 0; ULONGLONG nextSample = 0;
    bool firstFire = false, firstDamage = false, firstSlow = false, firstBoost = false;
    // Allow original hit effects to become visible after the first delivered damage.
    constexpr ULONGLONG HitVisibleDelayMs = 250;
    ULONGLONG firstDamageAt = 0;
    bool hitVisible = false;
    while (GetTickCount64() - start < duration) {
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (sent < config.value("waves", 1u) && elapsed >= sent * 1000) {
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_sendNextWave"))(game); ++sent;
            log("COMBAT_WAVE_REQUEST " + std::to_string(sent));
        }
        tick();
        auto& counter = combatCounters[target];
        if (counter.is_null()) counter = nlohmann::json::object();
        tower = towerForId(towers, id);
        if (tower) {
            counter["kills"] = tower[0x38 / 4]; counter["towerDamage"] = *reinterpret_cast<const float*>(tower + 0x3c / 4);
            counter["boostFactor"] = *reinterpret_cast<const float*>(tower + 0x5c / 4);
            counter["armedClock"] = tower[0x64 / 4];
        } else counter["removed"] = true;
        if (!firstFire && counter.value("effectAdds", 0) > 0) { capture(output / "combat-first-effect.png", 480, 800); firstFire = true; }
        if (!firstDamage && counter.value("damage", 0) > 0) {
            capture(output / "combat-first-damage.png", 480, 800);
            firstDamage = true;
            firstDamageAt = GetTickCount64();
        }
        if (firstDamage && !hitVisible && GetTickCount64() - firstDamageAt >= HitVisibleDelayMs) {
            capture(output / "combat-hit-visible.png", 480, 800);
            hitVisible = true;
        }
        if (!firstSlow && counter.value("maxSlowMs", 0) > 0) { capture(output / "combat-first-slow.png", 480, 800); firstSlow = true; }
        // Read the companion's original damage multiplier, changed only by original TOWERS_boost.
        if (config.value("companion", false)) {
            const int extraId = idForCell(towers, companionX, companionY);
            const int* extra = extraId == -1 ? nullptr : towerForId(towers, extraId);
            if (extra) counter["companionBoostMax"] = (std::max)(counter.value("companionBoostMax", 1.0f), *reinterpret_cast<const float*>(extra + 0x5c / 4));
            if (!firstBoost && counter.value("companionBoostMax", 1.0f) > 1.0f) { capture(output / "combat-first-boost.png", 480, 800); firstBoost = true; }
        }
        if (elapsed >= nextSample) {
            nlohmann::json sample{{"hostMs", elapsed}, {"state", playerState(game)}, {"counters", combatCounters}};
            result["samples"].push_back(sample); log("COMBAT_SAMPLE " + sample.dump()); nextSample = elapsed + 1000;
        }
        if (reinterpret_cast<int(__cdecl*)(void*)>(requireExport("GAME_hasEnded"))(game)) break;
    }
    result["final"] = playerState(game); result["counters"] = combatCounters; result["wavesRequested"] = sent;
    std::ofstream(output / "weapon-combat-result.json") << result.dump(2);
    capture(output / "combat-final.png", 480, 800);
    log("COMBAT_DONE " + result.dump());
    const auto& observed = combatCounters[target];
    const std::string expected = config.at("expect");
    if (expected == "damage" && observed.value("damage", 0) <= 0)
        throw std::runtime_error("Original weapon did not deliver observed damage");
    if (expected == "slow" && observed.value("maxSlowMs", 0) <= 0)
        throw std::runtime_error("Original support did not apply observed enemy slowdown");
    if (expected == "boost" && observed.value("companionBoostMax", 1.0f) <= 1.0f)
        throw std::runtime_error("Original support did not increase companion multiplier");
    if (expected == "mine" && (!observed.value("removed", false) || observed.value("damage", 0) <= 0))
        throw std::runtime_error("Original mine did not detonate and damage enemies");
    if (expected == "bait" && (!observed.value("removed", false) || observed.value("maxSlowMs", 0) <= 0))
        throw std::runtime_error("Original bait did not detonate and slow enemies");
    if (expected != "damage" && expected != "slow" && expected != "boost" && expected != "mine" && expected != "bait")
        throw std::runtime_error("Combat probe has no supported trigger assertion");
    log("WEAPON_COMBAT_PASSED case=" + config.at("key").get<std::string>() + " expect=" + expected);
}
