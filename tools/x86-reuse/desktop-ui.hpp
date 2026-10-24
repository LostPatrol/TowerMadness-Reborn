// Extend the original menu with desktop controls using its green side-tab artwork.
#pragma once

namespace desktop_ui {
// Desktop pages pause through the original menu; closing them leaves a deliberate Resume action.
bool settings = false;
int tab = 0, binding = -1, pressed = -1;
// A drag outside the original pressed control cancels this click until release.
bool pressCancelled = false;
// Local achievements retain original conditions and account progress without an online capability claim.
bool achievements = false;
int achievementPage = 0;
// Hit rectangles are rebuilt from the same layout used to draw each control.
struct Button { float x,y,w,h; int action; };
std::vector<Button> buttons;
// Action names correspond exactly to desktop_controls::Action.
constexpr const char* ActionNames[] = {"Pause / resume", "Send next wave", "Upgrade selected tower", "Rotate left", "Rotate right", "Reset camera", "Previous map", "Next map"};

/** Clear a held desktop control before changing page or losing capture. */
void cancelPress(HWND window) {
    const bool captured = pressed >= 0;
    pressed = -1; pressCancelled = false;
    if (captured && GetCapture() == window) ReleaseCapture();
}

/** Pause a running original game before opening an interaction-heavy desktop page. */
void pauseForPage() {
    if (desktop_controls::game(true) && originalMenu) desktop_controls::togglePause();
    desktop_controls::zoomPending = 0;
}

/** Draw a button and retain its matching hit rectangle in logical coordinates. */
void button(float x,float y,float w,float h,const std::string& label,int action) {
    buttons.push_back({x,y,w,h,action});
    desktop_skin::button(x,y,w,h,false,pressed==action&&!pressCancelled);
    desktop_map::buttonLabel(x,y,w,h,label);
}

/** Use Windows' localized key names so the binding list reflects the keyboard layout. */
std::string keyName(int key) {
    if (key == VK_SPACE) return "Space";
    const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    char text[64]{};
    if (GetKeyNameTextA(scan << 16, text, sizeof(text))) return text;
    return "Key " + std::to_string(key);
}

/** Apply only host presentation/input settings and their independent persistence. */
void activate(int action) {
    cancelPress(windowHandle); buttons.clear();
    if (action == 1) { pauseForPage(); settings = true; binding = -1; }
    else if (action == 2) { pauseForPage(); desktop_guide::open("guide.html"); }
    else if (action == 3) { settings = false; binding = -1; desktop_controls::saveSettings(); }
    else if (action == 4) tab = 0;
    else if (action == 5) tab = 1;
    else if (action == 6) { pauseForPage(); achievements = true; achievementPage = 0; }
    else if (action == 7) achievements = false;
    else if (action == 8) achievementPage = std::max(0,achievementPage-1);
    else if (action == 9) achievementPage = std::min(5,achievementPage+1);
    else if (action == 10) desktop_launch::toggleFullscreen();
    else if (action == 11) { desktop_save::autosaveEnabled = !desktop_save::autosaveEnabled; desktop_launch::saveSettings(); }
    else if (action == 12) desktop_launch::openSaveFolder();
    else if (action == 13) desktop_launch::exportDiagnostics();
    else if (action == 14) {
        pendingRestorePath=dataDirectory/"menu-save-auto.bin"; pendingRestoreTag="DESKTOP_LOAD_AUTOSAVE";
        pendingRestore=true; settings=false;
    }
    else if (action == 15) desktop_controls::toggleInGamePause();
    else if (action >= 100 && action < 100 + desktop_controls::ActionCount) binding = action - 100;
    else {
        if (action == 20) desktop_controls::dragSensitivity = std::max(0.5f, desktop_controls::dragSensitivity - 0.1f);
        if (action == 21) desktop_controls::dragSensitivity = std::min(2.0f, desktop_controls::dragSensitivity + 0.1f);
        if (action == 22) desktop_controls::wheelSensitivity = std::max(0.25f, desktop_controls::wheelSensitivity - 0.25f);
        if (action == 23) desktop_controls::wheelSensitivity = std::min(2.5f, desktop_controls::wheelSensitivity + 0.25f);
        if (action == 24) desktop_controls::hudScale = std::max(1.0f, desktop_controls::hudScale - 0.1f);
        if (action == 25) desktop_controls::hudScale = std::min(desktop_controls::MaximumHudScale, desktop_controls::hudScale + 0.1f);
        desktop_controls::saveSettings();
    }
}

/** Find the first free tab slot below the original main menu's Guide entry. */
float sidebarEntryTop() {
    if (!originalMenu || !*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu)+4)) return -1;
    const auto* page = *static_cast<unsigned char**>(originalMenu);
    bool mainPage = false;
    for (const char* name : {"mainMenuPage", "mainMenuPagePaid", "mainMenuPageZero", "mainMenuPageGameInProgress",
        "mainMenuPageGameInProgressPaid", "mainMenuPageGameInProgressZero"})
        if (page == findExport(name)) mainPage = true;
    if (!mainPage) return -1;
    int count = 0;
    while (count < 16 && *reinterpret_cast<const void* const*>(page+0x18+8*count)) ++count;
    const auto* base = images.front()->base;
    const float first = *reinterpret_cast<const float*>(base+0x362e80);
    const float pitch = *reinterpret_cast<const float*>(base+0x362e84);
    const float top = *reinterpret_cast<const float*>(base+0x362ea8);
    return first + count * pitch + top;
}

