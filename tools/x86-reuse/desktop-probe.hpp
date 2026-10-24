// Exercise the desktop additions through real Windows messages in isolated test state.
#pragma once

namespace desktop_present_probe {
// One bounded driver owns frame observations; normal play never enables this readback.
struct State {
    std::filesystem::path output;
    nlohmann::json frames = nlohmann::json::array();
    bool beganShowing = false;
    int whiteFrames = 0, solidWhiteFrames = 0, whiteHandoffs = 0, handoffs = 0, failedSwaps = 0;
    LARGE_INTEGER swapStart{};
};
State* state = nullptr;

/** Sample the final back buffer after overlays, then record the matching real swap result. */
void observe(bool afterSwap, BOOL succeeded) {
    State& sample = *state;
    LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    if (afterSwap) {
        sample.frames.back()["swapMs"] = 1000.0 * (now.QuadPart - sample.swapStart.QuadPart) / frequency.QuadPart;
        sample.frames.back()["swapSucceeded"] = succeeded != FALSE;
        if (!succeeded) ++sample.failedSwaps;
        return;
    }
    auto* menu = static_cast<unsigned char*>(originalMenu);
    void* game = *reinterpret_cast<void**>(menu + 0x15c);
    const bool showing = menu[4] != 0;
    if (sample.beganShowing && !showing && game) ++sample.handoffs;
    GLint readBuffer = 0, drawBuffer = 0; GLboolean doubleBuffer = GL_FALSE;
    glGetIntegerv(GL_READ_BUFFER, &readBuffer); glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffer);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(hostDisplay.width) * hostDisplay.height * 3);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, hostDisplay.width, hostDisplay.height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glReadBuffer(readBuffer); glPopClientAttrib();
    std::size_t white = 0;
    for (std::size_t at = 0; at < pixels.size(); at += 3)
        white += pixels[at] > 240 && pixels[at + 1] > 240 && pixels[at + 2] > 240;
    const double fraction = white / static_cast<double>(pixels.size() / 3);
    if (fraction > 0.95) {
        ++sample.whiteFrames;
        if (sample.beganShowing && !showing && game) ++sample.whiteHandoffs;
    }
    // Bright original menu fades are recorded separately from a solid-white submitted scene.
    if (fraction > 0.999) {
        if (sample.solidWhiteFrames < 4) capture(sample.output / ("submitted-white-" + std::to_string(sample.solidWhiteFrames) + ".png"), LogicalWidth, LogicalHeight);
        ++sample.solidWhiteFrames;
    }
    sample.frames.push_back({{"frame",sample.frames.size()},{"beganShowing",sample.beganShowing},{"showing",showing},
        {"game",game!=nullptr},{"tutorial",game?*reinterpret_cast<int*>(static_cast<unsigned char*>(game)+0xa38):0},
        {"fade",*reinterpret_cast<float*>(menu+0xec)},{"transitionTexture",*reinterpret_cast<int*>(menu+0x10)},
        {"whiteFraction",fraction},{"doubleBuffer",doubleBuffer!=GL_FALSE},{"drawBuffer",drawBuffer},{"readBuffer",readBuffer}});
    QueryPerformanceCounter(&sample.swapStart);
}
}

/** Observe every MENU/GAME submission while real selection and Resume messages change ownership. */
void desktopPresentProbe(void* menu, const std::filesystem::path& output) {
    desktop_present_probe::State samples; samples.output = output;
    desktop_present_probe::state = &samples; displayPresentObserver = &desktop_present_probe::observe;
    // Release the stack observer even when a native assertion aborts this bounded scenario.
    struct ObserverScope { ~ObserverScope() { displayPresentObserver = nullptr; desktop_present_probe::state = nullptr; } } observerScope;
    framePacer = std::make_unique<HostFramePacer>();
    const auto game = [&]() { return *reinterpret_cast<void**>(static_cast<unsigned char*>(menu)+0x15c); };
    const auto showing = [&]() { return *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu)+4)!=0; };
    const auto tick = [&](int milliseconds) {
        const ULONGLONG end = GetTickCount64()+milliseconds;
        do {
            samples.beganShowing = showing();
            if (game() && !showing()) {
                reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"))(game());
                reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"))(game());
            } else {
                updateAndRenderMenuPage(menu);
            }
            original_audio::update(); presentDisplay();
            MSG message{}; while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            finishHostFrame();
        } while (GetTickCount64()<end);
    };
    const auto key = [&](int value) { SendMessageW(windowHandle,WM_KEYDOWN,value,0); SendMessageW(windowHandle,WM_KEYUP,value,0); };
    const auto click = [&](float x,float y) {
        const auto p = hostDisplay.clientPoint(static_cast<int>(x),static_cast<int>(y));
        SendMessageW(windowHandle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
        SendMessageW(windowHandle,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));
    };
    tick(1000);
    nlohmann::json starts = nlohmann::json::array();
    for (const char* wanted : {"PlainPlane", "Eweton"}) {
        click(45,210); tick(1000);
        void* map = desktop_map::state(); if (!map) throw std::runtime_error("Presentation probe did not reach selector");
        bool selected = false;
        for (int attempt = 0; attempt < 120; ++attempt) {
            const auto list = desktop_map::entries(map);
            const auto& ref = list[desktop_map::selection(map,list)];
            if (wanted == std::string(ref.entry->name)) { selected = true; break; }
            key(VK_RIGHT); tick(20);
        }
        if (!selected) throw std::runtime_error("Presentation probe map not found");
        key(VK_RETURN); tick(2500);
        if (!game() || showing() || playerState(game()).at("map") != wanted) throw std::runtime_error("Presentation probe did not start selected map");
        starts.push_back(wanted);
        key(VK_ESCAPE); tick(1000);
        if (!showing()) throw std::runtime_error("Presentation probe did not return to menu");
        click(45,262); tick(1500);
        if (showing()) throw std::runtime_error("Presentation probe did not resume");
        key(VK_ESCAPE); tick(1000);
    }
    std::ofstream trace(output/"present-frames.json"); trace << samples.frames.dump(2); trace.close();
    const nlohmann::json result{{"frames",samples.frames.size()},{"whiteFrames",samples.whiteFrames},
        {"solidWhiteFrames",samples.solidWhiteFrames},{"whiteHandoffs",samples.whiteHandoffs},
        {"handoffs",samples.handoffs},{"failedSwaps",samples.failedSwaps},{"starts",starts},
        {"fps",framePacer->targetFPS()},{"glError",glGetError()}};
    std::ofstream(output/"present-checks.json") << result.dump(2);
    log("DESKTOP_PRESENT_DONE " + result.dump());
    if (samples.solidWhiteFrames || samples.whiteHandoffs || samples.failedSwaps || samples.handoffs < 4 || result.at("glError")!=GL_NO_ERROR)
        throw std::runtime_error("Presentation probe found an invalid submitted frame or missing handoff");
}

