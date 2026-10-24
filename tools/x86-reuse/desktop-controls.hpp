// Adapt desktop gestures and shortcuts while retaining original game commands and camera bounds.
#pragma once

// UI and persistence are defined after the original file/object adapters.
bool desktopOverlayInput(HWND window, UINT message, WPARAM first, LPARAM second);
void renderDesktopOverlay();
bool saveDesktopGame(void* menu);
void updateDesktopSave();
void setDesktopGuide(const std::string& filename);
bool desktopMapMenuStep(int direction);

namespace desktop_controls {
// Slop is measured in physical pixels and scaled for the current monitor DPI.
constexpr float ClickSlopPixels = 6.0f;
// Wheel motion decays exponentially, independent of frame rate; one notch keeps the original total span.
constexpr double ZoomDecaySeconds = 0.085;
constexpr float PinchHalfSpan = 80.0f, WheelSpanStep = 8.0f;
// Keep terrain margins while allowing small maps to fill the view like the user's reference framing.
constexpr float FramingMargin = 1.15f, MinimumFramingAltitude = 20.0f, MaximumFramingAltitude = 42.0f;
// Keep the settings page and persisted HUD preference on the same supported range.
constexpr float MaximumHudScale = 3.0f;
// Wrap yaw numerically after complete turns without imposing a physical rotation limit.
constexpr float FullTurnRadians = 6.28318530718f;
// User preferences affect gestures and presentation, never simulation speed or weapon data.
float dragSensitivity = 1.0f, wheelSensitivity = 1.0f, hudScale = 1.2f;
// Individual actions can be rebound in Desktop Settings.
enum Action { Pause, Wave, Upgrade, RotateLeft, RotateRight, ResetCamera, PreviousMap, NextMap, ActionCount };
int keys[ActionCount] = {VK_SPACE, 'N', 'W', 'Q', 'E', 'R', 'A', 'D'};
// Track a drag separately from the original touch to prevent sub-threshold move delivery.
bool pointerDown = false, dragging = false, rotating = false;
POINT pressPixel{}, rotationPixel{};
// Smoothed wheel state is invalidated when its GAME changes or a menu opens.
void* zoomGame = nullptr;
float zoomPending = 0, zoomX = 0, zoomY = 0;
LARGE_INTEGER lastZoomTick{};
// Stored separately from the original account and desktop window preferences.
std::filesystem::path settingsPath;
// Verified original rendering/allocator trampolines preserve their complete five-byte prologues.
using OriginalAllocateGame = void*(__cdecl*)(void*, int);
using OriginalOverlay = void(__cdecl*)(void*);
OriginalAllocateGame originalAllocate = nullptr;
OriginalOverlay originalOverlay = nullptr;
// Catch original MENU-owned renders as well as explicit host render calls.
OriginalOverlay originalRender = nullptr;
// The original tutorial animates its own camera; apply desktop framing only after it finishes.
void* pendingFraming = nullptr;
bool restoringCheckpoint = false;
// A desktop pause retains the battlefield and withholds only its simulation update.
void* pausedGame = nullptr;
void resetCamera(void* current);
// Spread the original lazy tower-model work across frames instead of the first wheel interaction.
void* warmGame = nullptr;
unsigned warmIndex = 0;
// Desktop orbit persists until reset; the phone's original yaw spring would otherwise erase it.
void* orbitGame = nullptr;
using OriginalCameraUpdate = void(__cdecl*)(void*,double);
OriginalCameraUpdate originalCameraUpdate = nullptr;
// Retain original target, altitude and FOV bounds while extending only desktop yaw.
using OriginalCameraBounds = void(__cdecl*)(void*);
OriginalCameraBounds originalCameraBounds = nullptr;
// Virtual HUD dimensions exist only during overlay drawing, after world markers and the wheel.
void* overlayGame = nullptr;
int overlayWidth = 0, overlayHeight = 0;
bool scaledOverlay = false;
// The client cursor uses original-game green/gold cues; Windows retains border resize cursors.
HCURSOR gameCursor = nullptr;
using OriginalWavePreview = void(__cdecl*)(void*, float);
OriginalWavePreview originalWavePreview = nullptr;

/** Return only a currently playable game, excluding the original menu and tutorial. */
void* game(bool allowTutorial = false) {
    if (!activeGame) return nullptr;
    if (originalMenu && *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu) + 4)) return nullptr;
    void* result = originalMenu ? *reinterpret_cast<void**>(static_cast<unsigned char*>(originalMenu) + 0x15c) : activeGame;
    if (result && !allowTutorial && *reinterpret_cast<int*>(static_cast<unsigned char*>(result) + 0xa38)) return nullptr;
    return result;
}