/** Continue the original sidebar's green tabs and small, left-aligned uppercase labels. */
void sidebarEntry(float y,const std::string& label,int action) {
    // Original tabs have a 48-point face and a four-point gap.
    constexpr float TabHeight = 48;
    buttons.push_back({0,y,DesktopSidebarWidth,TabHeight,action});
    desktop_skin::button(0,y,DesktopSidebarWidth,TabHeight,false,pressed==action&&!pressCancelled);
    desktop_map::text(7,y+21,label,0.82f,DesktopSidebarWidth-14);
}

/** Draw desktop controls with the original menu's artwork and white uppercase labels. */
void render() {
    buttons.clear();
    if (desktop_guide::render()) return;
    desktop_map::begin();
    const float w = static_cast<float>(LogicalWidth), h = static_cast<float>(LogicalHeight);
    if (achievements) {
        desktop_map::box(0,0,w,h,0.075f,0.105f,0.055f,0.97f);
        desktop_map::text(24,20,"LOCAL CAMPAIGN ACHIEVEMENTS");
        desktop_map::text(24,62,"Original conditions / local best scores. Online awards are unavailable.");
        button(w-130,18,106,42,"Close",7);
        const auto rows = desktop_achievements::rows();
        for (int i=achievementPage*6;i<std::min(static_cast<int>(rows.size()),achievementPage*6+6);++i) {
            const auto& row=rows[i]; const float y=116.0f+(i%6)*96;
            desktop_map::box(24,y,w-48,84,0.08f,0.15f,0.16f);
            const std::string title=row.title.substr(row.title.find_last_of('.')+1);
            desktop_map::text(36,y+8,(row.earned?"[EARNED] ":"[       ] ")+title,1,w-72);
            desktop_map::text(36,y+36,row.progress+"  /  "+row.description.substr(0,static_cast<std::size_t>(std::max(18,(LogicalWidth-280)/10))));
        }
        button(24,h-64,110,42,"Previous",8); button(w-134,h-64,110,42,"Next",9);
        desktop_map::text(w/2-60,h-55,std::to_string(achievementPage+1)+" / 6");
    } else if (!settings) {
        const float first = sidebarEntryTop();
        if (first >= 0) {
            sidebarEntry(first,"PC SETTINGS",1);
            sidebarEntry(first+52,"FIELD GUIDE",2);
        } else if (auto* game = desktop_controls::game(true); game && desktop_controls::isPaused(game)) {
            // The original game remains visible while its update loop is deliberately paused.
            const float width = std::min(360.0f,w-40), x = (w-width)/2, y = h/2-74;
            desktop_map::box(x-12,y-12,width+24,160,0.075f,0.105f,0.055f,0.93f);
            desktop_map::buttonLabel(x,y,width,46,"PAUSED");
            button(x,y+70,width,52,"RESUME  ["+keyName(desktop_controls::keys[desktop_controls::Pause])+"]",15);
        }
    } else {
        desktop_map::box(0,0,w,h,0.075f,0.105f,0.055f,0.97f);
        const float left = std::max(20.0f, (w - 760) / 2), right = w - left;
        desktop_map::text(left,20,"PC SETTINGS");
        button(left,70,150,42,"GENERAL",4); button(left+160,70,150,42,"KEY BINDINGS",5);
        button(right-100,20,100,42,"CLOSE",3);
        if (tab == 0) {
            float y = 146;
            const auto slider = [&](const std::string& label,float value,int minus,int plus) {
                std::ostringstream description; description << label << "  " << std::lround(value*100) << "%";
                desktop_map::text(left,y+9,description.str());
                button(right-108,y,46,42,"-",minus); button(right-52,y,46,42,"+",plus); y+=64;
            };
            slider("Mouse drag",desktop_controls::dragSensitivity,20,21);
            slider("Wheel zoom",desktop_controls::wheelSensitivity,22,23);
            slider("HUD size",desktop_controls::hudScale,24,25);
            button(left,y,right-left,46,"Toggle borderless fullscreen  (F11 / Alt+Enter)",10); y+=64;
            button(left,y,right-left,46,desktop_save::autosaveEnabled?"Autosave: ON  (separate file, every 2 min)":"Autosave: OFF  (manual F5 remains separate)",11); y+=64;
            button(left,y,(right-left-16)/2,46,"Open save folder",12);
            button((left+right)/2+8,y,(right-left-16)/2,46,"Export diagnostics",13);
            button(left,y+64,right-left,46,"Restore separate autosave",14);
            desktop_map::text(left,y+138,"F5 save / F9 restore / F1 guide",1,right-left);
            desktop_map::text(left,y+174,"Middle drag / Q,E rotate. R resets.",1,right-left);
        } else {
            for (int i=0;i<desktop_controls::ActionCount;++i) {
                const float y=140.0f+i*55;
                desktop_map::text(left,y+10,ActionNames[i]);
                button(right-140,y,140,44,binding==i?"Press a key...":keyName(desktop_controls::keys[i]),100+i);
            }
            desktop_map::text(left,620,"Click a binding, then press a key. Escape cancels.");
            desktop_map::text(left,657,"F1/F5/F6/F9/F10/F11 and Escape are reserved.");
        }
    }
    if (GetTickCount64() < desktop_save::toast.until) {
        const auto& toast = desktop_save::toast;
        std::string message;
        for(const auto character:toast.text) message+=character>=32&&character<127?static_cast<char>(character):'?';
        desktop_map::box(18,h-62,w-36,46,toast.success?0.10f:0.34f,0.16f,0.12f,0.98f);
        desktop_map::text(30,h-53,message.substr(0,120));
    }
    desktop_map::end();
}