/** Measure real wheel-driven camera movement without screenshot work inside the sampled frames. */
void desktopMotionProbe(void* menu, const std::filesystem::path& output) {
    framePacer = std::make_unique<HostFramePacer>();
    const auto game = [&]() { return *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c); };
    const auto showing = [&]() { return *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4) != 0; };
    const auto now = []() {
        LARGE_INTEGER value{}, frequency{}; QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
        return static_cast<double>(value.QuadPart) / frequency.QuadPart;
    };
    const auto frame = [&]() {
        if (game() && !showing()) {
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"))(game());
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"))(game());
        } else {
            updateAndRenderMenuPage(menu);
        }
        presentDisplay();
        MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        finishHostFrame();
    };
    const auto tick = [&](double seconds) { const double end = now() + seconds; do { frame(); } while (now() < end); };
    const auto click = [&](float x, float y) {
        const auto point = hostDisplay.clientPoint(static_cast<int>(x), static_cast<int>(y));
        SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
    };
    tick(1.0); click(45, 210); tick(1.5);
    if (!desktop_map::state()) throw std::runtime_error("Motion probe did not reach native selector");
    SendMessageW(windowHandle, WM_KEYDOWN, VK_RETURN, 0); tick(3.0);
    for (int attempt = 0; attempt < 18 && game() && *reinterpret_cast<int*>(static_cast<unsigned char*>(game()) + 0xa38); ++attempt) {
        click(LogicalWidth / 2.0f, 635); tick(2.0);
    }
    if (!game() || showing() || *reinterpret_cast<int*>(static_cast<unsigned char*>(game()) + 0xa38))
        throw std::runtime_error("Motion probe did not reach playable battlefield");
    tick(2.0);
    nlohmann::json results = nlohmann::json::array();
    for (int fps : {60, 120}) {
        framePacer->configure(fps); desktop_controls::resetCamera(game()); tick(0.5);
        POINT center = hostDisplay.clientPoint(LogicalWidth / 2, LogicalHeight / 2); ClientToScreen(windowHandle, &center);
        auto* camera = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(game()) + 0xa00);
        std::ofstream trace(output / ("zoom-" + std::to_string(fps) + ".csv"));
        trace << "seconds,frameMs,altitude,pending,deltaAltitude\n";
        const double start = now(); double previous = start, nextImpulse = 0;
        float altitude = *reinterpret_cast<float*>(camera + 0x110);
        int frames = 0, held = 0, moving = 0, impulses = 0; double maximumFrameMs = 0, totalTravel = 0;
        while (now() - start < 5.0) {
            const double elapsed = now() - start;
            if (elapsed >= nextImpulse && elapsed < 4.0) {
                // Alternate short scroll bursts so the original camera never rests at a zoom boundary.
                const int direction = static_cast<int>(elapsed / 0.5) % 2 ? -1 : 1;
                SendMessageW(windowHandle, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(direction * WHEEL_DELTA)), MAKELPARAM(center.x, center.y));
                nextImpulse += 0.125; ++impulses;
            }
            const bool pending = std::abs(desktop_controls::zoomPending) > 0.02f;
            frame();
            const double time = now(), frameMs = (time - previous) * 1000; previous = time;
            const float value = *reinterpret_cast<float*>(camera + 0x110), delta = value - altitude; altitude = value;
            if (pending) { ++moving; if (std::abs(delta) < 0.000001f) ++held; }
            maximumFrameMs = std::max(maximumFrameMs, frameMs); totalTravel += std::abs(delta); ++frames;
            trace << time - start << ',' << frameMs << ',' << value << ',' << desktop_controls::zoomPending << ',' << delta << '\n';
        }
        results.push_back({{"fps",fps},{"frames",frames},{"impulses",impulses},{"movingFrames",moving},{"heldFrames",held},
            {"maximumFrameMs",maximumFrameMs},{"totalAltitudeTravel",totalTravel},{"settled",desktop_controls::zoomPending==0}});
        if (totalTravel < 0.1 || glGetError() != GL_NO_ERROR) throw std::runtime_error("Wheel camera or GL state check failed");
    }
    capture(output / "zoom-finished.png", LogicalWidth, LogicalHeight);
    std::ofstream(output / "motion-checks.json") << results.dump(2);
    log("DESKTOP_MOTION_DONE " + results.dump());
}

