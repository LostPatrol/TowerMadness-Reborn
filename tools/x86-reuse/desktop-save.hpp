// Preserve original checkpoint bytes with atomic replacement, backups, and desktop feedback.
#pragma once

namespace desktop_save {
// Five prior successful checkpoints remain available beside the manual checkpoint.
constexpr std::size_t BackupCount = 5;
// Optional autosaves use their own file and run at a deliberately modest interval.
constexpr ULONGLONG AutosaveIntervalMs = 120000;
// Brief nonmodal feedback is rendered by the host's existing overlay.
constexpr ULONGLONG ToastDurationMs = 4500;
// Autosaving is opt-in and never changes the manual checkpoint.
bool autosaveEnabled = false;
// A deadline is initialized on first observation rather than saving immediately on launch.
ULONGLONG nextAutosave = 0;
// Last write failure is retained for the overlay and diagnostics.
std::wstring lastError;
// The host draws this state without introducing a second native child window.
struct Toast { std::wstring text; bool success = true; ULONGLONG until = 0; };
Toast toast;

/** Format a filesystem failure without replacing its useful Windows error code. */
std::wstring windowsError(DWORD code) {
    wchar_t* detail = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&detail), 0, nullptr);
    std::wstring text = detail ? detail : L"Windows file operation failed";
    if (detail) LocalFree(detail);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
    return text + L" (" + std::to_wstring(code) + L")";
}

/** Retain a visible result for the next several frames, including failures while paused. */
void notify(bool success, const std::wstring& text) {
    toast = {text, success, GetTickCount64() + ToastDurationMs};
    if (!success) {
        const int count=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
        std::string detail(static_cast<std::size_t>(count),'\0');
        if(count) WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),detail.data(),count,nullptr,nullptr);
        log("SAVE_WARNING "+detail);
    }
}

/** Write all bytes, treating short writes as failures rather than successful saves. */
bool writeBytes(HANDLE file, const void* bytes, DWORD count) {
    DWORD written = 0;
    if (WriteFile(file, bytes, count, &written, nullptr) && written == count) return true;
    DWORD code = GetLastError();
    if (!code) code = ERROR_WRITE_FAULT;
    lastError = windowsError(code);
    return false;
}

/** Remove only older backups of this exact checkpoint after a successful commit. */
void pruneBackups(const std::filesystem::path& checkpoint) {
    std::error_code error;
    std::vector<std::filesystem::path> backups;
    const std::wstring prefix = checkpoint.filename().wstring() + L".backup-";
    for (std::filesystem::directory_iterator entry(checkpoint.parent_path(), error), end; !error && entry != end; entry.increment(error)) {
        const auto name = entry->path().filename().wstring();
        if (name.starts_with(prefix) && name.ends_with(L".bin") && entry->is_regular_file(error)) backups.push_back(entry->path());
    }
    std::sort(backups.begin(), backups.end(), std::greater<std::filesystem::path>());
    for (std::size_t index = BackupCount; index < backups.size(); ++index) {
        error.clear();
        std::filesystem::remove(backups[index], error);
        if (error) log("SAVE_WARNING Could not remove an old checkpoint backup.");
    }
}

/** Commit the unchanged OBS envelope; a failed write never truncates the previous checkpoint. */
bool writeCheckpoint(const std::filesystem::path& path, const void* bytes, std::uint32_t length,
        std::uint32_t magic, bool keepBackups = true) {
    lastError.clear();
    if (!bytes || !length) { lastError = L"The original game produced an empty checkpoint."; return false; }
    std::error_code error;
    const auto directory = path.parent_path().empty() ? std::filesystem::path(L".") : path.parent_path();
    std::filesystem::create_directories(directory, error);
    if (error) { lastError = L"Cannot create the save directory: " + windowsError(error.value()); return false; }
    // A sibling staging file keeps the final rename on the same volume.
    const auto staging = std::filesystem::path(path.wstring() + L".writing");
    HANDLE file = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { lastError = windowsError(GetLastError()); return false; }
    bool success = writeBytes(file, &magic, sizeof(magic)) && writeBytes(file, &length, sizeof(length)) && writeBytes(file, bytes, length);
    if (success && !FlushFileBuffers(file)) { lastError = windowsError(GetLastError()); success = false; }
    if (!CloseHandle(file) && success) { lastError = windowsError(GetLastError()); success = false; }
    if (!success) { DeleteFileW(staging.c_str()); return false; }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        // ReplaceFile atomically retains the old file as a backup; failure leaves it intact.
        FILETIME now{}; GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER stamp{}; stamp.LowPart = now.dwLowDateTime; stamp.HighPart = now.dwHighDateTime;
        wchar_t suffix[96]{};
        std::swprintf(suffix, std::size(suffix), L".backup-%020llu-%010lu.bin", stamp.QuadPart, GetCurrentProcessId());
        const auto backup = std::filesystem::path(path.wstring() + suffix);
        success = ReplaceFileW(path.c_str(), staging.c_str(), keepBackups ? backup.c_str() : nullptr, 0, nullptr, nullptr) != FALSE;
    } else {
        success = MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!success) { lastError = windowsError(GetLastError()); DeleteFileW(staging.c_str()); return false; }
    if (keepBackups) pruneBackups(path);
    return true;
}

/** Schedule only enabled autosaves; callers save the original GAME to a separate path. */
bool autosaveDue(ULONGLONG now) {
    if (!autosaveEnabled) { nextAutosave = 0; return false; }
    if (!nextAutosave) { nextAutosave = now + AutosaveIntervalMs; return false; }
    if (now < nextAutosave) return false;
    nextAutosave = now + AutosaveIntervalMs;
    return true;
}

/** Offer Save, Discard, and Cancel before closing an active game; failed saves keep it open. */
bool confirmExit(HWND window, bool hasGame, bool (*save)(void*), void* context) {
    if (!hasGame) return true;
    const int choice = MessageBoxW(window,
        L"Save the current game before exiting?\n\nYes: save and exit\nNo: exit without saving\nCancel: continue playing",
        L"TowerMadness - Save before exiting", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON3);
    if (choice == IDCANCEL || !choice) return false;
    if (choice == IDNO) return true;
    if (save && save(context)) return true;
    MessageBoxW(window, (L"The game could not be saved. The window will remain open.\n\n" + lastError).c_str(),
        L"TowerMadness - Save failed", MB_OK | MB_ICONERROR);
    return false;
}
}
