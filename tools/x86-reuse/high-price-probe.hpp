/** Opt-in high-price tower acceptance using original earnings, commands and IBS saves. */
#pragma once

namespace high_price_probe {
// Keep three firing support towers while a costly chain completes at original build times.
constexpr int DefenseCells[][2] = {{12,10},{28,8},{8,26},{26,26}};
// Genuine earnings fund Railgun VI plus three retained Gauss chains and a small reserve.
constexpr int HarvestTotal = 4400, GaussChainCost = 355;
// Real-time limits bound failed acceptance runs without altering simulation time.
constexpr ULONGLONG RunLimit = 1200000, BuildLimit = 180000, DamageLimit = 180000;

/** Drive a dedicated original GAME; all resource changes go through original commands. */
inline void run(void* game, void* mode, const std::filesystem::path& output) {
    const char* selected = std::getenv("TOWERMADNESS_HIGH_PRICE_CASE");
    const char* checkpoint = std::getenv("TOWERMADNESS_HIGH_PRICE_CHECKPOINT");
    if (!selected || !checkpoint) throw std::runtime_error("High-price probe requires case/checkpoint environment");
    const std::string targetName = selected;
    int cells[4][2];
    std::memcpy(cells,DefenseCells,sizeof(cells));
    auto update = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"));
    auto render = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"));
    auto build = reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(requireExport("GAME_buildTower"));
    auto upgrade = reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_upgradeTower"));
    auto sell = reinterpret_cast<void(__cdecl*)(void*, int)>(requireExport("GAME_sellTower"));
    auto send = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_sendNextWave"));
    auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
    auto towerForId = reinterpret_cast<int*(__cdecl*)(void*, int)>(requireExport("TOWERS_getTowerForId"));
    const ULONGLONG started = GetTickCount64();
    nlohmann::json result{{"case",targetName},{"initial",playerState(game)},{"events",nlohmann::json::array()}};
    /** Record original state after each meaningful action. */
    const auto record = [&](const std::string& action) {
        auto entry = nlohmann::json{{"action",action},{"state",playerState(game)}};
        result["events"].push_back(entry); log("HIGH_PRICE_EVENT " + entry.dump());
        std::ofstream(output / "high-price-result.json") << result.dump(2);
    };
    /** Pump normal game time and host rendering with no synthetic delta. */
    const auto tick = [&]() {
        update(game); render(game); presentDisplay();
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(8);
        if (GetTickCount64()-started > RunLimit) throw std::runtime_error("High-price acceptance timed out");
        if (playerState(game).at("ended").get<bool>()) throw std::runtime_error("High-price battle ended before acceptance");
    };
    /** Read current tower ownership after restores replace all guest pointers. */
    const auto towerAt = [&](int x,int y) -> int* {
        void* towers = reinterpret_cast<void**>(game)[4];
        const int id = idForCell(towers,x,y);
        return id == -1 ? nullptr : towerForId(towers,id);
    };
    /** Advance only until the original accepts and completes the requested upgrade. */
    const auto chain = [&](int x,int y,const std::string& baseName,const std::string& capName) {
        mode = *reinterpret_cast<void**>(game);
        if (!towerAt(x,y)) {
            const int base=nativeTowerId(mode,baseName.c_str());
            const auto* tech=reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint32_t*>(mode)[7]);
            const auto* types=reinterpret_cast<const std::uint32_t* const*>(tech[15]);
            auto canBuild=reinterpret_cast<int(__cdecl*)(void*,int,int,int,int)>(requireExport("TOWERS_canBuildHere"));
            if (canBuild(reinterpret_cast<void**>(game)[4],x,y,0,types[base][34])!=0)
                throw std::runtime_error("High-price defense cell rejected: "+std::to_string(x)+","+std::to_string(y));
            build(game,base,x,y); record("build-request-"+baseName);
        }
        const int cap = nativeTowerId(mode,capName.c_str());
        ULONGLONG deadline = GetTickCount64()+BuildLimit;
        int requested=-1;
        for (;;) {
            tick(); int* tower = towerAt(x,y);
            if (tower && !tower[16] && tower[1] == cap) break;
            if (GetTickCount64()>deadline) throw std::runtime_error("Original high-price chain could not complete");
            if (!tower || tower[16] || tower[17] || tower[25] != -1) continue;
            const auto* tech = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint32_t*>(mode)[7]);
            const auto* types = reinterpret_cast<const std::uint32_t* const*>(tech[15]);
            const auto* current = types[tower[1]];
            if (current[25] != 1) throw std::runtime_error("High-price chain has no unique original upgrade edge");
            const int next = *reinterpret_cast<const int*>(current[27]);
            if (requested==next) continue;
            if (playerState(game).at("money").get<int>() < static_cast<int>(types[next][15]))
                throw std::runtime_error("Authentic checkpoint cannot afford high-price upgrade");
            upgrade(game,tower[0],next); requested=next; record("upgrade-request-"+std::to_string(next));
        }
        record("chain-complete-"+capName);
    };
    /** Write the original GAME_save stream through the normal save container. */
    const auto save = [&](const std::filesystem::path& path) {
        void* obs = reinterpret_cast<void*(__cdecl*)(void*)>(requireExport("GAME_save"))(game);
        const auto* bytes = static_cast<const std::uint32_t*>(obs);
        writeSaveState(path,reinterpret_cast<const void*>(bytes[0]),bytes[1]);
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("OBS_free"))(obs);
        record("saved-"+path.filename().string());
    };
    /** Free the previous GAME before loading its genuine IBS bytes at a frame boundary. */
    const auto restore = [&](const std::filesystem::path& path) {
        std::vector<std::uint8_t> bytes;
        if (!readSaveState(path,bytes)) throw std::runtime_error("High-price checkpoint cannot be read");
        activeGame = nullptr;
        *reinterpret_cast<void**>(hostMenu.data()+0x15c) = nullptr;
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_free"))(game);
        void* ibs = reinterpret_cast<void*(__cdecl*)(void*,unsigned,unsigned)>(requireExport("IBS_wrap"))(bytes.data(),static_cast<unsigned>(bytes.size()),0);
        game = reinterpret_cast<void*(__cdecl*)(void*)>(requireExport("GAME_fromIBS"))(ibs);
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("IBS_free"))(ibs);
        if (!game) throw std::runtime_error("Original IBS rejected high-price checkpoint");
        mode = *reinterpret_cast<void**>(game); activeGame = game;
        *reinterpret_cast<void**>(hostMenu.data()+0x15c) = game;
        reinterpret_cast<void(__cdecl*)(void*,int,int)>(requireExport("GAME_setResolution"))(game,LogicalWidth,LogicalHeight);
        record("restored-"+path.filename().string());
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_resume"))(game);
    };
    /** Select 4x through the original HUD input instead of writing the speed field. */
    const auto fast = [&]() {
        for (int i=0;i<2;++i) {
            const POINT point=hostDisplay.sceneProbePoint(460,56);
            SendMessageW(windowHandle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
            SendMessageW(windowHandle,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        }
    };
    /** Count the original enemy chain while preserving queued wave spawn timing. */
    const auto enemies = [&]() {
        const auto* raw = static_cast<const std::uint32_t*>(reinterpret_cast<void**>(game)[3]);
        int count=0;
        for (auto* e=reinterpret_cast<const std::uint32_t*>(raw[0x405]);e;e=reinterpret_cast<const std::uint32_t*>(e[0x1c]))
            if (++count>8192) throw std::runtime_error("Enemy chain does not terminate");
        return count;
    };
    if (targetName == "harvest") {
        auto canBuild=reinterpret_cast<int(__cdecl*)(void*,int,int,int,int)>(requireExport("TOWERS_canBuildHere"));
        for (auto& cell:cells) {
            bool found=false;
            for (int radius=0;radius<=8 && !found;++radius)
                for (int dy=-radius;dy<=radius && !found;++dy)
                    for (int dx=-radius;dx<=radius && !found;++dx) {
                        if ((std::max)(std::abs(dx),std::abs(dy))!=radius) continue;
                        const int x=cell[0]+dx,y=cell[1]+dy;
                        if (x<0 || y<0 || x>=40 || y>=38) continue;
                        if (canBuild(reinterpret_cast<void**>(game)[4],x,y,0,0)==0) {
                            cell[0]=x;cell[1]=y;found=true;
                        }
                    }
            if (!found) throw std::runtime_error("No legal original harvest cell");
            chain(cell[0],cell[1],"Plasmatron I","Gauss Cannon");
        }
        nlohmann::json placement=nlohmann::json::array();
        for (const auto& cell:cells) placement.push_back({cell[0],cell[1]});
        std::ofstream(std::string(checkpoint)+".json")<<placement.dump(2);
        fast(); int waveClock=-100000, waves=0; ULONGLONG sample=0;
        for (;;) {
            tick(); const int clock=static_cast<int*>(game)[10];
            if (GetTickCount64()>=sample) { sample=GetTickCount64()+5000; record("harvest-sample"); }
            if (waves && playerState(game).at("money").get<int>()+4*GaussChainCost>=HarvestTotal) break;
            if (clock-waveClock<30000 || enemies()!=0) continue;
            send(game); waveClock=clock; ++waves; record("harvest-wave-"+std::to_string(waves));
        }
        save(checkpoint); result["wavesRequested"]=waves; result["passed"]=true;
    } else {
        nlohmann::json placement;
        std::ifstream(std::string(checkpoint)+".json")>>placement;
        for (int i=0;i<4;++i) { cells[i][0]=placement.at(i).at(0);cells[i][1]=placement.at(i).at(1); }
        restore(checkpoint); fast(); result["funded"]=playerState(game);
        const std::string base = targetName=="Fusion Cannon" ? "Plasmatron I" : targetName=="Railgun VI" ? "Railgun I" : targetName=="Tesla Coil" ? "ElectroShock I" : targetName=="Flamethrower III" ? "Flamethrower I" : "";
        if (base.empty()) throw std::runtime_error("Unknown high-price target");
        const int x=cells[0][0], y=cells[0][1];
        int* replaced=towerAt(x,y);
        if (!replaced) throw std::runtime_error("Harvest defense missing at target cell");
        sell(game,replaced[0]);
        while (towerAt(x,y)) tick();
        record("original-defense-sold");
        chain(x,y,base,targetName);
        int* tower=towerAt(x,y); const int id=tower[0], type=tower[1];
        const auto snapshot=playerState(game); const int savedClock=static_cast<int*>(game)[10];
        const auto towerState=nlohmann::json{{"id",id},{"type",type},{"x",tower[4]},{"y",tower[5]}};
        result["tower"]=towerState;
        const auto saved=output/"high-price-tower.bin"; save(saved); restore(saved);
        tower=towerAt(x,y);
        if (!tower || tower[0]!=id || tower[1]!=type || playerState(game)!=snapshot)
            throw std::runtime_error("High-price original IBS did not preserve tower/player state");
        record("restore-exact"); fast();
        // Observe the original replay damage callback and weapon effects, forwarding every call unchanged.
        combatOriginalFires.clear(); combatTowerTypes.clear(); combatCounters.clear();
        const auto* technology=reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint32_t*>(mode)[7]);
        auto** weapons=reinterpret_cast<void**>(technology[17]);
        for (unsigned i=0;i<technology[16];++i) {
            auto* weapon=static_cast<std::uint32_t*>(weapons[i]);
            combatOriginalFires[weapon]=reinterpret_cast<CombatFire>(weapon[5]);
            weapon[5]=reinterpret_cast<std::uint32_t>(&combatFire);
        }
        combatTowerTypes[id]=type;
        combatCounters[type]=nlohmann::json::object();
        combatOriginalDamage=reinterpret_cast<decltype(combatOriginalDamage)>(static_cast<std::uint32_t*>(game)[0xb9c/4]);
        static_cast<std::uint32_t*>(game)[0xb9c/4]=reinterpret_cast<std::uint32_t>(&combatDamage);
        const float before=*reinterpret_cast<float*>(tower+0x3c/4);
        const ULONGLONG deadline=GetTickCount64()+DamageLimit;
        ULONGLONG nextWave=0;
        while (GetTickCount64()<deadline) {
            tick(); tower=towerAt(x,y);
            if (tower && combatCounters[type].value("damage",0)>0 && static_cast<int*>(game)[10]>savedClock) break;
            if (GetTickCount64()>=nextWave && enemies()==0) { send(game); nextWave=GetTickCount64()+12000; }
        }
        if (!tower || combatCounters[type].value("damage",0)<=0) throw std::runtime_error("Restored high-price tower delivered no original damage");
        result["deliveredDamage"]=combatCounters[type].value("damage",0);
        result["towerDamageDelta"]=*reinterpret_cast<float*>(tower+0x3c/4)-before;
        result["combatCounters"]=combatCounters[type];
        record("restored-attack"); capture(output/"high-price-attack.png",480,800);
        const int money=playerState(game).at("money"); sell(game,id);
        while (towerAt(x,y)) tick();
        if (playerState(game).at("money").get<int>()<=money) throw std::runtime_error("High-price sale returned no money");
        record("sold"); result["passed"]=true;
    }
    result["final"]=playerState(game);
    std::ofstream(output/"high-price-result.json")<<result.dump(2);
    log("HIGH_PRICE_DONE "+result.dump());
}
} // namespace high_price_probe