/** Persist only host interaction preferences. */
void saveSettings() {
    if (settingsPath.empty()) return;
    std::ofstream file(settingsPath);
    file << nlohmann::json{{"dragSensitivity", dragSensitivity}, {"wheelSensitivity", wheelSensitivity},
        {"hudScale", hudScale}, {"keys", std::vector<int>(keys, keys + ActionCount)}}.dump(2);
    if (!file) log("DESKTOP_WARNING Cannot write control-settings.json");
}

/** Read optional preferences without rejecting a playable game on malformed settings. */
void loadSettings(const std::filesystem::path& directory) {
    settingsPath = directory / "control-settings.json";
    std::ifstream file(settingsPath);
    if (!file) return;
    try {
        nlohmann::json value; file >> value;
        dragSensitivity = std::clamp(value.value("dragSensitivity", 1.0f), 0.5f, 2.0f);
        wheelSensitivity = std::clamp(value.value("wheelSensitivity", 1.0f), 0.25f, 2.5f);
        hudScale = std::clamp(value.value("hudScale", 1.2f), 1.0f, MaximumHudScale);
        const auto bindings = value.value("keys", std::vector<int>{});
        for (std::size_t i = 0; i < bindings.size() && i < ActionCount; ++i)
            if (bindings[i] > 0 && bindings[i] < 256) keys[i] = bindings[i];
    } catch (const std::exception&) { log("DESKTOP_WARNING Invalid control settings; using available defaults"); }
}

/** Use the original paused menu and resume methods so its simulation clock remains correct. */
void togglePause() {
    zoomPending = 0;
    // Once the original menu owns the pause, its Resume tab also owns resuming the battle.
    pausedGame = nullptr;
    if (!originalMenu) { hostPauseRequested = !hostPauseRequested; return; }
    void* current = *reinterpret_cast<void**>(static_cast<unsigned char*>(originalMenu) + 0x15c);
    if (!current || reinterpret_cast<int(__cdecl*)(void*)>(requireExport("GAME_hasEnded"))(current)) return;
    if (*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu) + 4))
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_hide"))(originalMenu);
    else {
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_backToMainMenu"))(originalMenu);
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_show"))(originalMenu);
    }
    log("DESKTOP_PAUSE toggled");
}

/** Test the retained game identity so replacing a game cannot inherit a desktop pause. */
bool isPaused(void* current) {
    return current && current == pausedGame;
}

/** Freeze the battlefield in place, then resume through the original clock/countdown method. */
void toggleInGamePause() {
    void* current = game();
    if (!current) { togglePause(); return; }
    if (reinterpret_cast<int(__cdecl*)(void*)>(requireExport("GAME_hasEnded"))(current)) return;
    zoomPending = 0;
    pointerDown = dragging = rotating = false;
    if (GetCapture() == windowHandle) ReleaseCapture();
    if (isPaused(current)) {
        pausedGame = nullptr;
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_resume"))(current);
        log("DESKTOP_BATTLE_PAUSE resumed");
    } else {
        pausedGame = current;
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_silence"))(current);
        log("DESKTOP_BATTLE_PAUSE paused");
    }
}