/** Consume modal controls before the original game receives mouse or keyboard input. */
bool input(HWND window,UINT message,WPARAM first,LPARAM second) {
    if (message==WM_KILLFOCUS || message==WM_CANCELMODE || message==WM_CAPTURECHANGED || message==WM_SIZE) {
        cancelPress(window);
        cancelLandscapeMenuPress();
    }
    if (desktop_guide::input(window,message,first,second)) return true;
    if (message == WM_KEYDOWN && !(second & (1LL<<30))) {
        if (achievements && first==VK_ESCAPE) { activate(7); return true; }
        if (binding >= 0) {
            if (first == VK_ESCAPE) binding = -1;
            else if (first != VK_F1 && first != VK_F5 && first != VK_F6 && first != VK_F9 && first != VK_F10 && first != VK_F11
                && first != VK_RETURN && first != VK_LEFT && first != VK_RIGHT && first != VK_MENU && first != VK_CONTROL && first != VK_SHIFT) {
                int& selected = desktop_controls::keys[binding];
                for (int i=0;i<desktop_controls::ActionCount;++i)
                    if (i != binding && desktop_controls::keys[i] == static_cast<int>(first)) desktop_controls::keys[i] = selected;
                selected = static_cast<int>(first); binding=-1; desktop_controls::saveSettings();
            }
            return true;
        }
        if (first == VK_F10) { if (settings) activate(3); else activate(1); return true; }
        if (first == VK_F1) { activate(2); return true; }
        if (settings && first == VK_ESCAPE) { activate(3); return true; }
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE) {
        float x=static_cast<float>(static_cast<short>(LOWORD(second))), y=static_cast<float>(static_cast<short>(HIWORD(second)));
        hostDisplay.pointer(x,y);
        const bool trophy = !settings && !achievements && originalMenu && !centeredLandscapeMenuPage()
            && *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu)+4)
            && x-menuLogoOffset()>=96 && x-menuLogoOffset()<235 && y>=37.5f && y<=80;
        int hit=trophy?6:-1;
        for (const auto& b:buttons) if(x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h) hit=b.action;
        if (message==WM_LBUTTONDOWN && hit>=0) { pressed=hit; pressCancelled=false; SetCapture(window); return true; }
        if (message==WM_MOUSEMOVE && pressed>=0) { if (hit!=pressed || !(first&MK_LBUTTON)) pressCancelled=true; return true; }
        if (message==WM_LBUTTONUP && pressed>=0) {
            const int action=pressed; const bool accepted=!pressCancelled&&action==hit;
            cancelPress(window); if(accepted) activate(action); return true;
        }
    }
    return (settings||achievements) && (message==WM_MOUSEMOVE||message==WM_MOUSEWHEEL||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_KEYDOWN||message==WM_KEYUP);
}
}

/** Give configured map navigation priority over fixed overview and play shortcuts. */
bool desktopMapMenuBoundKey(WPARAM key, LPARAM flags) {
    using namespace desktop_controls;
    const int direction = key == static_cast<WPARAM>(keys[PreviousMap]) ? -1
        : key == static_cast<WPARAM>(keys[NextMap]) ? 1 : 0;
    if (!direction) return false;
    if (!(flags & (1LL << 30))) desktopMapMenuStep(direction);
    return true;
}

/** Bridge early window dispatch to the desktop settings and help modules. */
bool desktopOverlayInput(HWND w,UINT m,WPARAM a,LPARAM b) { return desktop_ui::input(w,m,a,b) || desktopMapMenuInput(w,m,a,b); }

/** Draw nonmodal notifications and desktop pages immediately before buffer presentation. */
void renderDesktopOverlay() { desktop_ui::render(); }

/** Open an original offline page within the game's own paused desktop overlay. */
void setDesktopGuide(const std::string& filename) { desktop_ui::pauseForPage(); desktop_guide::open(filename); }