/** Verify desktop pages, menu navigation, mouse gestures, camera, pause, and original checkpoint recovery. */
void desktopRefineProbe(void* menu,const std::filesystem::path& output) {
    if (original_audio::enabled) throw std::runtime_error("Desktop verification requires silent audio");
    framePacer=std::make_unique<HostFramePacer>();
    nlohmann::json checks=nlohmann::json::array();
    const auto check=[&](bool passed,const char* name) {
        checks.push_back({{"name",name},{"passed",passed}});
        std::ofstream(output/"desktop-checks.json")<<checks.dump(2);
        if(!passed) throw std::runtime_error(std::string("Desktop verification failed: ")+name);
        log(std::string("DESKTOP_CHECK_PASS ")+name);
    };
    const auto game=[&]() { return *reinterpret_cast<void**>(static_cast<unsigned char*>(menu)+0x15c); };
    const auto showing=[&]() { return *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu)+4)!=0; };
    // Observe every presented tutorial frame and its handoff, without suppressing or replacing any frame.
    std::ofstream transitionTrace(output/"tutorial-frames.csv");
    transitionTrace << "sample,tutorial,whiteFraction,meanBrightness,altitude\n";
    bool sawTutorial=false;
    int transitionFrames=0, transitionSamples=0;
    double maximumTransitionWhite=0;
    const auto tick=[&](int milliseconds) {
        const ULONGLONG end=GetTickCount64()+milliseconds;
        do {
            if(pendingRestore) { pendingRestore=false; restoreSavedGame(menu,pendingRestorePath,pendingRestoreTag); }
            if(game()&&!showing()) {
                if (!desktop_controls::isPaused(game()))
                    reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"))(game());
                reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"))(game());
                const bool tutorial=*reinterpret_cast<int*>(static_cast<unsigned char*>(game())+0xa38)!=0;
                if (tutorial) sawTutorial=true;
                if (sawTutorial && transitionFrames<120) {
                    std::vector<unsigned char> pixels(static_cast<std::size_t>(hostDisplay.viewportWidth)*hostDisplay.viewportHeight*3);
                    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT); glPixelStorei(GL_PACK_ALIGNMENT,1);
                    glReadPixels(hostDisplay.x,hostDisplay.y,hostDisplay.viewportWidth,hostDisplay.viewportHeight,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
                    glPopClientAttrib();
                    std::size_t white=0; double brightness=0;
                    for (std::size_t at=0;at<pixels.size();at+=3) {
                        white+=pixels[at]>240 && pixels[at+1]>240 && pixels[at+2]>240;
                        brightness+=(pixels[at]+pixels[at+1]+pixels[at+2])/765.0;
                    }
                    const double fraction=white/static_cast<double>(pixels.size()/3);
                    const auto* camera=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(game())+0xa00);
                    transitionTrace << transitionSamples++ << ',' << tutorial << ',' << fraction << ','
                        << brightness/(pixels.size()/3) << ',' << *reinterpret_cast<const float*>(camera+0x110) << '\n';
                    maximumTransitionWhite=std::max(maximumTransitionWhite,fraction);
                    if(!tutorial) {
                        if(transitionFrames<3) capture(output/("tutorial-handoff-"+std::to_string(transitionFrames)+".png"),LogicalWidth,LogicalHeight);
                        ++transitionFrames;
                    }
                }
            } else {
                updateAndRenderMenuPage(menu);
            }
            original_audio::update(); presentDisplay();
            MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            finishHostFrame();
        } while(GetTickCount64()<end);
    };
    const auto key=[&](int value) { SendMessageW(windowHandle,WM_KEYDOWN,value,0); SendMessageW(windowHandle,WM_KEYUP,value,0); };
    const auto click=[&](float x,float y) {
        const auto p=hostDisplay.clientPoint(static_cast<int>(std::lround(x)),static_cast<int>(std::lround(y)));
        SendMessageW(windowHandle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
        SendMessageW(windowHandle,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y)); tick(80);
    };
    const auto shot=[&](const char* name) {
        // Read the complete currently displayed desktop frame, including modal overlays.
        if(game()&&!showing()) reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"))(game());
        else reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_render"))(menu);
        renderDesktopOverlay(); glFinish(); capture(output/name,LogicalWidth,LogicalHeight);
    };
    const auto uiButton=[&](int action) {
        for(const auto& button:desktop_ui::buttons) if(button.action==action) { click(button.x+button.w/2,button.y+button.h/2); return true; }
        return false;
    };
    const auto mapButton=[&](int action) {
        for(const auto& button:desktop_map::buttons) if(button.action==action) { click(button.x+button.w/2,button.y+button.h/2); return true; }
        return false;
    };
    tick(1600); shot("desktop-main.png");
    const auto pointer=[&](UINT message,float x,float y) {
        const auto point=hostDisplay.clientPoint(static_cast<int>(x),static_cast<int>(y));
        SendMessageW(windowHandle,message,message==WM_LBUTTONUP?0:MK_LBUTTON,MAKELPARAM(point.x,point.y));
    };
    void* firstPage=*static_cast<void**>(menu);
    pointer(WM_LBUTTONDOWN,45,210); tick(80);
    check(landscapePointerHeld&&heldLandscapeTab==0,"original sidebar pressed state"); shot("desktop-main-pressed.png");
    pointer(WM_MOUSEMOVE,220,210); pointer(WM_LBUTTONUP,45,210); tick(120);
    check(!landscapePointerHeld&&*static_cast<void**>(menu)==firstPage,"original sidebar drag-out cancels activation");
    check(GetClassLongPtrW(windowHandle,GCLP_HICON)!=0,"original icon loaded by Windows");
    check(desktop_controls::cursor()!=LoadCursorA(nullptr,IDC_ARROW),"custom game cursor created");
    key(VK_F10); tick(150); check(desktop_ui::settings,"settings opens"); shot("desktop-settings.png");
    for(const auto& button:desktop_ui::buttons) if(button.action==25) {
        const float before=desktop_controls::hudScale;
        pointer(WM_LBUTTONDOWN,button.x+button.w/2,button.y+button.h/2); tick(80);
        check(desktop_ui::pressed==25,"settings button held feedback"); shot("desktop-settings-pressed.png");
        pointer(WM_MOUSEMOVE,0,0); pointer(WM_LBUTTONUP,button.x+button.w/2,button.y+button.h/2); tick(80);
        check(desktop_controls::hudScale==before&&desktop_ui::pressed<0,"settings dragged click cancels"); break;
    }
    for(int i=0;i<20;++i) check(uiButton(25),"HUD increase button");
    check(std::abs(desktop_controls::hudScale-3.0f)<0.001f,"HUD reaches 300 percent");
    desktop_controls::saveSettings(); desktop_controls::hudScale=1.2f;
    desktop_controls::loadSettings(dataDirectory);
    check(std::abs(desktop_controls::hudScale-3.0f)<0.001f,"HUD 300 percent persists");
    shot("desktop-settings-hud300.png");
    for(int i=0;i<18;++i) uiButton(24);
    check(uiButton(5),"key tab button"); tick(60);
    check(uiButton(100+desktop_controls::Upgrade),"binding button"); key('U');
    check(desktop_controls::keys[desktop_controls::Upgrade]=='U',"custom key assigned");
    check(uiButton(100+desktop_controls::Upgrade),"rebind button"); key('W');
    check(desktop_controls::keys[desktop_controls::Upgrade]=='W',"custom key restored");
    key(VK_F10); tick(100); check(!desktop_ui::settings,"settings closes");
    key(VK_F1); tick(100); check(desktop_guide::visible,"in-game guide opens"); shot("desktop-guide.png");
    key(VK_ESCAPE); tick(80); check(!desktop_guide::visible,"guide closes");
    click(menuLogoOffset()+150,55); check(desktop_ui::achievements,"original trophy opens local achievements");
    shot("desktop-achievements.png"); key(VK_ESCAPE);
    const auto originalBounds=[&]() { RECT r{}; GetWindowRect(windowHandle,&r); return r; };
    const RECT beforeFullscreen=originalBounds(); key(VK_F11); tick(100);
    check((GetWindowLongPtrW(windowHandle,GWL_STYLE)&WS_OVERLAPPEDWINDOW)==0,"borderless fullscreen");
    key(VK_F11); tick(100); const RECT restoredBounds=originalBounds();
    check(EqualRect(&beforeFullscreen,&restoredBounds)!=FALSE,"fullscreen restores placement");
    click(45,210); tick(2000);
    check(*static_cast<void**>(menu)==findExport("startGamePage"),"native map page opens");
    shot("desktop-map.png");
    void* map=desktop_map::state(); check(map!=nullptr,"original selector live");
    const int firstSelected=desktop_map::word(map,10);
    const auto mapIndex=[&]() {return desktop_map::selection(map,desktop_map::entries(map));};
    const int firstMap=mapIndex(), mapCount=static_cast<int>(desktop_map::entries(map).size());
    const auto wheel=[&](int delta) {
        POINT center=hostDisplay.clientPoint(LogicalWidth/2,LogicalHeight/2); ClientToScreen(windowHandle,&center);
        SendMessageW(windowHandle,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),MAKELPARAM(center.x,center.y));
    };
    for(int i=0;i<3;++i) wheel(30);
    check(mapIndex()==firstMap,"fractional map wheel waits for complete notch"); wheel(30);
    check(mapIndex()==(firstMap-1+mapCount)%mapCount,"wheel up selects previous map");
    wheel(-360); check(mapIndex()==(firstMap+2)%mapCount,"multi-notch wheel down selects every map");
    wheel(240); tick(700); check(mapIndex()==firstMap,"reverse wheel restores exact selected map");
    wheel(-120); const double initialSlide=desktop_map::slide; tick(80);
    check(initialSlide>0&&desktop_map::slide>0&&desktop_map::slide<initialSlide,"map transition advances smoothly");
    shot("desktop-map-slide-80.png"); tick(100); shot("desktop-map-slide-180.png"); tick(520);
    check(desktop_map::slide==0,"map transition settles to native preview"); shot("desktop-map-settled.png");
    wheel(120); tick(700);
    for(int i=0;i<8;++i) wheel(-120);
    check(mapIndex()==(firstMap+8)%mapCount,"continuous wheel consumes all eight events");
    tick(80); wheel(960); tick(700); check(mapIndex()==firstMap,"fast wheel reversal keeps native selection");
    for(const auto& button:desktop_map::buttons) if(button.action==2&&button.label==">") {
        const float x=button.x+button.w/2,y=button.y+button.h/2;
        pointer(WM_LBUTTONDOWN,x,y); tick(100);
        check(desktop_map::pressed&&desktop_map::pressedAction==2,"map arrow pressed feedback"); shot("desktop-map-pressed.png");
        pointer(WM_MOUSEMOVE,0,0); pointer(WM_MOUSEMOVE,x,y); pointer(WM_LBUTTONUP,x,y); tick(100);
        check(mapIndex()==firstMap&&!desktop_map::pressed,"map arrow drag-out cancels even after returning"); break;
    }
    // Rebind through the actual settings UI; fixed M/Space shortcuts must not override navigation.
    key(VK_F10); tick(100);
    check(uiButton(100+desktop_controls::PreviousMap),"previous map binding button"); key('M');
    check(uiButton(100+desktop_controls::NextMap),"next map binding button"); key(VK_SPACE);
    check(desktop_controls::keys[desktop_controls::PreviousMap]=='M'
        &&desktop_controls::keys[desktop_controls::NextMap]==VK_SPACE,"map M and Space bindings assigned");
    key(VK_F10); tick(100);
    key(VK_SPACE); tick(100);
    check(desktop_map::word(map,10)!=firstSelected&&!desktop_map::overview&&!game()
        &&!desktop_map::word(map,8),"bound Space selects next map without starting");
    key('M'); tick(100);
    check(desktop_map::word(map,10)==firstSelected&&!desktop_map::overview,"bound M selects previous map without overview");
    key(VK_F10); tick(100);
    check(uiButton(100+desktop_controls::PreviousMap),"restore previous map binding button"); key('A');
    check(uiButton(100+desktop_controls::NextMap),"restore next map binding button"); key('D');
    check(desktop_controls::keys[desktop_controls::PreviousMap]=='A'
        &&desktop_controls::keys[desktop_controls::NextMap]=='D'
        &&desktop_controls::keys[desktop_controls::Pause]==VK_SPACE,"map and swapped pause bindings restored");
    key(VK_F10); tick(100);
    key('D'); tick(100);
    check(desktop_map::word(map,10)!=firstSelected&&!desktop_map::overview&&!game(),"restored D selects next map");
    key('A'); tick(100);
    check(desktop_map::word(map,10)==firstSelected&&!desktop_map::overview&&!game(),"restored A and D navigate without starting");
    key(VK_RIGHT); tick(100); check(desktop_map::word(map,10)!=firstSelected||desktop_map::word(map,0xc),"keyboard next map");
    key(VK_LEFT); tick(100);
    check(mapButton(3),"map overview button"); tick(1800);
    check(desktop_map::overview,"overview visible"); shot("desktop-overview.png");
    check(desktop_map::entries(map).size()>=112,"overview includes original catalog");
    check(mapButton(21),"campaign filter"); tick(120);
    check(desktop_map::entries(map).size()==4,"four original campaign maps");
    check(mapButton(1000),"select first campaign thumbnail"); tick(150);
    check(!desktop_map::overview,"thumbnail returns selected preview");
    key(VK_SPACE); tick(3000);
    check(game()!=nullptr,"original game starts from desktop selector");
    // Dismiss the original tutorial by its real touch handler, without changing account flags.
    for(int i=0;i<16&&*reinterpret_cast<int*>(static_cast<unsigned char*>(game())+0xa38);++i) {
        click(LogicalWidth/2.0f,635); tick(2500);
    }
    check(!*reinterpret_cast<int*>(static_cast<unsigned char*>(game())+0xa38),"original tutorial dismissed");
    tick(800); shot("desktop-game.png");
    check(transitionFrames>10&&maximumTransitionWhite<0.85,"tutorial handoff has no full white frame");
    auto* bytes=static_cast<unsigned char*>(game());
    void* camera=*reinterpret_cast<void**>(bytes+0xa00);
    const float initialAltitude=*reinterpret_cast<float*>(static_cast<unsigned char*>(camera)+0x110);
    check(initialAltitude<49,"closer initial camera");
    key('N'); tick(500); check(playerState(game()).at("gameStarted").get<bool>(),"N starts original next wave");
    const int oldClock=currentGameClock(menu); key(VK_SPACE); tick(1600); const int pausedClock=currentGameClock(menu); tick(350);
    check(!showing()&&desktop_controls::isPaused(game())&&currentGameClock(menu)==pausedClock,"Space pauses battlefield and original clock");
    shot("desktop-paused.png");
    // GAME_resume retains the original three-second countdown before advancing battle time.
    key(VK_SPACE); tick(4200); check(!showing()&&currentGameClock(menu)>oldClock,"Space resumes original clock");
    key(VK_SPACE); key(VK_ESCAPE); tick(500);
    check(showing()&&!desktop_controls::isPaused(game()),"Escape transfers pause to original menu");
    key(VK_SPACE); tick(4200);
    check(!showing()&&!desktop_controls::isPaused(game()),"original menu resumes a previously paused battle");
    desktop_controls::hudScale=3.0f; tick(80); shot("desktop-hud300.png");
    desktop_controls::hudScale=1.2f;
    const float originalAngle=*reinterpret_cast<float*>(bytes+0xa9c); key('E'); tick(120);
    check(*reinterpret_cast<float*>(bytes+0xa9c)>originalAngle,"camera rotation"); key('R'); tick(100);
    // Crossing both old yaw bounds must preserve the actual camera orbit, then wrap a complete turn.
    for(int i=0;i<22;++i) key('E');
    tick(120);
    const float fullYaw=*reinterpret_cast<float*>(bytes+0xa9c);
    check(fullYaw < -1.5f,"camera crosses 180 degrees");
    key(VK_F5); tick(80); key('R'); key(VK_F9); tick(250);
    bytes=static_cast<unsigned char*>(game()); camera=*reinterpret_cast<void**>(bytes+0xa00);
    check(std::abs(*reinterpret_cast<float*>(bytes+0xa9c)-fullYaw)<0.001f,"checkpoint preserves orbit beyond 180 degrees");
    key('R'); tick(100);
    auto middle=hostDisplay.clientPoint(LogicalWidth/2,LogicalHeight/2);
    SendMessageW(windowHandle,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(middle.x,middle.y));
    SendMessageW(windowHandle,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(middle.x+80,middle.y));
    SendMessageW(windowHandle,WM_MBUTTONUP,0,MAKELPARAM(middle.x+80,middle.y)); tick(100);
    check(*reinterpret_cast<float*>(bytes+0xa9c)<0,"rightward middle drag follows requested rotation");
    key('R'); tick(100);
    POINT center=hostDisplay.clientPoint(LogicalWidth/2,LogicalHeight/2); ClientToScreen(windowHandle,&center);
    SendMessageW(windowHandle,WM_MOUSEWHEEL,MAKEWPARAM(0,120),MAKELPARAM(center.x,center.y));
    const float impulse=desktop_controls::zoomPending; tick(500);
    check(impulse>0&&std::abs(desktop_controls::zoomPending)<impulse*0.02f,"wheel inertia settles");
    key('R'); tick(150);
    // Probe valid cells through the original projection; no hand-authored terrain or money changes.
    void* towers=*reinterpret_cast<void**>(bytes+0x10);
    bool tapped=false;
    int selectedX=0,selectedY=0;
    for(int cy=4;cy<27&&!tapped;++cy) for(int cx=3;cx<19&&!tapped;++cx) {
        int reason=0;
        if(reinterpret_cast<int(__cdecl*)(void*,int,int,int*,int)>(requireExport("TOWERS_canBuildHere"))(towers,cx,cy,&reason,0)!=0) continue;
        // Original projection writes homogeneous x/y/z/w, even though the pointer uses only x/y.
        float world[4]={static_cast<float>(cx+1),static_cast<float>(cy+1),0,1}, screen[4]{};
        reinterpret_cast<void(__cdecl*)(void*,float*,float*)>(requireExport("CAMERA_worldToScreen"))(camera,world,screen);
        if(screen[0]<100||screen[0]>LogicalWidth-100||screen[1]<120||screen[1]>LogicalHeight-120) continue;
        const auto p=hostDisplay.clientPoint(static_cast<int>(screen[0]),static_cast<int>(screen[1]));
        SendMessageW(windowHandle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
        SendMessageW(windowHandle,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(p.x+2,p.y+1));
        check(*reinterpret_cast<int*>(bytes+0xb38)==0,"jitter not dispatched as drag");
        SendMessageW(windowHandle,WM_LBUTTONUP,0,MAKELPARAM(p.x+2,p.y+1)); tick(250);
        void* pie=*reinterpret_cast<void**>(bytes+0xa48);
        tapped=reinterpret_cast<int(__cdecl*)(void*)>(requireExport("PIEMENU_isShowing"))(pie)!=0;
        if(tapped) { selectedX=cx; selectedY=cy; }
    }
    check(tapped,"jitter click opens native build wheel"); shot("desktop-wheel.png");
    SendMessageW(windowHandle,WM_RBUTTONDOWN,0,0); tick(120);
    check(!reinterpret_cast<int(__cdecl*)(void*)>(requireExport("PIEMENU_isShowing"))(*reinterpret_cast<void**>(bytes+0xa48)),"right click closes wheel");
    const auto selectCell=[&]() {
        float world[4]={static_cast<float>(selectedX+1),static_cast<float>(selectedY+1),0,1},screen[4]{};
        reinterpret_cast<void(__cdecl*)(void*,float*,float*)>(requireExport("CAMERA_worldToScreen"))(camera,world,screen);
        click(screen[0],screen[1]); tick(600);
    };
    const auto optionClick=[&](int kind,int type) {
        auto* pie=*reinterpret_cast<int**>(bytes+0xa48);
        auto* node=*reinterpret_cast<std::uint32_t**>(pie[1]);
        for(;node;node=reinterpret_cast<std::uint32_t*>(node[1])) {
            auto* option=reinterpret_cast<float*>(node[0]);
            if(*reinterpret_cast<int*>(option+10)!=kind || (type>=0&&*reinterpret_cast<int*>(option+9)!=type)) continue;
            float center[2]{};
            reinterpret_cast<void(__cdecl*)(float*,void*)>(requireExport("PIEMENU_getScreenPosition"))(center,pie);
            click(center[0]+option[0],center[1]+option[1]); tick(500); return true;
        }
        return false;
    };
    selectCell(); check(optionClick(2,-1),"native energy category clicked");
    void* mode=*reinterpret_cast<void**>(bytes);
    const int plasma=nativeTowerId(mode,"Plasmatron I"),plasmaUpgrade=nativeTowerId(mode,"Plasmatron II");
    check(optionClick(1,plasma),"native tower build option clicked"); tick(1600);
    const int towerId=reinterpret_cast<int(__cdecl*)(void*,int,int)>(requireExport("TOWERS_getIdForCell"))(towers,selectedX,selectedY);
    check(towerId>=0,"original tower built through mouse");
    selectCell(); key('W'); tick(1600);
    auto* tower=reinterpret_cast<int*(__cdecl*)(void*,int)>(requireExport("TOWERS_getTowerForId"))(towers,towerId);
    check(tower&&tower[1]==plasmaUpgrade,"W upgrades through original wheel");
    // Confirm a real drag still reaches the native camera rather than being swallowed as jitter.
    auto p=hostDisplay.clientPoint(LogicalWidth/2,LogicalHeight/2);
    SendMessageW(windowHandle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
    SendMessageW(windowHandle,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(p.x+50,p.y+35));
    check(*reinterpret_cast<int*>(bytes+0xb38)!=0,"real drag reaches native handler");
    SendMessageW(windowHandle,WM_LBUTTONUP,0,MAKELPARAM(p.x+50,p.y+35)); tick(100);
    key(VK_F5); tick(100); check(std::filesystem::exists(dataDirectory/"menu-save-hotkey.bin"),"manual checkpoint committed");
    const auto state=playerState(game());
    const float savedAltitude=*reinterpret_cast<float*>(static_cast<unsigned char*>(camera)+0x110);
    key(VK_F9); tick(180);
    check(game()!=nullptr&&playerState(game()).at("money")==state.at("money"),"manual checkpoint restores original money");
    const auto* restoredCamera=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(game())+0xa00);
    check(std::abs(*reinterpret_cast<const float*>(restoredCamera+0x110)-savedAltitude)<0.0001f,"restore preserves saved camera");
    check(GetTickCount64()<desktop_save::toast.until,"save restore feedback visible");
    shot("desktop-restore.png");
    const auto manualPath=dataDirectory/"menu-save-hotkey.bin";
    std::vector<std::uint8_t> manualBefore,manualAfter;
    check(readSaveState(manualPath,manualBefore),"manual checkpoint readable");
    HANDLE locked=CreateFileW(manualPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"checkpoint failure test lock");
    key(VK_F5); tick(80);
    check(!desktop_save::toast.success,"save failure feedback");
    check(readSaveState(manualPath,manualAfter)&&manualBefore==manualAfter,"failed save preserves previous bytes");
    CloseHandle(locked);
    for(int i=0;i<7;++i) { key(VK_F5); tick(40); }
    std::size_t backupCount=0;
    for(const auto& entry:std::filesystem::directory_iterator(dataDirectory))
        if(entry.path().filename().wstring().starts_with(L"menu-save-hotkey.bin.backup-")) {
            std::vector<std::uint8_t> backupBytes; check(readSaveState(entry.path(),backupBytes),"backup readable"); ++backupCount;
        }
    check(backupCount==5,"five recent manual backups retained");
    readSaveState(manualPath,manualBefore);
    key(VK_F10); tick(100); check(uiButton(4),"general tab");
    check(uiButton(11)&&desktop_save::autosaveEnabled,"optional autosave UI enables");
    key(VK_F10); key(VK_SPACE); tick(4200);
    desktop_save::nextAutosave=GetTickCount64()-1; updateDesktopSave();
    check(std::filesystem::exists(dataDirectory/"menu-save-auto.bin"),"separate autosave committed");
    check(readSaveState(manualPath,manualAfter)&&manualBefore==manualAfter,"autosave preserves manual checkpoint");
    key(VK_F10); tick(100); check(uiButton(14),"autosave restore button"); tick(500);
    check(game()!=nullptr&&!desktop_ui::settings,"autosave restores original game");
    key(VK_F10); tick(100); check(uiButton(11)&&!desktop_save::autosaveEnabled,"optional autosave UI disables");
    key(VK_F10); key(VK_SPACE); tick(4200);
    key('E'); tick(300); key(VK_F5); tick(100);
    const float savedYaw=*reinterpret_cast<float*>(static_cast<unsigned char*>(game())+0xa9c);
    key('R'); tick(100); key(VK_F9); tick(500);
    check(std::abs(*reinterpret_cast<float*>(static_cast<unsigned char*>(game())+0xa9c)-savedYaw)<0.001f,"save restores desktop orbit");
    check(desktop_launch::exportDiagnostics(false),"diagnostics export writes actual files");
    // Check the user's near-square client and a narrow client in the complete integrated renderer.
    for(const auto size:std::array<POINT,3>{{{1769,1460},{360,600},{1920,1080}}}) {
        RECT bounds{0,0,size.x,size.y}; AdjustWindowRect(&bounds,static_cast<DWORD>(GetWindowLongPtrW(windowHandle,GWL_STYLE)),FALSE);
        SetWindowPos(windowHandle,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER);
        tick(180);
        check(hostDisplay.width==size.x&&hostDisplay.height==size.y,"client resize exact");
        const std::string name="desktop-resize-"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png";
        shot(name.c_str());
        if(size.x==360) {
            key(VK_F10); tick(120); shot("desktop-settings-narrow.png"); key(VK_F10);
            key(VK_F1); tick(100); shot("desktop-guide-narrow.png"); key(VK_ESCAPE);
            key(VK_SPACE); tick(4200);
        }
    }
    key('R'); tick(350);
    // Camera placements are legal original-camera coordinates; all wheel openings still use real Windows clicks.
    for(int edge=0;edge<2;++edge) {
        bool verified=false;
        void* current=game(); auto* currentBytes=static_cast<unsigned char*>(current);
        void* currentCamera=*reinterpret_cast<void**>(currentBytes+0xa00);
        void* currentTowers=*reinterpret_cast<void**>(currentBytes+0x10);
        for(float altitude:{20.0f,12.0f,6.0f}) for(float targetY:{8.0f,16.0f,24.0f}) {
            if(verified) continue;
            reinterpret_cast<void(__cdecl*)(void*,float,float,float)>(requireExport("CAMERA_lookAt"))(currentCamera,11,targetY,0);
            reinterpret_cast<void(__cdecl*)(void*,float,float,float)>(requireExport("CAMERA_setPosition"))(currentCamera,11,targetY+5,altitude);
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_makeSureCameraIsGood"))(current); tick(60);
            for(int y=3;y<29&&!verified;++y) for(int x=3;x<19&&!verified;++x) {
                int reason=0;
                if(reinterpret_cast<int(__cdecl*)(void*,int,int,int*,int)>(requireExport("TOWERS_canBuildHere"))(currentTowers,x,y,&reason,0)!=0) continue;
                float world[4]={static_cast<float>(x+1),static_cast<float>(y+1),0,1},screen[4]{};
                reinterpret_cast<void(__cdecl*)(void*,float*,float*)>(requireExport("CAMERA_worldToScreen"))(currentCamera,world,screen);
                const bool vertical=edge?screen[1]>LogicalHeight-100&&screen[1]<LogicalHeight-24:screen[1]>24&&screen[1]<100;
                if(!vertical||screen[0]<120||screen[0]>LogicalWidth-120) continue;
                click(screen[0],screen[1]); tick(1400);
                void* pie=*reinterpret_cast<void**>(currentBytes+0xa48);
                if(!reinterpret_cast<int(__cdecl*)(void*)>(requireExport("PIEMENU_isShowing"))(pie)) continue;
                check(!reinterpret_cast<int(__cdecl*)(void*)>(requireExport("PIEMENU_isMostlyOffScreen"))(pie),edge?"bottom wheel remains visible":"top wheel remains visible");
                shot(edge?"desktop-wheel-bottom.png":"desktop-wheel-top.png");
                SendMessageW(windowHandle,WM_RBUTTONDOWN,0,0); tick(100); verified=true;
            }
        }
        check(verified,edge?"bottom edge wheel exercised":"top edge wheel exercised");
    }
    check(glGetError()==GL_NO_ERROR,"desktop GL state clean");
    log("DESKTOP_CHECK_DONE count="+std::to_string(checks.size()));
}