/** Feed a small pinch through original touch handlers and their unchanged zoom bounds. */
void pinch(void* current, float delta, float x, float y) {
    const HostTouch saved = hostTouch;
    hostTouchCount = 2;
    hostTouch = HostTouch{}; hostPinchTouch = HostTouch{};
    hostTouch.x = hostTouch.previousX = x - PinchHalfSpan;
    hostPinchTouch.x = hostPinchTouch.previousX = x + PinchHalfSpan;
    hostTouch.y = hostTouch.previousY = hostPinchTouch.y = hostPinchTouch.previousY = y;
    hostTouch.tapCount = hostPinchTouch.tapCount = 0;
    reinterpret_cast<void(__cdecl*)(void*, void*)>(requireExport("GAME_touchesBegan"))(current, &hostTouchSet);
    hostTouch.x -= delta; hostPinchTouch.x += delta;
    reinterpret_cast<void(__cdecl*)(void*, void*)>(requireExport("GAME_touchesMoved"))(current, &hostTouchSet);
    reinterpret_cast<void(__cdecl*)(void*, void*)>(requireExport("GAME_touchesEnded"))(current, &hostTouchSet);
    hostTouchCount = 1;
    hostTouch = saved;
}

/** Apply an exponential wheel impulse once per presentation frame, without changing realtime. */
void updateZoom() {
    LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    // GetTickCount64 quantizes motion to the system tick, leaving alternating 120-FPS frames stationary.
    const double elapsed = lastZoomTick.QuadPart
        ? static_cast<double>(now.QuadPart - lastZoomTick.QuadPart) / frequency.QuadPart : 1.0 / 60;
    lastZoomTick = now;
    void* current = game();
    if (!current || isPaused(current) || current != zoomGame || pointerDown || rotating) { zoomPending = 0; zoomGame = current; return; }
    if (std::abs(zoomPending) < 0.002f) { zoomPending = 0; return; }
    const float delta = std::clamp(zoomPending * static_cast<float>(1.0 - std::exp(-elapsed / ZoomDecaySeconds)), -35.0f, 35.0f);
    zoomPending -= delta;
    pinch(current, delta, zoomX, zoomY);
}

/** Preload one original tower type per frame using its existing shared model/texture cache. */
void warmTowerModels() {
    void* current=game(true); if(!current) return;
    if(current!=warmGame) { warmGame=current; warmIndex=0; }
    auto* mode=*reinterpret_cast<std::uint32_t**>(current);
    auto* technology=reinterpret_cast<std::uint32_t*>(mode[7]);
    if(!technology || warmIndex>=technology[14]) return;
    auto* types=reinterpret_cast<std::uint32_t*>(technology[15]);
    reinterpret_cast<void(__cdecl*)(void*)>(requireExport("PIEMENU_loadTowerModel"))(reinterpret_cast<void*>(types[warmIndex]));
    ++warmIndex;
    if(warmIndex==technology[14]) log("DESKTOP_PREWARM_DONE types="+std::to_string(warmIndex));
}

/** Close only the original wheel and selection through their original UI methods. */
void cancelWheel() {
    if (void* current = game()) {
        void* pie = *reinterpret_cast<void**>(static_cast<unsigned char*>(current) + 0xa48);
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("PIEMENU_hide"))(pie);
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_deselectCell"))(current);
    }
}

/** Reframe only the camera using the original terrain dimensions and original bounds. */
void resetCamera(void* current) {
    if (!current) return;
    auto* bytes = static_cast<unsigned char*>(current);
    void* mode = *reinterpret_cast<void**>(bytes);
    auto* terrain = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(mode) + 0x20);
    const float width = static_cast<float>(*reinterpret_cast<int*>(terrain + 0x24));
    const float height = static_cast<float>(*reinterpret_cast<int*>(terrain + 0x28));
    const float aspect = static_cast<float>(LogicalWidth) / LogicalHeight;
    const float altitude = std::clamp(std::max(width / aspect, height) * FramingMargin,
        MinimumFramingAltitude, MaximumFramingAltitude);
    void* camera = *reinterpret_cast<void**>(bytes + 0xa00);
    // Preserve the original camera's horizontal offset, including during tutorial handoff.
    const float offset = *reinterpret_cast<float*>(bytes + 0xa6c);
    orbitGame=nullptr;
    // Reset cancels pending pan/nudge momentum as well as restoring framing.
    std::fill_n(reinterpret_cast<float*>(bytes+0xa08),6,0.0f);
    std::fill_n(reinterpret_cast<float*>(bytes+0xa7c),3,0.0f);
    *reinterpret_cast<float*>(bytes + 0xa9c) = 0;
    reinterpret_cast<void(__cdecl*)(void*, float, float, float)>(requireExport("CAMERA_lookAt"))(camera, width / 2, height / 2, 0);
    reinterpret_cast<void(__cdecl*)(void*, float, float, float)>(requireExport("CAMERA_setPosition"))(camera, width / 2, height / 2 - offset, altitude);
    reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_makeSureCameraIsGood"))(current);
    log("DESKTOP_CAMERA terrain=" + std::to_string(width) + "x" + std::to_string(height) + " altitude=" + std::to_string(altitude));
}

