/** Opt-in original late-battle save/load acceptance driver; never used by normal play. */
#pragma once

namespace late_battle_probe {
// Match the verified strategy's 100 simulation seconds per wave at the real HUD's 4x speed.
constexpr ULONGLONG WaveInterval = 25000;
// Observe beyond the original three-second resume countdown.
constexpr ULONGLONG RestoreObservation = 8000;
// Require a mature original battle before exercising F5/F9.
constexpr int MatureSimulation = 800000;
// Keep readable periodic evidence and allow the original victory page to settle.
constexpr ULONGLONG SampleInterval = 5000, VictorySettle = 3000;

/** Count the original IBS enemy/projectile chains without changing guest data. */
inline nlohmann::json entityCounts(void* game) {
    const auto* enemies = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<void**>(game)[3]);
    const auto* projectiles = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<void**>(game)[5]);
    // Exceeds the original enemy ID capacity while bounding a corrupt linked list.
    constexpr int ChainLimit = 8192;
    int enemyCount = 0, projectileCount = 0;
    for (auto* enemy = reinterpret_cast<const std::uint32_t*>(enemies[0x405]); enemy;
         enemy = reinterpret_cast<const std::uint32_t*>(enemy[0x1c])) {
        if (++enemyCount > ChainLimit) throw std::runtime_error("Original enemy chain did not terminate");
    }
    for (auto* projectile = reinterpret_cast<const std::uint32_t*>(projectiles[1]); projectile;
         projectile = reinterpret_cast<const std::uint32_t*>(projectile[0x2f])) {
        if (++projectileCount > ChainLimit) throw std::runtime_error("Original projectile chain did not terminate");
    }
    return {{"enemies", enemyCount}, {"projectiles", projectileCount}};
}

/** Advance only this explicit verification scenario using original commands and HUD events. */
struct Driver {
    bool speedSet = false, saved = false, exactChecked = false, advancedChecked = false;
    ULONGLONG nextWave = 0, saveAt = 0, nextSample = 0, victoryAt = 0;
    int savedClock = 0;
    nlohmann::json savedCounts, savedPlayer;

    /** Restore the test cadence through the real HUD, preserving original IBS speed semantics. */
    void selectFastSpeed(ULONGLONG elapsed) {
        for (int click = 0; click < 2; ++click) {
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(460, 56));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(460, 56));
        }
        nextWave = elapsed + WaveInterval;
    }

    /** Compare restored state before the first update can move enemies or resolve projectiles. */
    void verifyRestored(void* game, ULONGLONG elapsed) {
        if (!saved || exactChecked || !game) return;
        const auto counts = entityCounts(game), state = playerState(game);
        log("LATE_BATTLE_RESTORED_EXACT entities=" + counts.dump() + " " + state.dump());
        if (counts != savedCounts || state != savedPlayer)
            throw std::runtime_error("Original late IBS restore changed entity counts or original player evaluators");
        exactChecked = true;
        // GAME_alloc defaults to 1x after IBS; host-time wave input must return to its original 4x cadence.
        selectFastSpeed(elapsed);
    }

    /** Drive legal combat and return true only after restored gameplay reaches original victory. */
    bool tick(void* menu, void* game, void* page, bool gameIsOver, ULONGLONG elapsed,
              const std::filesystem::path& output) {
        if (!game) return false;
        if (playerState(game).at("map") != "Madness")
            throw std::runtime_error("Late battle requires authentic Madness checkpoint");
        if (!speedSet) { speedSet = true; selectFastSpeed(elapsed); }
        if (!gameIsOver) {
            campaignMadnessDefense(game);
            if (elapsed >= nextWave) {
                nextWave = elapsed + WaveInterval;
                SendMessageW(windowHandle, WM_KEYDOWN, VK_SPACE, 0);
                log("LATE_BATTLE_WAVE " + playerState(game).dump());
            }
            if (elapsed >= nextSample) {
                nextSample = elapsed + SampleInterval;
                log("LATE_BATTLE_SAMPLE entities=" + entityCounts(game).dump() + " " + playerState(game).dump());
            }
            if (!saved && currentGameClock(menu) >= MatureSimulation) {
                void* towers = reinterpret_cast<void**>(game)[4];
                auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
                auto towerForId = reinterpret_cast<int*(__cdecl*)(void*, int)>(requireExport("TOWERS_getTowerForId"));
                // Nine legal firing cells plus the original Slow and Boost support cells.
                constexpr int Cells[][2] = {{8,24},{12,24},{8,20},{12,20},{10,18},{16,24},{16,20},{6,16},{14,16},{10,24},{10,22}};
                int towerCount = 0;
                for (const auto& cell : Cells) {
                    const int id = idForCell(towers, cell[0], cell[1]);
                    if (id != -1 && towerForId(towers, id)) ++towerCount;
                }
                const auto counts = entityCounts(game);
                if (towerCount == std::size(Cells) && counts.at("projectiles").get<int>() >= 2
                    && counts.at("enemies").get<int>() >= 1) {
                    saved = true; saveAt = elapsed; savedClock = currentGameClock(menu);
                    savedCounts = counts; savedPlayer = playerState(game);
                    capture(output / "late-battle-before-save.png", 480, 800);
                    log("LATE_BATTLE_ACTIVE towers=" + std::to_string(towerCount) + " entities=" + counts.dump()
                        + " " + savedPlayer.dump());
                    SendMessageW(windowHandle, WM_KEYDOWN, VK_F5, 0);
                    SendMessageW(windowHandle, WM_KEYDOWN, VK_F9, 0);
                }
            }
            if (saved && !advancedChecked && elapsed >= saveAt + RestoreObservation) {
                if (currentGameClock(menu) <= savedClock)
                    throw std::runtime_error("Late battle restore did not advance");
                advancedChecked = true;
                capture(output / "late-battle-restored-advancing.png", 480, 800);
                log("LATE_BATTLE_RESTORE_ADVANCED " + playerState(game).dump());
            }
        } else if (page == findExport("endOfGamePage")) {
            if (!playerState(game).at("won").get<bool>()) throw std::runtime_error("Late restored battle lost");
            if (!victoryAt) victoryAt = elapsed;
            if (elapsed >= victoryAt + VictorySettle) {
                if (!saved || !exactChecked || !advancedChecked)
                    throw std::runtime_error("Victory did not cover late restoration");
                capture(output / "late-battle-victory.png", 480, 800);
                log("LATE_BATTLE_DONE " + playerState(game).dump());
                return true;
            }
        }
        return false;
    }
};
} // namespace late_battle_probe
