// Provide the portable desktop entry point without changing original game/menu code.
#pragma once

namespace desktop_launch {
// Desktop settings and diagnostics belong to the portable copy, never a user profile.
std::filesystem::path settingsPath, diagnosticPath;
// Persist only the restored client size, so maximized dimensions do not replace it.
int clientWidth = 1280, clientHeight = 720, frameLimit = 60;
// The portable desktop version is independent of the original mobile version.
constexpr const wchar_t* Version = L"Windows 0.7";
// Remember the normal window rectangle and mode without replacing it during fullscreen.
int windowX = CW_USEDEFAULT, windowY = CW_USEDEFAULT;
bool fullscreen = false, maximized = false, restoringWindow = false;
// Restore the decorated window exactly when leaving borderless fullscreen.
WINDOWPLACEMENT normalPlacement{sizeof(WINDOWPLACEMENT)};
LONG_PTR normalStyle = WS_OVERLAPPEDWINDOW;
// Only ordinary no-argument launches display errors and retain desktop preferences.
bool interactive = false;

/** Display a readable failure even when no console exists. */
void showError(const std::wstring& detail) {
    std::wstring message = detail;
    if (!diagnosticPath.empty()) message += L"\n\nDiagnostic log:\n" + diagnosticPath.wstring();
    MessageBoxW(nullptr, message.c_str(), L"TowerMadness", MB_OK | MB_ICONERROR);
}

/** Save host preferences independently from the original account and game checkpoint. */
void saveSettings() {
    if (settingsPath.empty()) return;
    if (framePacer) frameLimit = framePacer->targetFPS();
    std::ofstream file(settingsPath);
    if (file) file << nlohmann::json{{"width", clientWidth}, {"height", clientHeight}, {"fps", frameLimit},
        {"x", windowX}, {"y", windowY}, {"fullscreen", fullscreen}, {"maximized", maximized},
        {"autosave", desktop_save::autosaveEnabled}}.dump(2);
    if (!file) log("DESKTOP_WARNING Could not write data/host-settings.json.");
}

/** Capture the normal placement, including a maximized window's restoration rectangle. */
void rememberWindow() {
    if (!windowHandle || fullscreen || restoringWindow || !IsWindow(windowHandle)) return;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    if (!GetWindowPlacement(windowHandle, &placement)) return;
    maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    if (maximized) {
        windowX = placement.rcNormalPosition.left; windowY = placement.rcNormalPosition.top;
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST), &monitor)) {
            // WINDOWPLACEMENT uses workspace coordinates for decorated top-level windows.
            windowX += monitor.rcWork.left - monitor.rcMonitor.left;
            windowY += monitor.rcWork.top - monitor.rcMonitor.top;
        }
    } else if (!IsIconic(windowHandle)) {
        RECT bounds{};
        if (GetWindowRect(windowHandle, &bounds)) { windowX = bounds.left; windowY = bounds.top; }
    }
    if (!IsIconic(windowHandle) && !maximized) {
        RECT client{};
        if (GetClientRect(windowHandle, &client) && client.right > 0 && client.bottom > 0) {
            clientWidth = client.right; clientHeight = client.bottom;
        }
    }
}