/** Complete tutorial framing before rendering the first playable frame. */
void prepareCamera(void* current) {
    if (current != pendingFraming || !current) return;
    if (*reinterpret_cast<int*>(static_cast<unsigned char*>(current) + 0xa38)) return;
    resetCamera(current);
    pendingFraming = nullptr;
}

/** Finish pending framing before the original renderer reads the camera matrices. */
void __cdecl renderGame(void* current) {
    prepareCamera(current);
    originalRender(current);
}

/** Retain original camera bounds while allowing a desktop orbit through complete turns. */
void __cdecl makeSureCameraIsGood(void* current) {
    auto* bytes = static_cast<unsigned char*>(current);
    const float angle = *reinterpret_cast<float*>(bytes + 0xa9c);
    originalCameraBounds(current);
    if (current != orbitGame || *reinterpret_cast<int*>(bytes + 0xa38)) return;
    *reinterpret_cast<float*>(bytes + 0xa9c) = angle;
    auto* camera = *reinterpret_cast<unsigned char**>(bytes + 0xa00);
    const float offset = *reinterpret_cast<float*>(bytes + 0xa6c);
    const float targetX = *reinterpret_cast<float*>(camera + 0x114);
    const float targetY = *reinterpret_cast<float*>(camera + 0x118);
    const float altitude = *reinterpret_cast<float*>(camera + 0x110);
    reinterpret_cast<void(__cdecl*)(void*, float, float, float)>(requireExport("CAMERA_setPosition"))(
        camera, targetX + std::sin(angle) * offset, targetY - std::cos(angle) * offset, altitude);
}

/** Set the original camera's supported orbit angle without changing its target or simulation. */
void rotateCamera(float radians) {
    if (void* current = game()) {
        orbitGame=current;
        auto& angle = *reinterpret_cast<float*>(static_cast<unsigned char*>(current) + 0xa9c);
        angle = std::remainder(angle + radians, FullTurnRadians);
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_makeSureCameraIsGood"))(current);
    }
}

/** Suppress only the phone yaw spring while retaining original pan, projection, and camera bounds. */
void __cdecl updateCamera(void* current,double realtime) {
    auto& lock=*reinterpret_cast<int*>(static_cast<unsigned char*>(current)+0xa98);
    const int previous=lock;
    if(current==orbitGame) lock=1;
    originalCameraUpdate(current,realtime);
    lock=previous;
    prepareCamera(current);
}

/** Recover desktop yaw from the original serialized camera instead of adding data to the save stream. */
void adoptRestoredCamera(void* current) {
    pausedGame=nullptr;
    auto* bytes=static_cast<unsigned char*>(current);
    const auto* camera=*reinterpret_cast<const unsigned char**>(bytes+0xa00);
    const float offset=*reinterpret_cast<const float*>(bytes+0xa6c);
    if(std::abs(offset)<0.001f) return;
    const float x=*reinterpret_cast<const float*>(camera+0x108)-*reinterpret_cast<const float*>(camera+0x114);
    const float y=*reinterpret_cast<const float*>(camera+0x118)-*reinterpret_cast<const float*>(camera+0x10c);
    const float angle=std::atan2(x/offset,y/offset);
    *reinterpret_cast<float*>(bytes+0xa9c)=angle;
    orbitGame=std::abs(angle)>0.0001f?current:nullptr;
}

/** Follow only the sole legal upgrade option from the currently selected native wheel. */
void upgradeSelection() {
    void* current = game(); if (!current) return;
    auto* pie = *reinterpret_cast<int**>(static_cast<unsigned char*>(current) + 0xa48);
    if (reinterpret_cast<int(__cdecl*)(void*)>(requireExport("PIEMENU_existingTowerSelected"))(pie) < 0) return;
    // Original option-list nodes hold {option,next}; option[10]=0 means upgrade.
    auto* node = *reinterpret_cast<std::uint32_t**>(pie[1]);
    float* option = nullptr;
    int count = 0;
    while (node) {
        auto* candidate = reinterpret_cast<float*>(node[0]);
        if (*reinterpret_cast<int*>(candidate + 10) == 0) { option = candidate; ++count; }
        node = reinterpret_cast<std::uint32_t*>(node[1]);
    }
    if (count != 1) { desktop_save::notify(false, L"Choose the desired upgrade in the tower wheel."); return; }
    std::array<float, 2> center{};
    reinterpret_cast<void(__cdecl*)(float*, void*)>(requireExport("PIEMENU_getScreenPosition"))(center.data(), pie);
    const float x = center[0] + option[0], y = center[1] + option[1];
    reinterpret_cast<int(__cdecl*)(void*, float, float, int)>(requireExport("PIEMENU_handleTap"))(pie, x, y, 0);
    reinterpret_cast<int(__cdecl*)(void*, float, float, int)>(requireExport("PIEMENU_handleTap"))(pie, x, y, 1);
    log("DESKTOP_UPGRADE original wheel action");
}

/** Apply map-specific framing only to fresh games; restoring preserves the saved camera. */
void* __cdecl allocateGame(void* mode, int replay) {
    void* current = originalAllocate(mode, replay);
    pausedGame=nullptr;
    warmGame=nullptr;
    orbitGame=nullptr;
    pendingFraming = current && !replay && !restoringCheckpoint ? current : nullptr;
    // MENU enables the tutorial after allocation returns; defer until the first actual render.
    return current;
}

/** Begin a larger HUD after the unchanged world labels and radial menu have already drawn. */
void __cdecl drawWavePreview(void* preview, float alpha) {
    if (overlayGame && hudScale > 1.001f) {
        auto* bytes = static_cast<unsigned char*>(overlayGame);
        *reinterpret_cast<int*>(bytes + 0xbf8) = static_cast<int>(std::lround(overlayWidth / hudScale));
        *reinterpret_cast<int*>(bytes + 0xbfc) = static_cast<int>(std::lround(overlayHeight / hudScale));
        reinterpret_cast<void(__cdecl*)()>(requireExport("PGL_projectionMatrixMode"))();
        reinterpret_cast<void(__cdecl*)()>(requireExport("PGL_loadIdentity"))();
        reinterpret_cast<void(__cdecl*)(float,float,float,float,float,float)>(requireExport("PGL_orthof"))(
            0, overlayWidth / hudScale, overlayHeight / hudScale, 0, 1000, -1000);
        reinterpret_cast<void(__cdecl*)()>(requireExport("PGL_modelViewMatrixMode"))();
        scaledOverlay = true;
    }
    originalWavePreview(preview, alpha);
}

/** Scope presentation dimensions to the HUD, leaving camera/world picking and saved state unchanged. */
void __cdecl renderOverlay(void* current) {
    auto* bytes = static_cast<unsigned char*>(current);
    overlayWidth = *reinterpret_cast<int*>(bytes + 0xbf8); overlayHeight = *reinterpret_cast<int*>(bytes + 0xbfc);
    overlayGame = *reinterpret_cast<int*>(bytes + 0xa38) ? nullptr : current;
    scaledOverlay = false;
    originalOverlay(current);
    if (scaledOverlay) {
        *reinterpret_cast<int*>(bytes + 0xbf8) = overlayWidth; *reinterpret_cast<int*>(bytes + 0xbfc) = overlayHeight;
    }
    overlayGame = nullptr;
}