/** Toggle borderless fullscreen on the current monitor while retaining normal placement. */
void toggleFullscreen() {
    if (!windowHandle || !IsWindow(windowHandle)) return;
    if (!fullscreen) {
        rememberWindow();
        normalPlacement.length = sizeof(normalPlacement);
        if (!GetWindowPlacement(windowHandle, &normalPlacement)) return;
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        normalStyle = GetWindowLongPtrW(windowHandle, GWL_STYLE);
        fullscreen = true;
        SetWindowLongPtrW(windowHandle, GWL_STYLE, normalStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(windowHandle, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        restoringWindow = true;
        SetWindowLongPtrW(windowHandle, GWL_STYLE, normalStyle);
        SetWindowPlacement(windowHandle, &normalPlacement);
        SetWindowPos(windowHandle, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        fullscreen = false;
        restoringWindow = false;
        rememberWindow();
    }
    saveSettings();
}

/** Apply desktop placement only to ordinary launches; disconnected monitors fall back safely. */
void restoreWindow() {
    if (!interactive || !windowHandle) return;
    const bool restoreFullscreen = fullscreen;
    fullscreen = false;
    restoringWindow = true;
    if (windowX != CW_USEDEFAULT && windowY != CW_USEDEFAULT) {
        RECT bounds{0, 0, clientWidth, clientHeight};
        AdjustWindowRect(&bounds, static_cast<DWORD>(GetWindowLongPtrW(windowHandle, GWL_STYLE)), FALSE);
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        RECT proposed{windowX, windowY, windowX + width, windowY + height};
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST), &monitor)) {
            // Keep the title bar reachable after monitor or DPI changes.
            const int x = std::clamp<LONG>(windowX, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width));
            const int y = std::clamp<LONG>(windowY, monitor.rcWork.top, std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
            SetWindowPos(windowHandle, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if (maximized) ShowWindow(windowHandle, SW_MAXIMIZE);
    restoringWindow = false;
    rememberWindow();
    if (restoreFullscreen) toggleFullscreen();
}

/** Open the portable save directory without depending on the process working directory. */
bool openSaveFolder() {
    const auto directory = !dataDirectory.empty() ? dataDirectory : settingsPath.parent_path();
    if (directory.empty()) return false;
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(windowHandle, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32) return true;
    desktop_save::notify(false, L"Could not open the save directory.");
    return false;
}

/** Export logs and host settings to a new diagnostics folder, excluding account and save files. */
bool exportDiagnostics(bool reveal) {
    if (diagnosticPath.empty()) return false;
    if (logFile) std::fflush(logFile);
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t name[96]{};
    std::swprintf(name, std::size(name), L"diagnostics-%04u%02u%02u-%02u%02u%02u-%03u", now.wYear,
        now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    const auto directory = diagnosticPath.parent_path() / name;
    try {
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(diagnosticPath, directory / L"game.log");
        if (!settingsPath.empty() && std::filesystem::exists(settingsPath))
            std::filesystem::copy_file(settingsPath, directory / L"host-settings.json");
        std::ofstream summary(directory / L"desktop.txt");
        summary << "TowerMadness Windows 0.7\nClient: " << hostDisplay.width << 'x' << hostDisplay.height
            << "\nFPS cap: " << (framePacer ? framePacer->targetFPS() : frameLimit)
            << "\nFullscreen: " << fullscreen << "\nAutosave: " << desktop_save::autosaveEnabled
            << "\nOpenGL: " << (windowGl ? reinterpret_cast<const char*>(glGetString(GL_VERSION)) : "unavailable") << '\n';
        summary.close();
        if (!summary) throw std::runtime_error("Cannot write diagnostic summary");
        desktop_save::notify(true, L"Diagnostics exported to the logs folder.");
        if(reveal) ShellExecuteW(windowHandle, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    } catch (const std::exception& error) {
        log("DESKTOP_WARNING Diagnostic export failed: " + std::string(error.what()));
        desktop_save::notify(false, L"Diagnostic export failed; check the log directory.");
        return false;
    }
}

/** Observe completed window messages while leaving the existing window procedure intact. */
LRESULT CALLBACK observeWindow(int code, WPARAM first, LPARAM second) {
    if (code >= 0) {
        const auto* message = reinterpret_cast<const CWPRETSTRUCT*>(second);
        if (message->hwnd == windowHandle) {
            if (message->message == WM_SIZE && message->wParam != SIZE_MINIMIZED) rememberWindow();
            if (message->message == WM_EXITSIZEMOVE || message->message == WM_CLOSE) { rememberWindow(); saveSettings(); }
            if (message->message == WM_KEYDOWN && message->wParam == VK_F6) saveSettings();
        }
    }
    return CallNextHookEx(nullptr, code, first, second);
}

/** Retain native-fault evidence and tell a desktop user where to find it. */
LONG WINAPI reportFault(EXCEPTION_POINTERS* exception) {
    const LONG result = nativeFault(exception);
    if (interactive) showError(L"The game encountered a native error and must close. Please retain the diagnostic log.");
    return result;
}

/** Resolve the executable directory independently of the launcher's working directory. */
std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("Cannot locate TowerMadness.exe.");
    return std::filesystem::path(buffer.data()).parent_path();
}

/** Load optional host preferences; environment overrides still support existing test tools. */
void loadSettings() {
    std::ifstream file(settingsPath);
    if (file) {
        try {
            nlohmann::json value; file >> value;
            const int width = value.value("width", clientWidth), height = value.value("height", clientHeight);
            const int fps = value.value("fps", frameLimit);
            if (width >= MinimumClientWidth && height >= MinimumClientHeight) { clientWidth = width; clientHeight = height; }
            if (fps >= 0) frameLimit = fps;
            windowX = value.value("x", windowX); windowY = value.value("y", windowY);
            fullscreen = value.value("fullscreen", false); maximized = value.value("maximized", false);
            desktop_save::autosaveEnabled = value.value("autosave", false);
        } catch (const std::exception&) { log("DESKTOP_WARNING Invalid host-settings.json; using defaults."); }
    }
    if (!std::getenv("TOWERMADNESS_WINDOW"))
        _putenv_s("TOWERMADNESS_WINDOW", (std::to_string(clientWidth) + "x" + std::to_string(clientHeight)).c_str());
    if (!std::getenv("TOWERMADNESS_FPS")) _putenv_s("TOWERMADNESS_FPS", std::to_string(frameLimit).c_str());
    if (!std::getenv("TOWERMADNESS_X86_AUDIO")) _putenv_s("TOWERMADNESS_X86_AUDIO", "1");
}

/** Launch the original menu by default, preserving explicit developer/test operations. */
int enter(int argc, wchar_t** argv) {
    interactive = argc == 1;
    if (!interactive && argc < 4) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(reportFault);
    if (const auto dpiMode = GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(dpiMode)(reinterpret_cast<HANDLE>(-4));
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HHOOK observer = nullptr;
    int result = 0;
    try {
        const auto root = executableDirectory();
        const auto package = interactive ? root / L"runtime/package" : std::filesystem::path(argv[1]);
        diagnosticPath = interactive ? root / L"logs/game.log" : std::filesystem::path(argv[3]);
        auto output = diagnosticPath.parent_path();
        if (output.empty()) output = L".";
        std::filesystem::create_directories(output);
        logFile = _wfopen(diagnosticPath.c_str(), L"w");
        if (!logFile) throw std::runtime_error("Cannot write the diagnostic log. Move the portable folder to a writable location.");
        if (interactive) {
            for (const auto& required : {package / L"lib/x86/libverde.so", package / L"assets",
                    root / L"runtime/textures", root / L"runtime/gs-instructions.json", root / L"runtime/gl-signatures.json",
                    root / L"runtime/portable-runtime.json"})
                if (!std::filesystem::exists(required)) throw std::runtime_error("Runtime files are missing. Extract the entire ZIP before starting TowerMadness.exe.");
            std::filesystem::create_directories(root / L"data");
            settingsPath = root / L"data/host-settings.json";
            loadSettings();
            observer = SetWindowsHookExW(WH_CALLWNDPROCRET, observeWindow, nullptr, GetCurrentThreadId());
            if (!observer) log("DESKTOP_WARNING Window size persistence is unavailable.");
        }
        // Operation names are ASCII; paths remain UTF-16 for Windows filesystem APIs.
        run(package, interactive ? "menu" : std::filesystem::path(argv[2]).string(), output);
        if (interactive) saveSettings();
    } catch (const std::exception& error) {
        log("HOST_ERROR " + std::string(error.what()));
        if (interactive) {
            const std::string detail(error.what());
            showError(std::wstring(detail.begin(), detail.end()));
        }
        result = 3;
    }
    if (observer) UnhookWindowsHookEx(observer);
    framePacer.reset();
    if (logFile) { std::fclose(logFile); logFile = nullptr; }
    CoUninitialize();
    return result;
}
}