/** Invert enlarged corner controls using the same fixed edge anchors as original drawing. */
void hudTouch(float& x, float& y) {
    if (!game() || hudScale <= 1.001f) return;
    if (x >= LogicalWidth - 70 * hudScale && y < 85 * hudScale) { x = LogicalWidth - (LogicalWidth - x) / hudScale; y /= hudScale; }
    else if (y >= LogicalHeight - 64 * hudScale && (x < 64 * hudScale || x >= LogicalWidth - 64 * hudScale)) {
        x = x < LogicalWidth / 2 ? x / hudScale : LogicalWidth - (LogicalWidth - x) / hudScale;
        y = LogicalHeight - (LogicalHeight - y) / hudScale;
    }
}

/** Install verified original helper shims only in this process's private ELF mapping. */
void install() {
    originalAllocate = reinterpret_cast<OriginalAllocateGame>(installLandscapeMenuHelper(0x250c01, reinterpret_cast<void*>(&allocateGame)));
    originalRender = reinterpret_cast<OriginalOverlay>(installLandscapeMenuHelper(0x254256, reinterpret_cast<void*>(&renderGame)));
    originalOverlay = reinterpret_cast<OriginalOverlay>(installLandscapeMenuHelper(0x253692, reinterpret_cast<void*>(&renderOverlay)));
    originalCameraUpdate=reinterpret_cast<OriginalCameraUpdate>(installLandscapeMenuHelper(0x25126e,reinterpret_cast<void*>(&updateCamera),0x56));
    originalCameraBounds = reinterpret_cast<OriginalCameraBounds>(installLandscapeMenuHelper(
        0x25094c, reinterpret_cast<void*>(&makeSureCameraIsGood)));
    originalWavePreview = reinterpret_cast<OriginalWavePreview>(requireExport("WAVEPREVIEW_draw"));
    installLandscapeBackdropCall(0x2538d4, reinterpret_cast<void*>(originalWavePreview), reinterpret_cast<void*>(&drawWavePreview));
}

/** Create a crisp game-styled pointer with a matching click hotspot and transparent surround. */
HCURSOR cursor() {
    if (gameCursor) return gameCursor;
    // A centered construction reticle is drawn from scratch, with no Windows arrow silhouette.
    constexpr int CursorSize = 40, CursorCenter = CursorSize / 2;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=CursorSize; info.bmiHeader.biHeight=-CursorSize;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    HDC screen=GetDC(nullptr), dc=CreateCompatibleDC(screen);
    HBITMAP color=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!color || !dc) { if(color)DeleteObject(color); if(dc)DeleteDC(dc); ReleaseDC(nullptr,screen); return LoadCursorA(nullptr,IDC_ARROW); }
    std::memset(pixels,0,CursorSize*CursorSize*4);
    HGDIOBJ previous=SelectObject(dc,color);
    const POINT shape[]={{20,5},{35,20},{20,35},{5,20}};
    HPEN outline=CreatePen(PS_SOLID,2,RGB(227,193,85)); HBRUSH fill=CreateSolidBrush(RGB(26,70,63));
    HGDIOBJ oldPen=SelectObject(dc,outline),oldBrush=SelectObject(dc,fill);
    Polygon(dc,shape,static_cast<int>(std::size(shape)));
    // The inset dark lens and bright cross establish an exact center click hotspot.
    HBRUSH lens=CreateSolidBrush(RGB(9,25,28));
    SelectObject(dc,lens); Ellipse(dc,12,12,29,29);
    HPEN cross=CreatePen(PS_SOLID,2,RGB(151,231,110)); SelectObject(dc,cross);
    MoveToEx(dc,20,1,nullptr); LineTo(dc,20,11);
    MoveToEx(dc,20,30,nullptr); LineTo(dc,20,39);
    MoveToEx(dc,1,20,nullptr); LineTo(dc,11,20);
    MoveToEx(dc,30,20,nullptr); LineTo(dc,39,20);
    MoveToEx(dc,17,20,nullptr); LineTo(dc,24,20);
    MoveToEx(dc,20,17,nullptr); LineTo(dc,20,24);
    SetPixelV(dc,CursorCenter,CursorCenter,RGB(255,245,190));
    SelectObject(dc,oldPen); SelectObject(dc,oldBrush);
    DeleteObject(cross); DeleteObject(lens); DeleteObject(outline); DeleteObject(fill);
    auto* rgba=static_cast<std::uint32_t*>(pixels);
    for(int i=0;i<CursorSize*CursorSize;++i) if(rgba[i]&0xffffff) rgba[i]|=0xff000000;
    SelectObject(dc,previous);
    HBITMAP mask=CreateBitmap(CursorSize,CursorSize,1,1,nullptr);
    ICONINFO icon{}; icon.fIcon=FALSE; icon.xHotspot=CursorCenter; icon.yHotspot=CursorCenter; icon.hbmColor=color; icon.hbmMask=mask;
    gameCursor=static_cast<HCURSOR>(CreateIconIndirect(&icon));
    DeleteObject(mask); DeleteObject(color); DeleteDC(dc); ReleaseDC(nullptr,screen);
    return gameCursor?gameCursor:LoadCursorA(nullptr,IDC_ARROW);
}

/** Recognize desktop gestures before the original single-touch dispatcher. */
bool input(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message==WM_SETCURSOR && LOWORD(second)==HTCLIENT) { SetCursor(cursor()); return true; }
    if (message == WM_KEYDOWN && !(second & (1LL << 30))) {
        const int key=static_cast<int>(first);
        if (key == keys[PreviousMap] && desktopMapMenuStep(-1)) return true;
        if (key == keys[NextMap] && desktopMapMenuStep(1)) return true;
        if (key == keys[Upgrade]) { upgradeSelection(); return true; }
        if (key == keys[RotateLeft]) { rotateCamera(-0.1745329f); return true; }
        if (key == keys[RotateRight]) { rotateCamera(0.1745329f); return true; }
        if (key == keys[ResetCamera]) { if (void* current = game()) resetCamera(current); return true; }
    }
    if (message == WM_MBUTTONDOWN && game()) {
        rotating = true; rotationPixel = POINT{static_cast<short>(LOWORD(second)), static_cast<short>(HIWORD(second))};
        SetCapture(window); return true;
    }
    if (message == WM_MOUSEMOVE && rotating) {
        const int x = static_cast<short>(LOWORD(second));
        rotateCamera((rotationPixel.x - x) * 0.005f); rotationPixel.x = x; return true;
    }
    if (message == WM_MBUTTONUP && rotating) { rotating = false; ReleaseCapture(); return true; }
    if ((message == WM_SYSKEYDOWN && first == VK_RETURN && (second & (1LL << 29)))
        || (message == WM_KEYDOWN && first == VK_F11 && !(second & (1LL << 30)))) {
        desktop_launch::toggleFullscreen(); return true;
    }
    if (message == WM_RBUTTONDOWN) { cancelWheel(); return true; }
    if (message == WM_RBUTTONUP) return true;
    if (message == WM_MOUSEWHEEL) {
        void* current = game();
        if (!current || pointerDown) return true;
        POINT center{static_cast<short>(LOWORD(second)), static_cast<short>(HIWORD(second))};
        ScreenToClient(window, &center);
        zoomX = static_cast<float>(center.x); zoomY = static_cast<float>(center.y);
        if (!hostDisplay.pointer(zoomX, zoomY)) return true;
        if (zoomGame != current) { zoomPending = 0; zoomGame = current; }
        zoomPending = std::clamp(zoomPending + static_cast<short>(HIWORD(first)) / 120.0f * WheelSpanStep * wheelSensitivity, -80.0f, 80.0f);
        log("DESKTOP_WHEEL impulse=" + std::to_string(zoomPending));
        return true;
    }
    if (message == WM_KILLFOCUS || message == WM_CANCELMODE) {
        pointerDown = dragging = rotating = false; zoomPending = 0;
        if (GetCapture() == window) ReleaseCapture();
        hostTouch.tapCount = 0;
    }
    return false;
}
}
