// Execute the immutable game's x86 ELF code in an isolated native Windows host.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <wincodec.h>
#include <intrin.h>
#include <GL/gl.h>
#include "../../third_party/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// ELF32 structures use the original four-byte ABI and never host-sized fields.
struct ElfHeader {
    unsigned char ident[16]; std::uint16_t type, machine; std::uint32_t version, entry, phoff, shoff, flags;
    std::uint16_t ehsize, phsize, phnum, shsize, shnum, shstr;
};
struct ProgramHeader { std::uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; };
struct Section { std::uint32_t name, type, flags, address, offset, size, link, info, align, stride; };
struct Symbol { std::uint32_t name, value, size; unsigned char info, other; std::uint16_t section; };
struct Relocation { std::uint32_t offset, info; };
struct Image {
    std::string name; std::vector<unsigned char> file; unsigned char* base = nullptr;
    std::vector<Section> sections; std::map<std::string, Symbol> exports;
};
// The process owns all image mappings, import traps, and experimental platform hooks.
std::vector<std::unique_ptr<Image>> images;
std::vector<std::string> missingNames;
std::map<std::string, void*> hooks;
std::filesystem::path assetsDirectory;
std::filesystem::path convertedTextures;
// Interactive (non-scripted) menu mode save/continue hotkeys, defined after the file helpers.
void hotkeySaveState(void* userMenu);
void hotkeyLoadState(void* userMenu);
std::FILE* logFile = nullptr;
// The experiment creates its own legacy OpenGL context and never plays audio.
HWND windowHandle = nullptr;
HDC windowDc = nullptr;
HGLRC windowGl = nullptr;
// Only the host's single-pointer UIKit boundary is adapted; native input handles actions.
void* activeGame = nullptr;
struct HostTouch { float x = 0, y = 0; };
HostTouch hostTouch;
int hostTouchSet = 0;
// Original EAGLView fields used by its unchanged outer button dispatcher.
std::array<unsigned char, 128> hostView{};
std::array<unsigned char, 512> hostMenu{};
// The original Menu hot zone currently toggles a host pause until the original MENU object is connected.
int hostPauseRequested = 0;
// The real original MENU object (0x3b4 bytes), its current GAME, and dummy platform receivers.
void* originalMenu = nullptr;
int hostApplicationToken = 0, hostGameAccountToken = 0;
// This private method address is verified against the immutable x86 1.22 ELF.
constexpr std::uint32_t OriginalViewTouchEnd = 0x28dc19;
// POSIX rand48 preserves the native visual-random sequence independently of combat MT.
std::uint64_t rand48State = 0x1234abcd330eULL;
constexpr std::uint64_t Rand48Multiplier = 0x5deece66dULL, Rand48Increment = 0xb, Rand48Mask = 0xffffffffffffULL;
// A single native main thread owns guest stack guards in this first probe.
std::array<std::uint32_t, 32> guestTls{};
// Windows mutexes replace opaque Bionic mutex storage without changing game state.
std::map<void*, std::unique_ptr<CRITICAL_SECTION>> mutexes;
// Bionic ctype exports are pointer objects, with one preceding EOF entry.
std::array<unsigned char, 257> ctypeTable{};
std::array<short, 257> upperTable{}, lowerTable{};
const unsigned char* ctypePointer = ctypeTable.data();
const short* upperPointer = upperTable.data();
const short* lowerPointer = lowerTable.data();
// All mappings reserve full Windows allocation pages; executable code stays x86.
constexpr std::uint32_t PageSize = 4096, LoadSegment = 1, SymbolTable = 11, RelSection = 9;

/** Record an immediately flushed diagnostic before entering foreign code. */
void log(const std::string& message) {
    // Per-frame original UI code repeats key reads; keep the first occurrences and periodic counts.
    static std::map<std::string, unsigned long long> repeats;
    const unsigned long long count = ++repeats[message];
    if (count > 3 && count % 500 != 0) return;
    std::FILE* stream = logFile ? logFile : stdout;
    if (count <= 3) std::fprintf(stream, "%s\n", message.c_str());
    else std::fprintf(stream, "%s [x%llu]\n", message.c_str(), count);
    std::fflush(stream);
}

/** Decode an original NUL-terminated ELF string at a validated file offset. */
const char* stringAt(const Image& image, std::uint32_t offset) {
    if (offset >= image.file.size()) throw std::runtime_error("ELF string outside file");
    return reinterpret_cast<const char*>(image.file.data() + offset);
}

/** Stop on an actually executed unresolved API instead of inventing a result. */
extern "C" __declspec(noreturn) void __cdecl missingImport(unsigned index) {
    log("UNIMPLEMENTED_API " + missingNames.at(index));
    const auto* stack = static_cast<const std::uint32_t*>(_AddressOfReturnAddress());
    const std::uint32_t caller = stack[2];
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uint32_t>(image->base);
        if (caller >= base && caller < base + image->file.size()) {
            std::ostringstream message; message << "IMPORT_CALLER " << image->name << "+0x" << std::hex << caller - base; log(message.str());
        }
    }
    ExitProcess(30);
}

/** Build a native x86 diagnostic trampoline for one unresolved function. */
void* importTrap(const std::string& name) {
    const unsigned index = static_cast<unsigned>(missingNames.size());
    missingNames.push_back(name);
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    code[0] = 0x68; std::memcpy(code + 1, &index, 4); // Push the import identity.
    code[5] = 0xb8;
    const auto target = reinterpret_cast<std::uint32_t>(&missingImport);
    std::memcpy(code + 6, &target, 4); code[10] = 0xff; code[11] = 0xd0; code[12] = 0xcc;
    return code;
}

/** Locate a bundled export without treating its existence as executed behavior. */
void* findExport(const std::string& name) {
    for (const auto& image : images) {
        const auto symbol = image->exports.find(name);
        if (symbol != image->exports.end()) return image->base + symbol->second.value;
    }
    return nullptr;
}

/** Locate an export inside one named image (Apportable bridge libraries also export objc_* stubs). */
void* findExportIn(const char* imageName, const std::string& name) {
    for (const auto& image : images) {
        if (image->name != imageName) continue;
        const auto symbol = image->exports.find(name);
        if (symbol != image->exports.end()) return image->base + symbol->second.value;
    }
    return nullptr;
}

/** Human-readable image offset plus the nearest exported symbol, for fault attribution. */
std::string describeImageAddress(std::uintptr_t address) {
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uintptr_t>(image->base);
        if (address < base || address >= base + image->file.size()) continue;
        const auto offset = static_cast<std::uint32_t>(address - base);
        std::ostringstream text;
        text << image->name << "+0x" << std::hex << offset;
        std::uint32_t best = 0; const std::string* match = nullptr;
        for (const auto& entry : image->exports) {
            if (entry.second.value <= offset && entry.second.value >= best) { best = entry.second.value; match = &entry.first; }
        }
        if (match) text << " (" << *match << "+0x" << std::hex << (offset - best) << ")";
        return text.str();
    }
    std::ostringstream text; text << "0x" << std::hex << address; return text.str();
}

/** Fail loudly when a scenario needs an original export that this package lacks. */
void* requireExport(const char* name) {
    void* address = findExport(name);
    if (!address) throw std::runtime_error(std::string("Original export does not exist: ") + name);
    return address;
}

/** Resolve explicit host adapters first, then the original bundled libraries. */
void* resolve(const std::string& name) {
    if (const auto hook = hooks.find(name); hook != hooks.end()) return hook->second;
    if (void* original = findExport(name)) return original;
    void* trap = importTrap(name); hooks.emplace(name, trap); return trap;
}

/** Copy original loadable segments; no constructors or gameplay execute here. */
void mapImage(const std::filesystem::path& path) {
    auto image = std::make_unique<Image>(); image->name = path.filename().string();
    std::ifstream input(path, std::ios::binary);
    image->file.assign(std::istreambuf_iterator<char>(input), {});
    if (image->file.size() < sizeof(ElfHeader)) throw std::runtime_error("ELF header missing");
    const auto* header = reinterpret_cast<const ElfHeader*>(image->file.data());
    if (std::memcmp(header->ident, "\x7f" "ELF", 4) || header->ident[4] != 1 || header->machine != 3)
        throw std::runtime_error("Expected the original Intel ELF32 library");
    std::uint32_t size = 0;
    for (unsigned index = 0; index < header->phnum; ++index) {
        const auto* segment = reinterpret_cast<const ProgramHeader*>(image->file.data() + header->phoff + index * header->phsize);
        if (segment->type == LoadSegment) size = std::max(size, segment->vaddr + segment->memsz);
    }
    image->base = static_cast<unsigned char*>(VirtualAlloc(nullptr, (size + PageSize - 1) & ~(PageSize - 1), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!image->base) throw std::runtime_error("Native image mapping failed");
    for (unsigned index = 0; index < header->phnum; ++index) {
        const auto* segment = reinterpret_cast<const ProgramHeader*>(image->file.data() + header->phoff + index * header->phsize);
        if (segment->type == LoadSegment) std::memcpy(image->base + segment->vaddr, image->file.data() + segment->offset, segment->filesz);
    }
    for (unsigned index = 0; index < header->shnum; ++index)
        image->sections.push_back(*reinterpret_cast<const Section*>(image->file.data() + header->shoff + index * header->shsize));
    for (const auto& section : image->sections) {
        if (section.type != SymbolTable) continue;
        const auto& strings = image->sections.at(section.link);
        for (unsigned at = 0; at < section.size; at += sizeof(Symbol)) {
            const auto& symbol = *reinterpret_cast<const Symbol*>(image->file.data() + section.offset + at);
            if (symbol.name && symbol.section && (symbol.info >> 4)) image->exports.emplace(stringAt(*image, strings.offset + symbol.name), symbol);
        }
    }
    std::ostringstream message; message << "MAPPED " << image->name << " base=" << static_cast<void*>(image->base) << " exports=" << image->exports.size(); log(message.str());
    images.push_back(std::move(image));
}

/** Apply the actual ELF32 relocation kinds found in the supplied APK. */
void relocate(Image& image) {
    for (const auto& section : image.sections) {
        if (section.type != RelSection) continue;
        const auto& symbols = image.sections.at(section.link);
        const auto& strings = image.sections.at(symbols.link);
        for (unsigned at = 0; at < section.size; at += sizeof(Relocation)) {
            const auto& relocation = *reinterpret_cast<const Relocation*>(image.file.data() + section.offset + at);
            auto* destination = reinterpret_cast<std::uint32_t*>(image.base + relocation.offset);
            const unsigned type = relocation.info & 255, symbolIndex = relocation.info >> 8;
            std::uint32_t address = 0;
            if (symbolIndex) {
                const auto& symbol = *reinterpret_cast<const Symbol*>(image.file.data() + symbols.offset + symbolIndex * sizeof(Symbol));
                const std::string name = stringAt(image, strings.offset + symbol.name);
                address = reinterpret_cast<std::uint32_t>(resolve(name));
                if (symbol.section && (symbol.info >> 4) == 0) address = reinterpret_cast<std::uint32_t>(image.base + symbol.value);
            }
            switch (type) {
                case 0: break;
                case 1: *destination += address; break;
                case 2: *destination += address - reinterpret_cast<std::uint32_t>(destination); break;
                case 6: case 7: *destination = address; break;
                case 8: *destination += reinterpret_cast<std::uint32_t>(image.base); break;
                default: throw std::runtime_error("Unsupported ELF relocation " + std::to_string(type));
            }
        }
    }
}

/** Defined with the image helpers below; the class-resolution pass needs it earlier. */
void* imageBaseNamed(const char* name);

/** Defined with the image helpers below; the startup slot dump needs the same test. */
bool inGuestImage(const void* pointer);

/** Defined with the selector helpers below; the startup slot dump prints selector strings. */
std::string selectorName(const char* selector);

/**
 * Log the guest's own Objective-C data slots once at startup, so the address question is settled by observed
 * values instead of argument. Ghidra names carry +0x10000, so the class slot is at ELF VA 0x3DD690 and the
 * selector slots at 0x3DC124/0x3DC128; the older 0x2ED69x/0x2EC12x guesses are printed for contrast.
 */
void logObjectiveCSlots() {
    if (void* base = imageBaseNamed("libverde.so")) {
        auto* data = static_cast<unsigned char*>(base);
        for (const std::uint32_t offset : {0x2ed690u, 0x3dd690u, 0x2ec124u, 0x3dc124u, 0x2ec128u, 0x3dc128u}) {
            const std::uintptr_t value = *reinterpret_cast<std::uintptr_t*>(data + offset);
            std::ostringstream slot; slot << std::hex << offset;
            log("PLATFORM_OBJC_SLOT libverde+0x" + slot.str() + "=" + std::to_string(value)
                + " inImage=" + (inGuestImage(reinterpret_cast<void*>(value)) ? "yes" : "no"));
        }
        // Independent check of the two date selectors the SCORES path uses, from the guest's own slots.
        for (const std::uint32_t offset : {0x3dc144u, 0x3dc15cu}) {
            const auto value = *reinterpret_cast<std::uintptr_t*>(data + offset);
            std::string text = "(not in image)";
            if (inGuestImage(reinterpret_cast<void*>(value))) {
                try { text = selectorName(reinterpret_cast<const char*>(value)); } catch (...) { text = "(unreadable)"; }
            }
            std::ostringstream slot; slot << std::hex << offset;
            log("PLATFORM_OBJC_SELECTOR libverde+0x" + slot.str() + "=" + std::to_string(value) + " " + text);
        }
    }
}

/** Runtime class lookup that survives a runtime-side fault (kept object-free so SEH is legal here). */
void* guardedClassLookup(void* function, const char* name) {
    using ClassLookup = void*(__cdecl*)(const char*);
    __try { return reinterpret_cast<ClassLookup>(function)(name); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

/**
 * Objective-C class references are data symbols (`OBJC_CLASS_$_X`, `_OBJC_CLASS_$_X`, `OBJC_METACLASS_$_X`).
 * When the bundled libraries do not define them, the relocation keeps its link-time value and the original
 * then messages a pointer that is not an object; the real runtime faults while caching that message
 * (observed endgame signature: `[name UTF8String]` returns, then libSystem bcopy + access=0). The genuine
 * runtime is present, so ask it for the real class object and patch the slot with that value.
 */
void resolveObjectiveCClasses() {
    void* getClass = findExport("objc_getClass");
    void* getMetaClass = findExport("objc_getMetaClass");
    if (!getClass) { log("PLATFORM_OBJC_CLASS runtime lookup unavailable"); return; }
    log("PLATFORM_OBJC_PROBE getClass=" + describeImageAddress(reinterpret_cast<std::uintptr_t>(getClass))
        + " libSystem:getClass=" + describeImageAddress(reinterpret_cast<std::uintptr_t>(findExportIn("libSystem.so", "objc_getClass")))
        + " classSym=" + describeImageAddress(reinterpret_cast<std::uintptr_t>(findExport("OBJC_CLASS_$_NSString")))
        + " classSymF=" + describeImageAddress(reinterpret_cast<std::uintptr_t>(findExportIn("libFoundation.so", "OBJC_CLASS_$_NSString"))));
    if (void* base = imageBaseNamed("libverde.so")) {
        auto* data = static_cast<unsigned char*>(base);
        for (const std::uint32_t offset : {0x2ed690u, 0x3dd690u, 0x2ec124u, 0x3dc124u}) {
            const auto value = *reinterpret_cast<std::uintptr_t*>(data + offset);
            log("PLATFORM_OBJC_PROBE slot libverde+0x" + std::to_string(offset) + "=" + describeImageAddress(value));
        }
    }
    unsigned patched = 0, missing = 0, failed = 0, traced = 0;
    for (const auto& image : images) {
        for (const auto& section : image->sections) {
            if (section.type != RelSection) continue;
            const auto& symbols = image->sections.at(section.link);
            const auto& strings = image->sections.at(symbols.link);
            for (unsigned at = 0; at < section.size; at += sizeof(Relocation)) {
                const auto& relocation = *reinterpret_cast<const Relocation*>(image->file.data() + section.offset + at);
                const unsigned type = relocation.info & 255, symbolIndex = relocation.info >> 8;
                if (!symbolIndex) continue;
                const auto& symbol = *reinterpret_cast<const Symbol*>(image->file.data() + symbols.offset + symbolIndex * sizeof(Symbol));
                const std::string name = stringAt(*image, strings.offset + symbol.name);
                const bool meta = name.find("OBJC_METACLASS_$_") != std::string::npos;
                const std::size_t marker = name.find(meta ? "OBJC_METACLASS_$_" : "OBJC_CLASS_$_");
                if (marker == std::string::npos) continue;
                const std::string className = name.substr(marker + (meta ? 17 : 13));
                if (className.empty()) continue;
                void* cls = guardedClassLookup(meta && getMetaClass ? getMetaClass : getClass, className.c_str());
                if (traced < 24) { ++traced; log("PLATFORM_OBJC_CLASS lookup " + image->name + ":" + name + " -> " + std::to_string(reinterpret_cast<std::uintptr_t>(cls))); }
                if (!cls) { ++missing; continue; }
                auto* destination = reinterpret_cast<std::uint32_t*>(image->base + relocation.offset);
                const auto address = reinterpret_cast<std::uint32_t>(cls);
                if (type == 6 || type == 7) *destination = address;
                else if (type == 1) *destination += address;
                else continue;
                ++patched;
            }
        }
    }
    (void)failed;
    log("PLATFORM_OBJC_CLASS patched=" + std::to_string(patched) + " unresolved=" + std::to_string(missing));
}

/** Redirect only exported platform boundaries in the private loaded image. */void installHook(const std::string& name, void* replacement) {
    hooks[name] = replacement;
    for (const auto& image : images) {
        const auto symbol = image->exports.find(name);
        // Apportable's verified assembly dispatchers have STT_NOTYPE and size zero.
        // Apportable's verified assembly dispatchers have STT_NOTYPE and size zero, so the size/type gate
        // must not skip them. objc_msgSend_fpret belongs here too: without it the guest's
        // [NSDate timeIntervalSinceReferenceDate] reached the real (uninitialised) runtime and faulted
        // inside libSystem right after the endgame's [name UTF8String] message.
        const bool assemblyDispatcher = name == "objc_msgSend" || name == "objc_msgSend_fpret" || name == "objc_msgSend_stret";
        if (symbol == image->exports.end() || (!assemblyDispatcher && (symbol->second.size < 5 || (symbol->second.info & 15) != 2))) continue;
        auto* entry = image->base + symbol->second.value;
        entry[0] = 0xe9;
        const std::uint32_t offset = reinterpret_cast<std::uint32_t>(replacement) - reinterpret_cast<std::uint32_t>(entry + 5);
        std::memcpy(entry + 1, &offset, 4);
        log("PLATFORM_HOOK " + image->name + ":" + name);
    }
}

/** Preserve the cdecl argument stack while calling the driver's stdcall entry. */
void* driverBridge(void* target, unsigned words) {
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, PageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    unsigned position = 0;
    code[position++] = 0x55; code[position++] = 0x8b; code[position++] = 0xec;
    for (unsigned word = words; word; --word) {
        code[position++] = 0xff; code[position++] = 0xb5;
        const std::uint32_t displacement = 4 + word * 4;
        std::memcpy(code + position, &displacement, 4); position += 4;
    }
    code[position++] = 0xb8; const auto address = reinterpret_cast<std::uint32_t>(target);
    std::memcpy(code + position, &address, 4); position += 4;
    code[position++] = 0xff; code[position++] = 0xd0; code[position++] = 0x5d; code[position++] = 0xc3;
    return code;
}

/** Handle only this native experiment's close events through Windows. */
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (activeGame && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || (message == WM_MOUSEMOVE && (first & MK_LBUTTON)))) {
        hostTouch.x = static_cast<float>(static_cast<short>(LOWORD(second)));
        hostTouch.y = static_cast<float>(static_cast<short>(HIWORD(second)));
        const bool menuShowing = originalMenu && *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu) + 4) != 0;
        void* currentGame = originalMenu ? *reinterpret_cast<void**>(static_cast<unsigned char*>(originalMenu) + 0x15c) : activeGame;
        const char* phase = message == WM_LBUTTONDOWN ? "down" : message == WM_LBUTTONUP ? "up" : "move";
        if (message == WM_LBUTTONDOWN) SetCapture(window);
        if (message == WM_LBUTTONUP) ReleaseCapture();
        log(std::string("WINDOWS_POINTER ") + phase + " " + std::to_string(hostTouch.x) + "," + std::to_string(hostTouch.y));
        if (message == WM_LBUTTONUP) {
            // The original EAGLView IMP routes to MENU_handleTouchEnded or GAME_touchesEnded by itself.
            reinterpret_cast<void(__cdecl*)(void*, const char*, void*)>(images.front()->base + OriginalViewTouchEnd)(hostView.data(), "touchesEnded:withEvent:", &hostTouchSet);
        }
        else if (menuShowing) {
            const char* menuMethod = message == WM_LBUTTONDOWN ? "MENU_handleTouchBegan" : "MENU_handleTouchMoved";
            reinterpret_cast<void(__cdecl*)(void*, void*)>(findExport(menuMethod))(originalMenu, &hostTouchSet);
        }
        else if (currentGame) {
            const char* gameMethod = message == WM_LBUTTONDOWN ? "GAME_touchesBegan" : "GAME_touchesMoved";
            reinterpret_cast<void(__cdecl*)(void*, void*)>(findExport(gameMethod))(currentGame, &hostTouchSet);
        }
        return 0;
    }
    if (activeGame && message == WM_KEYDOWN && first == VK_SPACE) {
        void* currentGame = originalMenu ? *reinterpret_cast<void**>(static_cast<unsigned char*>(originalMenu) + 0x15c) : activeGame;
        if (currentGame) {
            log("WINDOWS_KEY Space -> original GAME_sendNextWave");
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("GAME_sendNextWave"))(currentGame);
        }
        return 0;
    }
    if (activeGame && message == WM_KEYDOWN && first == VK_ESCAPE && originalMenu) {
        // Original semantics: running game -> in-progress main menu; level select -> previous page.
        void* currentGame = *reinterpret_cast<void**>(static_cast<unsigned char*>(originalMenu) + 0x15c);
        const void* page = *reinterpret_cast<void**>(originalMenu);
        if (currentGame) {
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_backToMainMenu"))(originalMenu);
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_show"))(originalMenu);
        } else if (page && page == findExport("startGamePage")) {
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MAPMENU_backButton"))(originalMenu);
        }
        log("WINDOWS_KEY Escape -> original menu back");
        return 0;
    }
    // Interactive menu mode: F5 saves through the original OBS path, F9 restores it.
    if (activeGame && message == WM_KEYDOWN && originalMenu && (first == VK_F5 || first == VK_F9)) {
        log(std::string("WINDOWS_KEY ") + (first == VK_F5 ? "F5 save" : "F9 continue"));
        if (first == VK_F5) hotkeySaveState(originalMenu); else hotkeyLoadState(originalMenu);
        return 0;
    }
    return DefWindowProcW(window, message, first, second);
}

/** Create a desktop compatibility context for the original fixed-function renderer. */
void createGraphics() {
    WNDCLASSW definition{}; definition.lpfnWndProc = windowProcedure; definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpszClassName = L"TowerMadnessOriginalX86Probe"; definition.style = CS_OWNDC;
    RegisterClassW(&definition);
    // This first native prototype keeps the recovered portrait geometry fixed.
    constexpr DWORD WindowStyle = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    RECT bounds{0, 0, 480, 800}; AdjustWindowRect(&bounds, WindowStyle, FALSE);
    windowHandle = CreateWindowW(definition.lpszClassName, L"TowerMadness original x86 experiment", WindowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, definition.hInstance, nullptr);
    windowDc = GetDC(windowHandle);
    PIXELFORMATDESCRIPTOR format{}; format.nSize = sizeof(format); format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32; format.cDepthBits = 24; format.cStencilBits = 8;
    const int chosen = ChoosePixelFormat(windowDc, &format);
    if (!chosen || !SetPixelFormat(windowDc, chosen, &format)) throw std::runtime_error("OpenGL pixel format failed");
    windowGl = wglCreateContext(windowDc);
    if (!windowGl || !wglMakeCurrent(windowDc, windowGl)) throw std::runtime_error("Original renderer context failed");
    log(std::string("NATIVE_OPENGL ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)));
}

/** Preserve GLES float projection semantics through desktop double entry points. */
void __cdecl nativeOrthof(float left, float right, float bottom, float top, float nearValue, float farValue) {
    glOrtho(left, right, bottom, top, nearValue, farValue);
}

/** Convert the GLES frustum ABI without changing the original camera values. */
void __cdecl nativeFrustumf(float left, float right, float bottom, float top, float nearValue, float farValue) {
    glFrustum(left, right, bottom, top, nearValue, farValue);
}

/** Bind real OpenGL entries and redirect the original Apportable platform wrappers. */
void registerGraphics(const std::filesystem::path& path) {
    std::ifstream input(path); const auto signatures = nlohmann::json::parse(input);
    const HMODULE driver = GetModuleHandleW(L"opengl32.dll");
    unsigned count = 0;
    for (auto entry = signatures.begin(); entry != signatures.end(); ++entry) {
        const std::string name = entry.key();
        PROC function = GetProcAddress(driver, name.c_str());
        if (!function) function = wglGetProcAddress(name.c_str());
        const auto address = reinterpret_cast<std::uintptr_t>(function);
        if (!function || address <= 3 || address == static_cast<std::uintptr_t>(-1)) continue;
        void* bridge = driverBridge(reinterpret_cast<void*>(function), entry.value().get<unsigned>());
        hooks[name] = bridge;
        const std::string wrapper = "__wrap_" + name;
        if (findExport(wrapper)) installHook(wrapper, bridge);
        ++count;
    }
    log("NATIVE_GL_BRIDGES " + std::to_string(count));
    hooks["glOrthof"] = reinterpret_cast<void*>(&nativeOrthof); hooks["glFrustumf"] = reinterpret_cast<void*>(&nativeFrustumf);
    installHook("__wrap_glOrthof", reinterpret_cast<void*>(&nativeOrthof));
    installHook("__wrap_glFrustumf", reinterpret_cast<void*>(&nativeFrustumf));
}

/** Lock a native Windows mutex keyed by the original opaque Bionic address. */
int __cdecl mutexLock(void* address) {
    auto& mutex = mutexes[address];
    if (!mutex) { mutex = std::make_unique<CRITICAL_SECTION>(); InitializeCriticalSection(mutex.get()); }
    EnterCriticalSection(mutex.get()); return 0;
}

/** Unlock the same Windows mutex used for an original Bionic lock. */
int __cdecl mutexUnlock(void* address) {
    if (const auto mutex = mutexes.find(address); mutex != mutexes.end()) LeaveCriticalSection(mutex->second.get());
    return 0;
}

/** Initialize an opaque guest mutex by creating its Windows equivalent. */
int __cdecl mutexInit(void* address, const void*) { mutexLock(address); return mutexUnlock(address); }

/** Release native mutex storage when the original library destroys its mutex. */
int __cdecl mutexDestroy(void* address) {
    if (const auto mutex = mutexes.find(address); mutex != mutexes.end()) { DeleteCriticalSection(mutex->second.get()); mutexes.erase(mutex); }
    return 0;
}

/** Redirect verified absolute GS references in private mappings, never APK bytes. */
void redirectGs(const std::filesystem::path& path) {
    std::ifstream input(path);
    auto entries = nlohmann::json::parse(input);
    guestTls[5] = 0x746d7878;
    unsigned count = 0;
    for (const auto& entry : entries) {
        if (!entry.value("canRedirect", false)) continue;
        const std::string library = entry.at("library");
        const auto image = std::find_if(images.begin(), images.end(), [&](const auto& candidate) { return candidate->name == library; });
        if (image == images.end()) continue;
        const auto offset = entry.at("va").get<std::uint32_t>();
        const auto slot = entry.at("slot").get<unsigned>();
        if (slot + 4 > sizeof(guestTls)) throw std::runtime_error("Unexpected guest TLS slot");
        auto* instruction = (*image)->base + offset;
        const auto prefix = entry.at("segmentPrefixOffset").get<unsigned>();
        const auto displacement = entry.at("displacementOffset").get<unsigned>();
        if (instruction[prefix] != 0x65) throw std::runtime_error("GS instruction identity mismatch");
        instruction[prefix] = 0x90;
        const auto replacement = reinterpret_cast<std::uint32_t>(guestTls.data()) + slot;
        std::memcpy(instruction + displacement, &replacement, 4); ++count;
    }
    log("SINGLE_THREAD_GS_ADAPTATIONS " + std::to_string(count));
}

/** Read original APK assets through the recovered VFS ABI and heap ownership. */
int __cdecl nativeFile(const char* name, const char* extension, char** data, int* length) {
    std::string filename = name;
    if (extension && *extension) filename += "." + std::string(extension);
    const auto path = assetsDirectory / filename;
    std::ifstream input(path, std::ios::binary);
    if (!input) { log("ASSET_MISSING " + filename); return 0; }
    std::string contents(std::istreambuf_iterator<char>(input), {});
    *length = static_cast<int>(contents.size());
    *data = static_cast<char*>(std::malloc(contents.size() + 1));
    std::memcpy(*data, contents.c_str(), contents.size() + 1);
    log("ORIGINAL_ASSET " + filename + " bytes=" + std::to_string(*length));
    return 1;
}

// Original file-list nodes hold basename, extension, and next in this exact ABI.
struct FileNode { char* name; char* extension; FileNode* next; };

/** List immutable original assets using the original linked-list representation. */
FileNode* __cdecl nativeFiles(const char* extension) {
    FileNode* first = nullptr; FileNode** tail = &first;
    for (const auto& file : std::filesystem::directory_iterator(assetsDirectory)) {
        if (file.path().extension().string() != "." + std::string(extension)) continue;
        auto* node = static_cast<FileNode*>(std::malloc(sizeof(FileNode)));
        node->name = _strdup(file.path().stem().string().c_str()); node->extension = _strdup(extension); node->next = nullptr;
        *tail = node; tail = &node->next;
    }
    return first;
}

/** Upload the same packaged PNG through Windows WIC instead of NSBundle/UIKit. */
int __cdecl nativePng(const char* name) {
    if (!name) return 0;
    std::filesystem::path filename = std::string(name) + ".pvr.png";
    if (!std::filesystem::exists(convertedTextures / filename)) filename = std::string(name) + ".png";
    const auto path = std::filesystem::exists(convertedTextures / filename) ? convertedTextures / filename : assetsDirectory / filename;
    IWICImagingFactory* factory = nullptr; IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* converter = nullptr;
    HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(status)) status = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(status)) status = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(status)) status = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(status)) status = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    UINT width = 0, height = 0;
    if (SUCCEEDED(status)) status = converter->GetSize(&width, &height);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    if (SUCCEEDED(status)) status = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (SUCCEEDED(status)) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (auto mipmap = reinterpret_cast<void(APIENTRY*)(GLenum)>(wglGetProcAddress("glGenerateMipmap"))) mipmap(GL_TEXTURE_2D);
        log("PNG_UPLOAD " + filename.string() + " " + std::to_string(width) + "x" + std::to_string(height));
    } else log("PNG_DECODE_FAILED " + path.string() + " hr=" + std::to_string(status));
    if (converter) converter->Release(); if (frame) frame->Release(); if (decoder) decoder->Release(); if (factory) factory->Release();
    return SUCCEEDED(status) ? 1 : 0;
}

/** Preserve the original localization key when platform localization is unavailable. */
const char* __cdecl nativeTranslate(const char* name) { return name; }

/** Keep sound handles stable while the authorized silent probe has no audio device. */
int __cdecl silentSound(const char* name) {
    static std::map<std::string, int> ids;
    auto [entry, inserted] = ids.emplace(name ? name : "", static_cast<int>(ids.size()));
    return entry->second;
}

/** Honor the project-wide silent setting at the platform playback boundary. */
void __cdecl silentPlayback() {}

/** Route the original Menu hot zone to a host pause until the original MENU object is connected. */
void __cdecl hostMenuButton() {
    hostPauseRequested = hostPauseRequested ? 0 : 1;
    // HOST_PAUSE is an explicit adaptation and must never be reported as the original menu.
    log(hostPauseRequested
        ? "HOST_PAUSE Original menu button pressed; GAME_update is withheld until the next press."
        : "HOST_RESUME GAME_resume returns the original simulation clock.");
}

/** Resolve a selector name whether the caller passed the string or its relocated slot. */
std::string selectorName(const char* selector) {
    const auto printable = [](const char* text) {
        if (!text) return false;
        for (int index = 0; index < 64; ++index) {
            const unsigned char value = static_cast<unsigned char>(text[index]);
            if (value == 0) return index > 0;
            if (value < 32 || value > 126) return false;
        }
        return true;
    };
    if (printable(selector)) return selector;
    const auto address = reinterpret_cast<std::uintptr_t>(selector);
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uintptr_t>(image->base);
        if (address >= base && address + 4 <= base + image->file.size()) {
            const char* indirect = *reinterpret_cast<const char* const*>(selector);
            if (printable(indirect)) return indirect;
            break;
        }
    }
    return "(unknown)";
}

// Minimal host-side platform objects. The original code only passes these back into the
// same handlers (GameAccount key/value sync), so real values are kept instead of nulls.
struct HostString { char text[512]; };
// Monotonic storage: original code keeps references to these tokens, so slots must never be recycled.
std::deque<HostString> hostStrings;
// Minimal real UIAlertView objects for the original endgame/alert call sites.
struct HostAlert { char title[256]; char message[256]; int buttonCount = 0; };
HostAlert hostAlerts[8];
int hostAlertCount = 0;
// Minimal real Foundation containers used by the original endgame score path (never a generic token).
struct HostContainer {
    char kind = 0;                   // 'A' array, 'D' dictionary, 'N' number, 'T' date, 'B' data
    std::vector<void*> items;        // array items / dictionary values (host object tokens)
    std::vector<std::string> keys;   // dictionary keys
    std::string text;                // number/date value
    std::vector<unsigned char> data; // NSData bytes
    unsigned long mutations = 0;
    std::vector<void*> enumeration;  // stable token array handed to fast enumeration
    bool sliceGiven = false;
};
std::deque<HostContainer> hostContainers;
std::map<std::string, int> hostAccountInts;
std::map<std::string, std::string> hostAccountStrings;
std::map<std::string, double> hostAccountFloats;
// Restored GAME subsystems may keep pointers into the IBS bytes, so the buffer must outlive the load.
std::vector<std::uint8_t> loadedIbsBytes;
// NSUserDefaults surface and the on-disk platform store (real content keys, persisted across processes).
int hostUserDefaultsToken = 0;
std::filesystem::path persistedStorePath;
std::filesystem::path dataDirectory;
// A game replaced mid-frame is freed at the next frame boundary: original render/update code can still
// hold the old pointer during the frame in which the swap happens.
void* pendingGameFree = nullptr;
bool persistedStoreLoaded = false;
void loadPersistedStore();
void savePersistedStore();

/** True when the pointer is inside a mapped original image and reads as a short printable C string. */
bool printableCString(const char* text) {
    if (!text) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(text);
    bool mapped = false;
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uintptr_t>(image->base);
        if (address >= base && address < base + image->file.size()) { mapped = true; break; }
    }
    if (!mapped) return false;
    for (int index = 0; index < 128; ++index) {
        const unsigned char value = static_cast<unsigned char>(text[index]);
        if (value == 0) return index > 0;
        if (value < 32 || value > 126) return false;
    }
    return true;
}

/** True when the whole range is committed and readable in this process. */
bool readableBytes(const void* value, std::size_t count) {
    if (!value) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(value, &info, sizeof(info)) == 0) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(value);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return start + count <= regionEnd;
}

/**
 * Read printable text from a pointer. Original rodata strings and guest malloc'd names are both
 * accepted, using a bounded page-guarded scan so an invalid pointer can never fault the probe.
 */
bool tryReadText(const void* value, std::string& out) {
    const char* text = static_cast<const char*>(value);
    if (!text) return false;
    if (printableCString(text)) { out = text; return true; }
    out.clear();
    for (std::size_t index = 0; index < 512; ++index) {
        if (!readableBytes(text + index, 1)) return false;
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte == 0) return !out.empty();
        if (byte < 32 || byte > 126) return false;
        out.push_back(static_cast<char>(byte));
    }
    return !out.empty();
}

/** Create the next host string wrapper holding real UTF-8 bytes. */
HostString* nextHostString() {
    hostStrings.emplace_back();
    return &hostStrings.back();
}

/** Wrap real UTF-8 bytes in a host string object. */
HostString* copyHostText(const std::string& text) {
    HostString* value = nextHostString();
    std::snprintf(value->text, sizeof(value->text), "%s", text.c_str());
    return value;
}

/**
 * Decode an original CFString/NSString to its C-string bytes. Constants use either {isa, cstr} or
 * {isa, flags, cstr, length}; heap NSStrings keep the same pointer slots. Both are probed safely.
 */
const char* cfStringBytes(const void* value) {
    std::string direct;
    if (tryReadText(value, direct)) return reinterpret_cast<const char*>(value);
    for (const int offset : {8, 4}) {
        if (!readableBytes(static_cast<const char*>(value) + offset, 4)) continue;
        const auto candidate = *reinterpret_cast<const char* const*>(static_cast<const char*>(value) + offset);
        std::string text;
        if (tryReadText(candidate, text)) return candidate;
    }
    return nullptr;
}

/** Decode text from any original string-ish pointer (CFString constant, host string, or C string). */
std::string decodedText(const void* value) {
    if (const char* bytes = cfStringBytes(value)) return bytes;
    std::string text; tryReadText(value, text); return text;
}

/**
 * Decode an ObjC class/instance name from original metadata. The old ABI stores the class name as a
 * C-string pointer inside the class object; instances reach it through their isa.
 */
std::string objcClassName(const void* object) {
    if (!object) return "(nil)";
    // Only hand real guest-image pointers to the original runtime: host tokens are not ObjC objects and
    // object_getClass/class_getName would dereference them as one (observed as a null access inside libSystem).
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    bool inGuestImage = false;
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uintptr_t>(image->base);
        if (address >= base && address < base + image->file.size()) { inGuestImage = true; break; }
    }
    if (inGuestImage) {
        // Prefer the original runtime's own class-name API over guessing the class struct layout.
        if (const auto classGetName = reinterpret_cast<const char*(__cdecl*)(const void*)>(findExport("class_getName"))) {
            const void* cls = object;
            if (const auto objectGetClass = reinterpret_cast<void*(__cdecl*)(const void*)>(findExport("object_getClass")))
                if (void* isa = objectGetClass(object)) cls = isa;
            std::string text;
            if (tryReadText(classGetName(cls), text) && !text.empty() && text.size() <= 96) return text;
        }
    }
    const auto probe = [](const void* base) -> std::string {
        for (const int offset : {8, 4, 0xc, 0x10, 0x14, 0x18, 0}) {
            if (!readableBytes(static_cast<const char*>(base) + offset, 4)) continue;
            const char* candidate = *reinterpret_cast<const char* const*>(static_cast<const char*>(base) + offset);
            std::string text;
            if (!tryReadText(candidate, text) || text.empty() || text.size() > 96) continue;
            const char first = text.front();
            if ((first >= 'A' && first <= 'Z') || first == '_') return text;
        }
        return std::string();
    };
    std::string name = probe(object);
    if (!name.empty()) return name;
    const void* isa = readableBytes(object, 4) ? *reinterpret_cast<const void* const*>(object) : nullptr;
    if (isa && isa != object) { name = probe(isa); if (!name.empty()) return name; }
    return "(unknown class)";
}

/** Map a native code address to image+offset for evidence logs. */
std::string nativeCaller(void* address) {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uintptr_t>(image->base);
        if (value >= base && value < base + image->file.size()) {
            std::ostringstream text; text << image->name << "+0x" << std::hex << (value - base); return text.str();
        }
    }
    std::ostringstream text; text << "0x" << std::hex << value; return text.str();
}

/**
 * Key text for the account store. Decodes host strings and CFString constants by content;
 * undecodable mapped objects fall back to a stable image-relative tag (never a raw process pointer).
 */
/** Defined with the image helpers below; declared here because key decoding needs the same test. */
bool inGuestImage(const void* pointer);

std::string hostKeyText(const void* value) {
    for (const auto& candidate : hostStrings)
        if (value == &candidate) return candidate.text;
    if (const char* text = cfStringBytes(value)) return text;
    // An object that is not a host wrapper (for example an original CFString/NSString constant) still owns its
    // bytes; ask the dispatch entry for them. Note the guest's own [NSString alloc] initWithCString: goes through
    // the hooked entry and yields a host string, so this branch only serves genuinely original objects. Guarded
    // by an isa check so a plain C string inside an image is never messaged.
    if (inGuestImage(value) && inGuestImage(*reinterpret_cast<void* const*>(value))) {
        static void* messageSend = nullptr;
        static void* selUtf8String = nullptr;
        if (!messageSend) messageSend = findExport("objc_msgSend");
        if (!selUtf8String) {
            if (const auto registerName = reinterpret_cast<const char*(__cdecl*)(const char*)>(findExport("sel_registerName")))
                selUtf8String = const_cast<char*>(registerName("UTF8String"));
        }
        if (messageSend && selUtf8String) {
            const auto send = reinterpret_cast<const char*(*)(const void*, void*, ...)>(messageSend);
            std::string decoded;
            if (tryReadText(send(value, selUtf8String), decoded) && !decoded.empty()) return decoded;
        }
    }
    const auto address = reinterpret_cast<std::uintptr_t>(value);
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uintptr_t>(image->base);
        if (address >= base && address < base + image->file.size()) {
            std::ostringstream tag; tag << image->name << "+0x" << std::hex << (address - base); return tag.str();
        }
    }
    char tag[32]; std::snprintf(tag, sizeof(tag), "heap:%p", value); return tag;
}

/** Expand the small NSString format subset the original menu and score keys use. */
std::string formatNSString(const char* format, va_list arguments) {
    std::string out;
    for (const char* cursor = format; cursor && *cursor;) {
        if (*cursor != '%') { out.push_back(*cursor++); continue; }
        ++cursor;
        if (*cursor == '%') { out.push_back('%'); ++cursor; continue; }
        while (*cursor == 'l' || *cursor == 'h') ++cursor;
        const char specifier = *cursor ? *cursor++ : 0;
        if (specifier == '@') { out += hostKeyText(va_arg(arguments, const void*)); }
        else if (specifier == 's') { const char* text = va_arg(arguments, const char*); std::string value; if (tryReadText(text, value)) out += value; }
        else if (specifier == 'd' || specifier == 'i') { out += std::to_string(va_arg(arguments, int)); }
        else if (specifier) { out.push_back('%'); out.push_back(specifier); }
    }
    return out;
}

// ---- Real Foundation containers for the score serializer ------------------------------------

/** Token for an arbitrary original object; existing host objects keep their identity. */
void* tokenForObject(const void* object) {
    if (!object) return nullptr;
    for (auto& container : hostContainers) if (object == &container) return &container;
    for (auto& string : hostStrings) if (object == &string) return &string;
    return copyHostText(decodedText(object));
}

HostContainer* newHostContainer() {
    hostContainers.emplace_back();
    return &hostContainers.back();
}

/** True when a pointer lies inside one of the loaded original images (never true for host tokens). */
bool inGuestImage(const void* pointer) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uintptr_t>(image->base);
        if (address >= base && address < base + image->file.size()) return true;
    }
    return false;
}

/** Base address of a loaded original image (the layout keeps VA == offset for the first segment). */
void* imageBaseNamed(const char* name) {
    for (const auto& image : images) if (image->name == name) return image->base;
    return nullptr;
}

/**
 * Ask the guest's own NSString class slot for an object. Note (independently reviewed and confirmed): the
 * dispatch entry `findExport("objc_msgSend")` is the very entry installHook patched with an E9 jump, so the
 * nested sends re-enter nativeMessage and the result is a host string managed by the hook, NOT a real runtime
 * NSString. That is sufficient here because every consumer on this path goes through the same hook; the log and
 * this comment say so instead of overstating a real-runtime object.
 */
void* originalNSString(const std::string& text) {
    static void* messageSend = nullptr;
    static void* stringClass = nullptr;
    static void* selAlloc = nullptr;
    static void* selInitWithCString = nullptr;
    static bool loggedBadPointers = false;
    if (!messageSend) messageSend = findExport("objc_msgSend");
    if (!stringClass) {
        // Never call the runtime's own lookup APIs from the host: objc_getClass/sel_registerName fault inside
        // libSystem here (observed libSystem.so+0x42744, frames 0x4bf7b/0x4afb4/0x4f23a) because the host
        // never runs library constructors, so the runtime class table is not initialised. Read the guest's
        // own relocated constant slots instead: libverde's Ghidra names carry +0x10000, so
        // PTR_OBJC_CLASS___NSString_003ed690 is ELF VA 0x3DD690 and the selectors are 0x3DC124/0x3DC128.
        if (void* base = imageBaseNamed("libverde.so")) {
            auto* data = static_cast<unsigned char*>(base);
            stringClass = *reinterpret_cast<void**>(data + 0x3dd690);
            selAlloc = *reinterpret_cast<void**>(data + 0x3dc124);
            selInitWithCString = *reinterpret_cast<void**>(data + 0x3dc128);
        }
    }
    if (!messageSend || !stringClass || !selAlloc || !selInitWithCString) return nullptr;
    if (!inGuestImage(messageSend) || !inGuestImage(stringClass) || !inGuestImage(selAlloc) || !inGuestImage(selInitWithCString)) {
        if (!loggedBadPointers) {
            loggedBadPointers = true;
            log("PLATFORM_ORIGINAL_STRING rejected non-image pointers msgSend=" + std::to_string(reinterpret_cast<std::uintptr_t>(messageSend))
                + " class=" + std::to_string(reinterpret_cast<std::uintptr_t>(stringClass)));
        }
        return nullptr;
    }
    static bool loggedPointers = false;
    if (!loggedPointers) {
        loggedPointers = true;
        std::string allocText = "(unreadable)", initText = "(unreadable)";
        try { allocText = selectorName(static_cast<const char*>(selAlloc)); } catch (...) {}
        try { initText = selectorName(static_cast<const char*>(selInitWithCString)); } catch (...) {}
        log("PLATFORM_ORIGINAL_STRING msgSend=" + std::to_string(reinterpret_cast<std::uintptr_t>(messageSend))
            + " class=" + std::to_string(reinterpret_cast<std::uintptr_t>(stringClass))
            + " selAlloc=" + std::to_string(reinterpret_cast<std::uintptr_t>(selAlloc))
            + " selInit=" + std::to_string(reinterpret_cast<std::uintptr_t>(selInitWithCString))
            + " selAllocText=" + allocText + " selInitText=" + initText);
    }
    auto send = reinterpret_cast<void*(*)(void*, void*, ...)>(messageSend);
    void* allocated = send(stringClass, selAlloc);
    if (!allocated) return nullptr;
    return send(allocated, selInitWithCString, text.c_str(), 1);
}

nlohmann::json tokenToJson(const void* token);
void* tokenFromJson(const nlohmann::json& value);

nlohmann::json containerToJson(const HostContainer& container) {
    nlohmann::json out{{"k", std::string(1, container.kind)}};
    if (container.kind == 'A' || container.kind == 'D') {
        nlohmann::json entries = nlohmann::json::array();
        for (std::size_t index = 0; index < container.items.size(); ++index) {
            nlohmann::json entry{{"v", tokenToJson(container.items[index])}};
            if (container.kind == 'D' && index < container.keys.size()) entry["n"] = container.keys[index];
            entries.push_back(entry);
        }
        out["v"] = entries;
    } else if (container.kind == 'B') {
        static const char* digits = "0123456789abcdef";
        std::string hex; hex.reserve(container.data.size() * 2);
        for (unsigned char byte : container.data) { hex.push_back(digits[byte >> 4]); hex.push_back(digits[byte & 0xf]); }
        out["v"] = hex;
    } else {
        out["v"] = container.text;
    }
    return out;
}

nlohmann::json tokenToJson(const void* token) {
    if (!token) return nlohmann::json{{"k", "0"}};
    for (const auto& container : hostContainers)
        if (token == &container) return containerToJson(container);
    for (const auto& string : hostStrings)
        if (token == &string) return nlohmann::json{{"k", "S"}, {"v", std::string(string.text)}};
    const char* text = cfStringBytes(token);
    return nlohmann::json{{"k", "S"}, {"v", text ? std::string(text) : std::string()}};
}

void* tokenFromJson(const nlohmann::json& value) {
    const std::string kind = value.value("k", std::string("0"));
    if (kind.empty() || kind == "0") return nullptr;
    if (kind == "S") return copyHostText(value.value("v", std::string()));
    HostContainer* container = newHostContainer();
    container->kind = kind[0];
    if (container->kind == 'A' || container->kind == 'D') {
        for (const auto& entry : value.value("v", nlohmann::json::array())) {
            container->items.push_back(tokenFromJson(entry.value("v", nlohmann::json::object())));
            if (container->kind == 'D') container->keys.push_back(entry.value("n", std::string()));
        }
    } else if (container->kind == 'B') {
        const std::string hex = value.value("v", std::string());
        for (std::size_t index = 0; index + 1 < hex.size(); index += 2)
            container->data.push_back(static_cast<unsigned char>(std::stoi(hex.substr(index, 2), nullptr, 16)));
    } else {
        container->text = value.value("v", std::string());
    }
    return container;
}

std::string serializeContainer(const HostContainer& container) { return "OBJ1:" + containerToJson(container).dump(); }

bool deserializeContainer(const std::string& text, void** tokenOut) {
    if (text.rfind("OBJ1:", 0) != 0) return false;
    try { *tokenOut = tokenFromJson(nlohmann::json::parse(text.substr(5))); return true; }
    catch (const std::exception&) { *tokenOut = nullptr; return false; }
}

/** Implement only the pointer and platform selectors with known call-site semantics. */
std::uint32_t __cdecl nativeMessage(void* receiver, const char* selector, ...) {
    // Objective-C messages to nil have a defined zero result.
    if (!receiver) return 0;
    const std::string name = selectorName(selector);
    // Endgame-path tracing: which messages actually reach the host hook, on which receiver.
    if (name == "name" || name == "UTF8String" || name == "isGameCenter" || name == "reportScore:modename:"
        || name == "deleteSavegameForMode:" || name == "timeIntervalSinceReferenceDate") {
        log("PLATFORM_MSG " + name + " receiver=" + std::to_string(reinterpret_cast<std::uintptr_t>(receiver)));
    }
    loadPersistedStore();
    // Music-only messages: the probe is expressly silent.
    if (name == "resetTimeForTune:" || name == "playMusic:loop:") return 0;
    // The host pointer objects retain the original NSSet/UITouch contract.
    if (receiver == &hostTouchSet) {
        if (name == "count") return 1;
        if (name == "anyObject") return reinterpret_cast<std::uint32_t>(&hostTouch);
    }
    if (receiver == &hostTouch) {
        if (name == "view") return reinterpret_cast<std::uint32_t>(hostView.data());
        if (name == "tapCount") return 1;
    }
    // Class-specific real allocation: only the alert class the original endgame path uses.
    if (name == "alloc" || name == "allocWithZone:") {
        const std::string alertClass = objcClassName(receiver);
        if (alertClass == "UIAlertView" || alertClass == "UIAlertController") {
            HostAlert* alert = &hostAlerts[hostAlertCount++ % (sizeof(hostAlerts) / sizeof(hostAlerts[0]))];
            *alert = HostAlert{};
            log("PLATFORM_ALERT alloc class=" + alertClass);
            return reinterpret_cast<std::uint32_t>(alert);
        }
        // Real NSString allocation: the endgame SCORES path builds string objects right after reading defaults.
        if (alertClass == "NSString" || alertClass == "NSMutableString" || alertClass == "NSConstantString") {
            HostString* text = nextHostString();
            log("PLATFORM_STRING alloc class=" + alertClass);
            return reinterpret_cast<std::uint32_t>(text);
        }
        if (alertClass == "NSMutableArray" || alertClass == "NSArray" || alertClass == "NSMutableDictionary"
            || alertClass == "NSDictionary") {
            HostContainer* container = newHostContainer();
            container->kind = (alertClass == "NSMutableArray" || alertClass == "NSArray") ? 'A' : 'D';
            log("PLATFORM_CONTAINER alloc class=" + alertClass);
            return reinterpret_cast<std::uint32_t>(container);
        }
        log("UNIMPLEMENTED_OBJC_SELECTOR " + name + " receiver=" + std::to_string(reinterpret_cast<std::uintptr_t>(receiver))
            + " class=" + alertClass + " caller=" + nativeCaller(_ReturnAddress()));
        ExitProcess(31);
    }
    // Minimal real UIAlertView surface (headless): store content, count buttons, never open a window.
    for (auto& alert : hostAlerts) {
        if (receiver != &alert) continue;
        if (name == "initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:") {
            va_list arguments; va_start(arguments, selector);
            const void* title = va_arg(arguments, const void*);
            const void* message = va_arg(arguments, const void*);
            va_arg(arguments, const void*); // delegate
            const void* cancel = va_arg(arguments, const void*);
            va_end(arguments);
            const std::string titleText = decodedText(title), messageText = decodedText(message), cancelText = decodedText(cancel);
            std::snprintf(alert.title, sizeof(alert.title), "%s", titleText.c_str());
            std::snprintf(alert.message, sizeof(alert.message), "%s", messageText.c_str());
            log("PLATFORM_ALERT init title=" + titleText + " message=" + messageText + " cancel=" + cancelText);
            return reinterpret_cast<std::uint32_t>(&alert);
        }
        if (name == "show") { log("PLATFORM_ALERT show title=" + std::string(alert.title)); return 0; }
        if (name == "addButtonWithTitle:") { ++alert.buttonCount; return 0; }
        if (name == "release" || name == "dealloc" || name == "dismissWithClickedButtonIndex:animated:"
            || name == "setTitle:" || name == "setMessage:" || name == "setDelegate:") return 0;
        log("PLATFORM_ALERT_UNHANDLED " + name);
        ExitProcess(31);
    }
    // Minimal real array/dictionary/value objects used by the original score serializer.
    for (auto& container : hostContainers) {
        if (receiver != &container) continue;
        if (name == "init" || name == "initWithCapacity:" || name == "initWithObjects:" || name == "initWithObjects:forKeys:"
            || name == "retain") {
            return reinterpret_cast<std::uint32_t>(&container);
        }
        if (name == "addObject:" || name == "addObjectsFromArray:") {
            va_list arguments; va_start(arguments, selector);
            const void* value = va_arg(arguments, const void*); va_end(arguments);
            container.items.push_back(tokenForObject(value));
            ++container.mutations; container.sliceGiven = false;
            return 0;
        }
        if (name == "setObject:forKey:" || name == "setValue:forKey:") {
            va_list arguments; va_start(arguments, selector);
            const void* value = va_arg(arguments, const void*);
            const void* key = va_arg(arguments, const void*); va_end(arguments);
            container.items.push_back(tokenForObject(value));
            container.keys.push_back(hostKeyText(key));
            ++container.mutations; container.sliceGiven = false;
            return 0;
        }
        if (name == "count") return static_cast<std::uint32_t>(container.items.size());
        if (name == "countByEnumeratingWithState:objects:count:") {
            va_list arguments; va_start(arguments, selector);
            auto* state = va_arg(arguments, unsigned long*);
            va_arg(arguments, void**);
            va_arg(arguments, unsigned long);
            va_end(arguments);
            if (container.sliceGiven || container.items.empty()) { container.sliceGiven = false; return 0; }
            container.enumeration = container.items;
            if (state) {
                // i386 NSFastEnumerationState: [0]=state, [1]=itemsPtr, [2]=mutationsPtr. The original reads
                // state[2] and dereferences it to detect mutation, so it must always point at a live counter.
                state[0] = 0;
                state[1] = reinterpret_cast<unsigned long>(container.enumeration.data());
                state[2] = reinterpret_cast<unsigned long>(&container.mutations);
            }
            container.sliceGiven = true;
            return static_cast<std::uint32_t>(container.enumeration.size());
        }
        if (name == "objectAtIndex:" || name == "objectAtIndexedSubscript:") {
            va_list arguments; va_start(arguments, selector);
            const unsigned index = static_cast<unsigned>(va_arg(arguments, int)); va_end(arguments);
            return index < container.items.size() ? reinterpret_cast<std::uint32_t>(container.items[index]) : 0u;
        }
        if (name == "objectForKey:" || name == "valueForKey:") {
            va_list arguments; va_start(arguments, selector);
            const void* key = va_arg(arguments, const void*); va_end(arguments);
            const std::string wanted = hostKeyText(key);
            for (std::size_t index = 0; index < container.keys.size() && index < container.items.size(); ++index)
                if (container.keys[index] == wanted) return reinterpret_cast<std::uint32_t>(container.items[index]);
            return 0;
        }
        if (name == "removeAllObjects" || name == "removeObjectForKey:" || name == "removeObjectAtIndex:") {
            container.items.clear(); container.keys.clear();
            ++container.mutations; container.sliceGiven = false;
            return 0;
        }
        if (name == "intValue" || name == "integerValue") return static_cast<std::uint32_t>(std::atoi(container.text.c_str()));
        if (name == "bytes") return reinterpret_cast<std::uint32_t>(container.data.empty() ? nullptr : container.data.data());
        if (name == "length") {
            return static_cast<std::uint32_t>(container.kind == 'B' ? container.data.size() : std::strlen(container.text.c_str()));
        }
        if (name == "release" || name == "dealloc" || name == "autorelease") return 0;
        log("PLATFORM_CONTAINER_UNHANDLED " + name);
        ExitProcess(31);
    }
    // Host string objects answer the NSString value/length contract used by defaults and account UI.
    for (auto& candidate : hostStrings) {
        if (receiver != &candidate) continue;
        if (name == "init" || name == "autorelease" || name == "release" || name == "dealloc") return reinterpret_cast<std::uint32_t>(&candidate);
        if (name == "initWithUTF8String:" || name == "initWithCString:" || name == "initWithCString:encoding:"
            || name == "initWithString:") {
            va_list arguments; va_start(arguments, selector);
            const void* source = va_arg(arguments, const void*); va_end(arguments);
            const char* bytes = cfStringBytes(source);
            std::snprintf(candidate.text, sizeof(candidate.text), "%s", bytes ? bytes : "");
            return reinterpret_cast<std::uint32_t>(&candidate);
        }
        if (name == "initWithFormat:" || name == "initWithFormat:arguments:") {
            va_list arguments; va_start(arguments, selector);
            const void* format = va_arg(arguments, const void*); va_end(arguments);
            const char* bytes = cfStringBytes(format);
            std::snprintf(candidate.text, sizeof(candidate.text), "%s", bytes ? bytes : "");
            return reinterpret_cast<std::uint32_t>(&candidate);
        }
        if (name == "initWithData:encoding:" || name == "initWithContentsOfFile:encoding:error:"
            || name == "initWithBytes:length:encoding:") return reinterpret_cast<std::uint32_t>(&candidate);
        if (name == "boolValue") return std::atoi(candidate.text) != 0 ? 1u : 0u;
        if (name == "intValue") return static_cast<std::uint32_t>(std::atoi(candidate.text));
        if (name == "length") return static_cast<std::uint32_t>(std::strlen(candidate.text));
        if (name == "UTF8String") return reinterpret_cast<std::uint32_t>(candidate.text);
        if (name == "stringValue" || name == "description") return reinterpret_cast<std::uint32_t>(&candidate);
        if (name == "stringByAppendingString:" || name == "stringByAppendingPathComponent:") {
            va_list arguments; va_start(arguments, selector);
            const void* other = va_arg(arguments, const void*); va_end(arguments);
            const char* bytes = cfStringBytes(other);
            if (bytes) {
                const std::string joined = std::string(candidate.text)
                    + (name == "stringByAppendingPathComponent:" ? "/" : "") + bytes;
                return reinterpret_cast<std::uint32_t>(copyHostText(joined));
            }
            return reinterpret_cast<std::uint32_t>(&candidate);
        }
        if (name == "isEqualToString:") {
            va_list arguments; va_start(arguments, selector);
            const void* other = va_arg(arguments, const void*); va_end(arguments);
            const char* bytes = cfStringBytes(other);
            return (bytes && !std::strcmp(candidate.text, bytes)) ? 1u : 0u;
        }
        log("PLATFORM_STRING_UNHANDLED " + name);
        ExitProcess(31);
    }
    // NSUserDefaults: real content-key storage, persisted under agent/dsh/state across processes.
    if (receiver == &hostUserDefaultsToken) {
        if (name == "synchronize") { savePersistedStore(); log("PLATFORM_DEFAULTS synchronize"); return 1; }
        if (name == "registerDefaults:" || name == "dictionaryRepresentation") return 0;
        // SCORES_load asks for the stored score list; restore a previously persisted container when present.
        if (name == "arrayForKey:" || name == "dictionaryForKey:" || name == "dataForKey:") {
            va_list arguments; va_start(arguments, selector);
            const void* key = va_arg(arguments, const void*); va_end(arguments);
            const std::string keyText = hostKeyText(key);
            const auto found = hostAccountStrings.find(keyText);
            void* token = nullptr;
            if (found != hostAccountStrings.end() && deserializeContainer(found->second, &token) && token) {
                log("PLATFORM_DEFAULTS " + name + " " + keyText + " -> restored container");
                return reinterpret_cast<std::uint32_t>(token);
            }
            log("PLATFORM_DEFAULTS " + name + " " + keyText + " -> nil (no stored value)");
            return 0;
        }
        if (name == "removeObjectForKey:") {
            va_list arguments; va_start(arguments, selector);
            const void* key = va_arg(arguments, const void*); va_end(arguments);
            const std::string text = hostKeyText(key);
            hostAccountInts.erase(text); hostAccountStrings.erase(text); hostAccountFloats.erase(text);
            savePersistedStore();
            log("PLATFORM_DEFAULTS remove " + text);
            return 0;
        }
        if (name.rfind("setObject", 0) == 0) {
            va_list arguments; va_start(arguments, selector);
            const void* value = va_arg(arguments, const void*);
            const void* key = va_arg(arguments, const void*);
            va_end(arguments);
            const std::string keyText = hostKeyText(key);
            std::string resolved;
            for (const auto& container : hostContainers)
                if (value == &container) { resolved = serializeContainer(container); break; }
            if (resolved.empty()) {
                if (const char* bytes = cfStringBytes(value)) resolved = bytes;
                else tryReadText(value, resolved);
            }
            hostAccountStrings[keyText] = resolved;
            savePersistedStore();
            log("PLATFORM_DEFAULTS setObject " + keyText + "=" + (resolved.size() > 96 ? resolved.substr(0, 96) + "..." : resolved));
            return 0;
        }
        if (name.rfind("setBool", 0) == 0 || name.rfind("setInteger", 0) == 0) {
            va_list arguments; va_start(arguments, selector);
            const int value = va_arg(arguments, int);
            const void* key = va_arg(arguments, const void*);
            va_end(arguments);
            const std::string keyText = hostKeyText(key);
            hostAccountInts[keyText] = value;
            savePersistedStore();
            log("PLATFORM_DEFAULTS setInt " + keyText + "=" + std::to_string(value));
            return 0;
        }
        if (name.rfind("setFloat", 0) == 0) {
            va_list arguments; va_start(arguments, selector);
            const unsigned int raw = va_arg(arguments, unsigned int);
            const void* key = va_arg(arguments, const void*);
            va_end(arguments);
            const std::string keyText = hostKeyText(key);
            hostAccountFloats[keyText] = *reinterpret_cast<const float*>(&raw);
            savePersistedStore();
            log("PLATFORM_DEFAULTS setFloat " + keyText + "=" + std::to_string(hostAccountFloats[keyText]));
            return 0;
        }
        if (name.rfind("setDouble", 0) == 0) {
            va_list arguments; va_start(arguments, selector);
            const double value = va_arg(arguments, double);
            const void* key = va_arg(arguments, const void*);
            va_end(arguments);
            const std::string keyText = hostKeyText(key);
            hostAccountFloats[keyText] = value;
            savePersistedStore();
            log("PLATFORM_DEFAULTS setDouble " + keyText + "=" + std::to_string(value));
            return 0;
        }
        if (name.rfind("boolForKey:", 0) == 0 || name.rfind("integerForKey:", 0) == 0) {
            va_list arguments; va_start(arguments, selector);
            const void* key = va_arg(arguments, const void*); va_end(arguments);
            const std::string text = hostKeyText(key);
            const auto found = hostAccountInts.find(text);
            const int result = found == hostAccountInts.end() ? 0 : found->second;
            log("PLATFORM_DEFAULTS " + name + " " + text + " -> " + std::to_string(result));
            return static_cast<std::uint32_t>(result);
        }
        if (name.rfind("objectForKey:", 0) == 0 || name.rfind("stringForKey:", 0) == 0) {
            va_list arguments; va_start(arguments, selector);
            const void* key = va_arg(arguments, const void*); va_end(arguments);
            const std::string text = hostKeyText(key);
            const auto stringValue = hostAccountStrings.find(text);
            if (stringValue != hostAccountStrings.end()) {
                log("PLATFORM_DEFAULTS " + name + " " + text + " -> " + stringValue->second);
                return reinterpret_cast<std::uint32_t>(copyHostText(stringValue->second));
            }
            const auto intValue = hostAccountInts.find(text);
            if (intValue != hostAccountInts.end()) {
                char buffer[32]; std::snprintf(buffer, sizeof(buffer), "%d", intValue->second);
                log("PLATFORM_DEFAULTS " + name + " " + text + " -> " + buffer);
                return reinterpret_cast<std::uint32_t>(copyHostText(buffer));
            }
            log("PLATFORM_DEFAULTS " + name + " " + text + " -> nil");
            return 0;
        }
        log("PLATFORM_DEFAULTS_UNHANDLED " + name);
        ExitProcess(31);
    }
    // UIKit lifecycle: the host owns the window and GL context, so only the idle timer is accepted.
    if (name == "sharedApplication") return reinterpret_cast<std::uint32_t>(&hostApplicationToken);
    if (name == "standardUserDefaults") { log("PLATFORM_DEFAULTS standardUserDefaults"); return reinterpret_cast<std::uint32_t>(&hostUserDefaultsToken); }
    if (name == "setIdleTimerDisabled:") return 0;
    if (name == "delegate") return 0; // A headless host has no UIKit delegate.
    // Purchase verification is explicitly out of scope; the full-version menu pages are required.
    if (name == "unlockedFullVersion") { log("PLATFORM_PURCHASE unlockedFullVersion -> 1 (full build)"); return 1; }
    // Store gating is out of scope; every shipped gamemode is startable in this full build.
    if (name == "canStartMapWithName:") {
        va_list arguments; va_start(arguments, selector);
        const char* mapName = va_arg(arguments, const char*); va_end(arguments);
        std::string resolved; const bool decoded = tryReadText(mapName, resolved);
        log(std::string("PLATFORM_STORE canStartMapWithName: ") + (decoded ? resolved : "(unprintable)") + " -> 1 (content shipped; purchase gate excluded)");
        return 1;
    }
    // Progress/account sync is kept as a real in-memory key/value contract for this run.
    if (name == "current") return reinterpret_cast<std::uint32_t>(&hostGameAccountToken);
    if (name == "isGameCenter") { log("PLATFORM_CAPABILITY isGameCenter -> 0 (no Game Center on this host)"); return 0; }
    if (name == "isGameCenterAvailable") { log("PLATFORM_CAPABILITY isGameCenterAvailable -> 0 (no Game Center on this host)"); return 0; }
    if (name == "flush") { savePersistedStore(); log("PLATFORM_ACCOUNT flush (persisted)"); return 0; }
    if (name == "name") {
        const auto stored = hostAccountStrings.find("playername");
        const std::string player = stored != hostAccountStrings.end() && !stored->second.empty() ? stored->second : std::string("host");
        // Ask the guest string class slot for an object. Boundary (verified): installHook writes an E9 jump at
        // the original export entry, so the alloc/init messages sent here re-enter nativeMessage and the result
        // is the host string adaptation, NOT a Foundation-allocated NSString. The earlier "cached IMPs bypass the
        // hook" reasoning is retracted: the endgame crash was the missing objc_msgSend_fpret hook.
        if (void* real = originalNSString(player)) {
            log("PLATFORM_ACCOUNT name -> " + player + " (host string via hooked alloc/init)");
            return reinterpret_cast<std::uint32_t>(real);
        }
        HostString* value = nextHostString();
        std::snprintf(value->text, sizeof(value->text), "%s", player.c_str());
        log("PLATFORM_ACCOUNT name -> " + player);
        return reinterpret_cast<std::uint32_t>(value);
    }
    if (name == "UTF8String") {
        for (auto& candidate : hostStrings)
            if (receiver == &candidate) { log("PLATFORM_STRING UTF8String -> " + std::string(candidate.text)); return reinterpret_cast<std::uint32_t>(candidate.text); }
        // An original CFString constant still owns real bytes; return them instead of an empty string.
        if (const char* bytes = cfStringBytes(receiver)) return reinterpret_cast<std::uint32_t>(bytes);
        log("PLATFORM_STRING UTF8String unresolved receiver");
        return reinterpret_cast<std::uint32_t>("");
    }
    if (name == "setCurrentContext:") { log("PLATFORM_GL setCurrentContext: (host retains its context)"); return 0; }
    if (name == "stopAnimation") { log("PLATFORM_UI stopAnimation -> 0"); return 0; }
    // Campaign/DLC progress accessors share the unified key store; absent keys are the real unset default.
    if (name.rfind("unlocked", 0) == 0) {
        const auto found = hostAccountInts.find(name);
        const int result = found == hostAccountInts.end() ? 0 : found->second;
        log("PLATFORM_PROGRESS " + name + " -> " + std::to_string(result) + (found == hostAccountInts.end() ? " (unset default)" : " (stored)"));
        return static_cast<std::uint32_t>(result);
    }
    if (name == "stringWithUTF8String:") {
        va_list arguments; va_start(arguments, selector);
        const char* text = va_arg(arguments, const char*);
        va_end(arguments);
        HostString* value = nextHostString();
        std::string text8; tryReadText(text, text8);
        std::snprintf(value->text, sizeof(value->text), "%s", text8.c_str());
        log("PLATFORM_STRING utf8 -> " + std::string(value->text));
        return reinterpret_cast<std::uint32_t>(value);
    }
    if (name == "stringWithFormat:") {
        va_list arguments; va_start(arguments, selector);
        const void* format = va_arg(arguments, const void*);
        const char* formatText = cfStringBytes(format);
        const std::string produced = formatText ? formatNSString(formatText, arguments) : std::string();
        va_end(arguments);
        log(std::string("PLATFORM_STRING stringWithFormat format=") + (formatText ? formatText : "(undecoded)") + " -> " + produced);
        return reinterpret_cast<std::uint32_t>(copyHostText(produced));
    }
    // Class-method factories the original score serializer uses.
    if (name == "numberWithInt:" || name == "numberWithInteger:" || name == "numberWithFloat:"
        || name == "dateWithTimeIntervalSinceReferenceDate:" || name == "dataWithBytes:length:"
        || name == "stringWithCString:encoding:" || name == "stringWithUTF8String:") {
        HostContainer* container = newHostContainer();
        // Kind drives persistence: numbers 'N', dates 'T', byte payloads 'B' (replay bytes were previously
        // saved as an empty number and lost), strings 'S'.
        container->kind = name.rfind("numberWith", 0) == 0 ? 'N'
            : (name.rfind("dateWith", 0) == 0 ? 'T' : (name.rfind("dataWith", 0) == 0 ? 'B' : 'S'));
        va_list arguments; va_start(arguments, selector);
        if (name == "stringWithCString:encoding:" || name == "stringWithUTF8String:") {
            const void* source = va_arg(arguments, const void*);
            const char* bytes = cfStringBytes(source);
            if (bytes) container->text = bytes;
        } else if (name == "numberWithInt:" || name == "numberWithInteger:") {
            container->text = std::to_string(va_arg(arguments, int));
        } else if (name == "numberWithFloat:") {
            // i386 ObjC args are not variadic-promoted: the call site pushes a 4-byte float.
            const unsigned int raw = va_arg(arguments, unsigned int);
            container->text = std::to_string(*reinterpret_cast<const float*>(&raw));
        } else if (name == "dateWithTimeIntervalSinceReferenceDate:") {
            container->text = std::to_string(va_arg(arguments, double));
        } else if (name == "dataWithBytes:length:") {
            const void* bytes = va_arg(arguments, const void*);
            const unsigned length = static_cast<unsigned>(va_arg(arguments, unsigned int));
            if (bytes && length) {
                const auto* source = static_cast<const unsigned char*>(bytes);
                container->data.assign(source, source + length);
            }
        }
        va_end(arguments);
        // Strings share the host string surface; numbers/dates/data keep a readable value.
        if (name.rfind("stringWith", 0) == 0) {
            HostString* value = nextHostString();
            std::snprintf(value->text, sizeof(value->text), "%s", container->text.c_str());
            log("PLATFORM_STRING " + name + " -> " + container->text);
            return reinterpret_cast<std::uint32_t>(value);
        }
        log("PLATFORM_CONTAINER " + name + " kind=" + std::string(1, container->kind)
            + " value=" + (container->kind == 'B' ? std::to_string(container->data.size()) + " bytes" : container->text));
        return reinterpret_cast<std::uint32_t>(container);
    }
    if (name.rfind("getBool", 0) == 0) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const int fallback = va_arg(arguments, int);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        const auto found = hostAccountInts.find(text);
        const int result = found == hostAccountInts.end() ? fallback : found->second;
        log("PLATFORM_ACCOUNT getBool " + text + " default=" + std::to_string(fallback) + " -> " + std::to_string(result));
        return static_cast<std::uint32_t>(result);
    }
    if (name.rfind("getString", 0) == 0) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const void* fallback = va_arg(arguments, const void*);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        const auto found = hostAccountStrings.find(text);
        if (found != hostAccountStrings.end()) {
            HostString* value = nextHostString();
            std::snprintf(value->text, sizeof(value->text), "%s", found->second.c_str());
            log("PLATFORM_ACCOUNT getString " + text + " -> " + std::string(value->text));
            return reinterpret_cast<std::uint32_t>(value);
        }
        // withDefault: semantics require the caller's own default object when the key is absent.
        log("PLATFORM_ACCOUNT getString " + text + " -> default " + hostKeyText(fallback));
        return reinterpret_cast<std::uint32_t>(fallback);
    }
    // GameAccount player name is handled above (real stored value with a stable default).
    if (name.rfind("getInt", 0) == 0) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const int fallback = va_arg(arguments, int);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        const auto found = hostAccountInts.find(text);
        const int result = found == hostAccountInts.end() ? fallback : found->second;
        log("PLATFORM_ACCOUNT getInt " + text + " default=" + std::to_string(fallback) + " -> " + std::to_string(result));
        return static_cast<std::uint32_t>(result);
    }
    if (name.rfind("setBool", 0) == 0) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const int value = va_arg(arguments, int);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        hostAccountInts[text] = value;
        savePersistedStore();
        log("PLATFORM_ACCOUNT setBool " + text + "=" + std::to_string(value));
        return 0;
    }
    if (name.rfind("setInt", 0) == 0) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const int value = va_arg(arguments, int);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        hostAccountInts[text] = value;
        savePersistedStore();
        log("PLATFORM_ACCOUNT setInt " + text + "=" + std::to_string(value));
        return 0;
    }
    if (name.rfind("setString", 0) == 0) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const void* value = va_arg(arguments, const void*);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        hostAccountStrings[text] = hostKeyText(value);
        savePersistedStore();
        log("PLATFORM_ACCOUNT setString " + text + "=" + hostAccountStrings[text]);
        return 0;
    }
    if (name.rfind("setFloat", 0) == 0) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const unsigned int raw = va_arg(arguments, unsigned int);
        va_end(arguments);
        const float value = *reinterpret_cast<const float*>(&raw);
        hostAccountFloats[hostKeyText(key)] = value;
        savePersistedStore();
        log("PLATFORM_ACCOUNT setFloat " + hostKeyText(key) + "=" + std::to_string(value));
        return 0;
    }
    if (name == "deleteSavegameForMode:") {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        hostAccountInts.erase(text); hostAccountStrings.erase(text);
        savePersistedStore();
        log("PLATFORM_ACCOUNT deleteSavegame " + text);
        return 0;
    }
    // Ranking and DLC purchase endpoints are out of scope by project rules; log the exact selector.
    if (name == "reportScore:modename:") { log("PLATFORM_RANKING_EXCLUDED " + name); return 0; }
    if (name == "openURL:" || name == "URLWithString:") { log("PLATFORM_PURCHASE_EXCLUDED " + name); return 0; }
    log("UNIMPLEMENTED_OBJC_SELECTOR " + name + " receiver=" + std::to_string(reinterpret_cast<std::uintptr_t>(receiver))
        + " class=" + objcClassName(receiver) + " caller=" + nativeCaller(_ReturnAddress()));
    ExitProcess(31);
}

/** Convert Windows cursor coordinates through the original CGPoint return ABI. */
void __cdecl nativeMessageStruct(float* point, void* receiver, const char* selector, void*) {
    const std::string name = selectorName(selector);
    if (receiver == &hostTouch && name == "locationInView:") { point[0] = hostTouch.x; point[1] = hostTouch.y; return; }
    if (receiver == hostView.data() && name == "bounds") { point[0] = point[1] = 0; point[2] = 480; point[3] = 800; return; }
    log("UNIMPLEMENTED_OBJC_STRUCT_SELECTOR " + name); ExitProcess(32);
}

/** Implement the one floating-point platform selector with real NSDate semantics. */
double __cdecl nativeMessageFpret(void* receiver, const char* selector, ...) {
    if (!receiver) return 0.0;
    const std::string name = selectorName(selector);
    if (name == "timeIntervalSinceReferenceDate") {
        // Seconds since 2001-01-01, matching NSDate's reference date.
        FILETIME time{}; GetSystemTimeAsFileTime(&time);
        const std::uint64_t ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        constexpr std::uint64_t Reference2001Ticks = 126227808000000000ULL;
        return static_cast<double>(ticks - Reference2001Ticks) / 10000000.0;
    }
    if (name.rfind("getFloat", 0) == 0) {
        // i386 call sites pass the default as a 4-byte float slot, so read exactly that word.
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        const unsigned int raw = va_arg(arguments, unsigned int);
        va_end(arguments);
        const float fallback = *reinterpret_cast<const float*>(&raw);
        const std::string text = hostKeyText(key);
        const auto found = hostAccountFloats.find(text);
        const double result = found == hostAccountFloats.end() ? static_cast<double>(fallback) : found->second;
        log("PLATFORM_ACCOUNT getFloat " + text + " default=" + std::to_string(fallback) + " -> " + std::to_string(result));
        return result;
    }
    if (receiver == &hostUserDefaultsToken && (name.rfind("floatForKey:", 0) == 0 || name.rfind("doubleForKey:", 0) == 0)) {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        double result = 0.0;
        const auto asFloat = hostAccountFloats.find(text);
        if (asFloat != hostAccountFloats.end()) result = asFloat->second;
        else { const auto asInt = hostAccountInts.find(text); if (asInt != hostAccountInts.end()) result = asInt->second; }
        log("PLATFORM_DEFAULTS " + name + " " + text + " -> " + std::to_string(result));
        return result;
   }
    for (auto& container : hostContainers) {
        if (receiver != &container) continue;
        // Faithful NSDate semantics: the container was built by dateWithTimeIntervalSinceReferenceDate:, so
        // timeIntervalSince1970 must add the 1970<->2001 delta. The original itself mixes the two units:
        // SCORES_load reads timeIntervalSince1970 and hands the double to SCORES_add (pseudocode.c:8306/8309,
        // stored raw at :8442), while SCORES_save feeds that same double back into
        // dateWithTimeIntervalSinceReferenceDate: (:8372). Reproducing the platform faithfully therefore keeps
        // the original's own unit drift; that drift is a property of the shipped game, not something the host
        // should silently hide. (Earlier pass-through made load/save value-stable but changed NSDate semantics.)
        if (name == "timeIntervalSince1970") return std::atof(container.text.c_str()) + 978307200.0;
        if (name == "doubleValue" || name == "floatValue") return std::atof(container.text.c_str());
        // Any other fp message on a host container is unimplemented: fail loudly instead of returning an
        // unlogged 0.0, which silently faked success and made the exit-33 path unreachable.
        log("UNIMPLEMENTED_OBJC_FPRET " + name + " receiver=container class=" + objcClassName(receiver)
            + " caller=" + nativeCaller(_ReturnAddress()));
        ExitProcess(33);
    }
    for (auto& candidate : hostStrings) {
        if (receiver != &candidate) continue;
        if (name == "floatValue" || name == "doubleValue") return std::atof(candidate.text);
        break;
    }
    log("UNIMPLEMENTED_OBJC_FPRET " + name);
    ExitProcess(33);
}

/** Match i386 Objective-C's callee cleanup of the hidden structure-result pointer. */
__declspec(naked) void nativeMessageStructBridge() {
    __asm {
        mov eax, esp
        push dword ptr [eax + 16]
        push dword ptr [eax + 12]
        push dword ptr [eax + 8]
        push dword ptr [eax + 4]
        call nativeMessageStruct
        add esp, 16
        ret 4
    }
}

/** Advance the exact POSIX 48-bit linear-congruential generator. */
std::uint64_t nextRand48() { rand48State = (rand48State * Rand48Multiplier + Rand48Increment) & Rand48Mask; return rand48State; }

/** Provide the native signed-positive integer visual-random API. */
long __cdecl nativeLrand48() { return static_cast<long>(nextRand48() >> 17); }

/** Provide the native double visual-random API with its original denominator. */
double __cdecl nativeDrand48() { return static_cast<double>(nextRand48()) * 0x1p-48; }

/** Apply the POSIX seed expansion used by the original platform. */
void __cdecl nativeSrand48(long seed) { rand48State = (static_cast<std::uint64_t>(static_cast<unsigned long>(seed)) << 16) | 0x330e; }

/** Supply the monotonic seconds expected by the original game clock. */
double __cdecl nativeRealtime() {
    static const ULONGLONG start = GetTickCount64();
    return static_cast<double>(GetTickCount64() - start) / 1000.0;
}

/** Return the original 32-bit Unix timeval layout using Windows system time. */
int __cdecl nativeGettimeofday(std::int32_t* value, void*) {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    const std::uint64_t ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    // Windows FILETIME starts in 1601; Unix timestamps start in 1970.
    constexpr std::uint64_t UnixEpochTicks = 116444736000000000ULL;
    const auto microseconds = (ticks - UnixEpochTicks) / 10;
    value[0] = static_cast<std::int32_t>(microseconds / 1000000); value[1] = static_cast<std::int32_t>(microseconds % 1000000);
    return 0;
}

/** Save pixels produced by the original renderer without substituting a scene. */
void capture(const std::filesystem::path& path, unsigned width, unsigned height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4), flipped(pixels.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (unsigned row = 0; row < height; ++row)
        std::memcpy(flipped.data() + row * width * 4, pixels.data() + (height - row - 1) * width * 4, width * 4);
    IWICImagingFactory* factory = nullptr; IWICStream* stream = nullptr; IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr; IPropertyBag2* options = nullptr;
    HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(status)) status = factory->CreateStream(&stream);
    if (SUCCEEDED(status)) status = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(status)) status = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(status)) status = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(status)) status = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(status)) status = frame->Initialize(options);
    if (SUCCEEDED(status)) status = frame->SetSize(width, height);
    // WIC's PNG encoder accepts BGRA; GL readback is RGBA and must be converted.
    for (std::size_t index = 0; index < flipped.size(); index += 4) std::swap(flipped[index], flipped[index + 2]);
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(status)) status = frame->SetPixelFormat(&pixelFormat);
    if (SUCCEEDED(status)) status = frame->WritePixels(height, width * 4, static_cast<UINT>(flipped.size()), flipped.data());
    if (SUCCEEDED(status)) status = frame->Commit();
    if (SUCCEEDED(status)) status = encoder->Commit();
    if (options) options->Release(); if (frame) frame->Release(); if (encoder) encoder->Release(); if (stream) stream->Release(); if (factory) factory->Release();
    if (FAILED(status)) throw std::runtime_error("Original frame PNG export failed");
    log("ORIGINAL_FRAME " + path.string());
}

/** Fraction of sampled framebuffer pixels that are not near-white; proves a page actually rendered. */
double frameNonWhiteFraction(unsigned width, unsigned height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::size_t sampled = 0, nonWhite = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4 * 16) {
        ++sampled;
        if (pixels[index] < 245 || pixels[index + 1] < 245 || pixels[index + 2] < 245) ++nonWhite;
    }
    return sampled ? static_cast<double>(nonWhite) / static_cast<double>(sampled) : 0.0;
}

/** Read real player statistics and terminal flags through the original exported getters. */
nlohmann::json playerState(void* game) {
    void* player = reinterpret_cast<void**>(game)[1];
    const auto getter = [&](const char* name) { return reinterpret_cast<int(__cdecl*)(void*)>(findExport(name))(player); };
    const auto gameGetter = [&](const char* name) { return reinterpret_cast<int(__cdecl*)(void*)>(findExport(name))(game); };
    return {{"money", getter("PLAYER_getMoney")}, {"score", getter("PLAYER_getScore")}, {"lives", getter("PLAYER_getLife")},
        {"simulationMs", reinterpret_cast<int*>(game)[10]}, {"gameStarted", reinterpret_cast<int*>(game)[6] != 0},
        // Ended/won/final values come from the original terminal evaluators, never from host guesses.
        {"ended", gameGetter("GAME_hasEnded") != 0}, {"won", gameGetter("GAME_isWon") != 0},
        {"finalScore", gameGetter("GAME_getFinalScore")}, {"finalLives", gameGetter("GAME_getFinalLifes")}};
}

/** Read the original GAME simulation clock through the menu's game pointer, or -1 when absent. */
int currentGameClock(void* menu) {
    if (!menu) return -1;
    void* game = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
    return game ? reinterpret_cast<int*>(game)[10] : -1;
}

/** Original IBS buffer files are length-prefixed with a small magic for this test host. */
constexpr std::uint32_t SaveMagic = 0x31534249; // "IBS1"

/** Stable save location: the packaged data directory (or the dev workspace state fallback). */
std::filesystem::path saveStatePath(const std::filesystem::path& output) {
    if (!dataDirectory.empty()) return dataDirectory / "menu-save.bin";
    return output.parent_path().parent_path() / "state" / "menu-save.bin";
}

/** Choose the packaged data directory only for a staged layout, else the dev workspace state dir. */
std::filesystem::path resolveDataDirectory(const std::filesystem::path& package, const std::filesystem::path& outputDirectory) {
    std::error_code error;
    const auto root = package.parent_path().parent_path().parent_path().parent_path();
    const bool staged = std::filesystem::exists(root / "runtime") && std::filesystem::exists(root / "assets");
    if (staged) {
        const auto packaged = root / "data";
        std::filesystem::create_directories(packaged, error);
        if (!error) return packaged;
    }
    error.clear();
    const auto fallback = outputDirectory.parent_path().parent_path() / "state";
    std::filesystem::create_directories(fallback, error);
    return fallback;
}

/** Persist the exact original OBS bytes produced by GAME_save (no host-side rewriting). */
void writeSaveState(const std::filesystem::path& path, const void* bytes, std::uint32_t length) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(&SaveMagic), 4);
    stream.write(reinterpret_cast<const char*>(&length), 4);
    stream.write(static_cast<const char*>(bytes), length);
}

/** Read a previously persisted original OBS byte stream. */
bool readSaveState(const std::filesystem::path& path, std::vector<std::uint8_t>& data) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::uint32_t magic = 0, length = 0;
    stream.read(reinterpret_cast<char*>(&magic), 4);
    stream.read(reinterpret_cast<char*>(&length), 4);
    if (!stream || magic != SaveMagic || length == 0 || length > (1u << 20)) return false;
    data.resize(length);
    stream.read(reinterpret_cast<char*>(data.data()), length);
    return static_cast<bool>(stream);
}

/** Load the persisted platform key/value store; empty keys keep their original unset default. */
void loadPersistedStore() {
    if (persistedStoreLoaded || persistedStorePath.empty()) return;
    persistedStoreLoaded = true;
    std::ifstream stream(persistedStorePath, std::ios::binary);
    if (!stream) return;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() < 4 || line[1] != '\t') continue;
        const std::size_t first = line.find('\t');
        const std::size_t second = line.find('\t', first + 1);
        if (second == std::string::npos) continue;
        const char kind = line[0];
        const std::string key = line.substr(first + 1, second - first - 1);
        const std::string value = line.substr(second + 1);
        if (kind == 'i') hostAccountInts[key] = std::atoi(value.c_str());
        else if (kind == 'f') hostAccountFloats[key] = std::atof(value.c_str());
        else if (kind == 's') hostAccountStrings[key] = value;
    }
}

/** Persist the platform key/value store so a new process reads the same account/settings content. */
void savePersistedStore() {
    if (persistedStorePath.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(persistedStorePath.parent_path(), error);
    std::ofstream stream(persistedStorePath, std::ios::binary | std::ios::trunc);
    if (!stream) return;
    for (const auto& entry : hostAccountInts) stream << "i\t" << entry.first << "\t" << entry.second << "\n";
    for (const auto& entry : hostAccountStrings) stream << "s\t" << entry.first << "\t" << entry.second << "\n";
    for (const auto& entry : hostAccountFloats) stream << "f\t" << entry.first << "\t" << entry.second << "\n";
}

/**
 * Install a restored GAME exactly the way the original start callback FUN_002c6403 finishes:
 * replace any existing game, set the resolution from the menu's logical size, go back to the
 * in-progress main menu (MENU_backToMainMenu), MENU_hide, then clear the fade state and set
 * showing=0 directly so the game runs on the next frame instead of through the menu fade.
 */
void installRestoredGame(void* userMenu, void* loaded) {
    if (!userMenu || !loaded) return;
    auto* bytes = static_cast<unsigned char*>(userMenu);
    auto* gameSlot = reinterpret_cast<void**>(bytes + 0x15c);
    // Never free a running game here: the render/update path can still hold it (observed as an access
    // violation in the in-process hotkey load). Replace the slot and let the old game leak in the probe.
    if (*gameSlot && *gameSlot != loaded) {
        pendingGameFree = *gameSlot;
        log("ORIGINAL_GAME_REPLACED old=" + std::to_string(reinterpret_cast<std::uintptr_t>(*gameSlot))
            + " (free deferred to the next frame boundary)");
    }
    *gameSlot = loaded;
    const int width = static_cast<int>(std::lround(*reinterpret_cast<const float*>(bytes + 0x110)));
    const int height = static_cast<int>(std::lround(*reinterpret_cast<const float*>(bytes + 0x114)));
    reinterpret_cast<void(__cdecl*)(void*, int, int)>(findExport("GAME_setResolution"))(loaded, width, height);
    // Original order: back to the in-progress main menu (its GAME_silence/changePage/MENUWORLD_resume),
    // then MENU_hide (which re-anchors GAME_resume and the account game-in-progress flag).
    reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_backToMainMenu"))(userMenu);
    reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_hide"))(userMenu);
    *reinterpret_cast<float*>(bytes + 0xec) = 0.0f;
    bytes[4] = 0;
    bytes[0xf8] = 1;
    log("ORIGINAL_SAVE_CONTINUE_INSTALLED showing=" + std::to_string(static_cast<int>(bytes[4])));
}

/** F5 in the interactive menu: persist the running GAME through the original GAME_save OBS bytes. */
void hotkeySaveState(void* userMenu) {
    if (!userMenu) return;
    void* game = *reinterpret_cast<void**>(static_cast<unsigned char*>(userMenu) + 0x15c);
    if (!game) { log("ORIGINAL_SAVE_HOTKEY_NOGAME"); return; }
    void* obs = reinterpret_cast<void*(__cdecl*)(void*)>(findExport("GAME_save"))(game);
    const auto* header = reinterpret_cast<const std::uint32_t*>(obs);
    // Hotkey saves stay separate so an interactive save never replaces the canonical continue state.
    const auto path = dataDirectory.empty() ? saveStatePath({}) : dataDirectory / "menu-save-hotkey.bin";
    writeSaveState(path, reinterpret_cast<void*>(header[0]), header[1]);
    log("ORIGINAL_SAVE_HOTKEY file=" + path.string() + " bytes=" + std::to_string(header[1]) + " " + playerState(game).dump());
}

/** F9 in the interactive menu: restore the saved GAME through the original GAME_fromIBS path. */
void hotkeyLoadState(void* userMenu) {
    if (!userMenu) return;
    const auto path = dataDirectory.empty() ? saveStatePath({}) : dataDirectory / "menu-save-hotkey.bin";
    if (!readSaveState(path, loadedIbsBytes)) { log("ORIGINAL_LOAD_HOTKEY_NOFILE " + path.string()); return; }
    void* ibs = reinterpret_cast<void*(__cdecl*)(void*, unsigned, unsigned)>(findExport("IBS_wrap"))(
        loadedIbsBytes.data(), static_cast<unsigned>(loadedIbsBytes.size()), 0);
    void* loaded = reinterpret_cast<void*(__cdecl*)(void*)>(findExport("GAME_fromIBS"))(ibs);
    log("ORIGINAL_LOAD_HOTKEY bytes=" + std::to_string(loadedIbsBytes.size()) + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(loaded))
        + (loaded ? " " + playerState(loaded).dump() : std::string(" (failed)")));
    if (!loaded) return;
    installRestoredGame(userMenu, loaded);
}

/** Resolve tower IDs from the original loaded technology, never a rewritten catalog. */
int nativeTowerId(void* mode, const char* name) {
    const auto* technology = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint32_t*>(mode)[7]);
    const auto* types = reinterpret_cast<std::uint32_t*>(technology[15]);
    for (unsigned index = 0; index < technology[14]; ++index) {
        const auto* type = reinterpret_cast<std::uint32_t*>(types[index]);
        if (!std::strcmp(reinterpret_cast<const char*>(type[1]), name)) return static_cast<int>(type[0]);
    }
    throw std::runtime_error("Original technology does not contain requested tower");
}

/** Resolve an optional upgrade target without aborting a scenario when a tech omits it. */
int nativeTowerIdOrNone(void* mode, const char* name) {
    try { return nativeTowerId(mode, name); } catch (const std::exception&) { return -1; }
}

// A scenario keeps the existing operation names and adds an optional original gamemode name.
struct Scenario { std::string operation; std::string modeName = "PlainPlane"; };

/** Split "base" or "base:ModeName" without altering the original export operations. */
Scenario parseScenario(const std::string& value) {
    Scenario scenario;
    const auto separator = value.find(':');
    scenario.operation = value.substr(0, separator);
    if (separator != std::string::npos && separator + 1 < value.size()) scenario.modeName = value.substr(separator + 1);
    return scenario;
}

/** Load and drive a real mode through original gameplay and rendering exports. */
void originalScene(const std::filesystem::path& output, const Scenario& scenario) {
    const std::string& operation = scenario.operation;
    reinterpret_cast<void(__cdecl*)()>(findExport("PGL_init"))(); log("PGL_READY");
    reinterpret_cast<void(__cdecl*)()>(findExport("WAVES_init"))(); log("WAVES_READY");
    auto* mode = reinterpret_cast<void*(__cdecl*)(const char*, int)>(findExport("GAMEMODE_load"))(scenario.modeName.c_str(), 0);
    if (!mode) throw std::runtime_error("Original GAMEMODE_load rejected " + scenario.modeName);
    log("GAMEMODE_READY " + scenario.modeName);
    auto* game = reinterpret_cast<void*(__cdecl*)(void*, int)>(findExport("GAME_alloc"))(mode, 0);
    if (!game) throw std::runtime_error("Original GAME_alloc returned null");
    log("GAME_READY");
    reinterpret_cast<void(__cdecl*)(void*, int, int)>(findExport("GAME_setResolution"))(game, 480, 800);
    auto resume = reinterpret_cast<void(__cdecl*)(void*)>(findExport("GAME_resume"));
    resume(game);
    glViewport(0, 0, 480, 800);
    auto update = reinterpret_cast<void(__cdecl*)(void*)>(findExport("GAME_update"));
    auto render = reinterpret_cast<void(__cdecl*)(void*)>(findExport("GAME_render"));
    auto build = reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(findExport("GAME_buildTower"));
    auto nextWave = reinterpret_cast<void(__cdecl*)(void*)>(findExport("GAME_sendNextWave"));
    auto toggleSpeed = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_togglePlaySpeed"));
    auto hasEnded = reinterpret_cast<int(__cdecl*)(void*)>(requireExport("GAME_hasEnded"));
    auto isWon = reinterpret_cast<int(__cdecl*)(void*)>(requireExport("GAME_isWon"));
    auto upgrade = reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_upgradeTower"));
    auto sell = reinterpret_cast<void(__cdecl*)(void*, int)>(requireExport("GAME_sellTower"));
    auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
    const int laser = nativeTowerId(mode, "Kilowatt Laser");
    const int megawatt = nativeTowerIdOrNone(mode, "Megawatt Laser");
    nlohmann::json trace{{"execution", "Original x86 machine code in native Win32; no CPU interpreter or Android system"},
        {"mode", scenario.modeName}, {"operation", operation}, {"laserTypeId", laser}, {"megawattTypeId", megawatt},
        {"initial", playerState(game)}, {"samples", nlohmann::json::array()}};
    std::ofstream traceOutput(output / (operation + "-trace.jsonl"));
    log("ORIGINAL_PLAYER " + trace.at("initial").dump());
    const ULONGLONG start = GetTickCount64(); ULONGLONG lastSample = 0, lastAutoWave = 0;
    bool built = false, sent = false, capturedBuild = false, capturedCombat = false, opening = false, clicked = false, categoryClicked = false, towerClicked = false, waveClicked = false, speedClicked = false;
    bool upgraded = false, sold = false, spedUp = false, ended = false, paused = false;
    // Bounded scenarios keep terminal evidence reproducible; interactive play stays open until the window closes.
    const ULONGLONG duration = operation == "play" ? std::numeric_limits<ULONGLONG>::max()
        : operation == "combat" || operation == "input" ? 35000
        : operation == "upgrade" ? 12000
        : operation == "flood" ? 240000
        : operation == "autoplay" ? 900000
        : 1000;
    bool windowClosed = false;
    activeGame = game;
    *reinterpret_cast<void**>(hostView.data() + 0x40) = hostMenu.data();
    *reinterpret_cast<void**>(hostMenu.data() + 0x15c) = game;
    if (operation == "play") ShowWindow(windowHandle, SW_SHOW);
    while (GetTickCount64() - start < duration) {
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (operation == "input" && elapsed >= 800 && !clicked) {
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(340, 340));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(340, 340)); clicked = true;
        }
        if (operation == "input" && elapsed >= 2000 && !categoryClicked) {
            capture(output / "original-input-category-ring.png", 480, 800);
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(325, 400));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(325, 400)); categoryClicked = true;
        }
        if (operation == "input" && elapsed >= 3500 && !towerClicked) {
            capture(output / "original-input-tower-ring.png", 480, 800);
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(260, 295));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(260, 295)); towerClicked = true;
        }
        if (operation == "input" && elapsed >= 5200 && !waveClicked) {
            capture(output / "original-input-built.png", 480, 800);
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(455, 774));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(455, 774)); waveClicked = true;
        }
        // The speed hot zone is x in [W-41.6,W), y in [32,80); this drives the original outer dispatcher.
        if (operation == "input" && elapsed >= 7000 && !speedClicked) {
            capture(output / "original-input-speed-before.png", 480, 800);
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(460, 56));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(460, 56)); speedClicked = true;
        }
        if ((operation == "combat" || operation == "upgrade" || operation == "autoplay") && elapsed >= 1000 && !built) {
            log("ORIGINAL_COMMAND build Kilowatt Laser 5,10"); build(game, laser, 5, 10);
            if (operation != "upgrade") {
                log("ORIGINAL_COMMAND build Kilowatt Laser 12,14"); build(game, laser, 12, 14);
            }
            built = true;
        }
        if (operation == "autoplay" && elapsed >= 700 && !spedUp) {
            toggleSpeed(game); toggleSpeed(game);
            log("ORIGINAL_COMMAND togglePlaySpeed x2 -> 4x playback");
            spedUp = true;
        }
        if (operation == "upgrade" && elapsed >= 3500 && !upgraded) {
            void* towers = reinterpret_cast<void**>(game)[4];
            const int towerId = idForCell(towers, 5, 10);
            log("ORIGINAL_COMMAND upgrade towerId=" + std::to_string(towerId) + " type=" + std::to_string(megawatt));
            if (towerId != -1 && megawatt != -1) upgrade(game, towerId, megawatt);
            upgraded = true;
        }
        if (operation == "upgrade" && elapsed >= 7000 && !sold) {
            void* towers = reinterpret_cast<void**>(game)[4];
            const int towerId = idForCell(towers, 5, 10);
            log("ORIGINAL_COMMAND sell towerId=" + std::to_string(towerId));
            if (towerId != -1) sell(game, towerId);
            sold = true;
        }
        // Auto-wave cadence is throttled because the original command API has no host-side debounce.
        if ((operation == "flood" || operation == "autoplay") && elapsed >= 1500 && elapsed - lastAutoWave >= (operation == "flood" ? 2500 : 4000)) {
            log("ORIGINAL_COMMAND nextWave elapsed=" + std::to_string(elapsed));
            nextWave(game); lastAutoWave = elapsed;
        }
        if (operation == "combat" && elapsed >= 2200 && !sent) {
            log("ORIGINAL_COMMAND nextWave"); nextWave(game); sent = true;
        }
        if (paused != (hostPauseRequested != 0)) {
            paused = hostPauseRequested != 0;
            if (!paused) { resume(game); log("ORIGINAL_COMMAND GAME_resume after host pause"); }
        }
        if (!paused) update(game);
        render(game);
        if (!opening && elapsed >= 600) { capture(output / "original-opening.png", 480, 800); opening = true; }
        if (built && !capturedBuild && elapsed >= 2000) { capture(output / "original-built.png", 480, 800); capturedBuild = true; }
        if (sent && !capturedCombat && elapsed >= 12000) { capture(output / "original-combat.png", 480, 800); capturedCombat = true; }
        // Record the original terminal evaluator the first time it reports an end.
        if (!ended && hasEnded(game) != 0) {
            ended = true;
            capture(output / (operation + "-end.png"), 480, 800);
            log(std::string("ORIGINAL_GAME_ENDED won=") + (isWon(game) ? "1" : "0") + " state=" + playerState(game).dump());
        }
        if (ended && operation != "play") break;
        if (elapsed >= lastSample) {
            auto state = playerState(game); state["hostElapsedMs"] = elapsed;
            trace["samples"].push_back(state); traceOutput << state.dump() << '\n'; traceOutput.flush(); lastSample = elapsed + 500;
        }
        SwapBuffers(windowDc);
        MSG message{}; bool closed = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) closed = true; TranslateMessage(&message); DispatchMessageW(&message); }
        if (closed) { windowClosed = true; break; }
        Sleep(16);
    }
    if (!windowClosed) {
        render(game); glFinish();
        capture(output / (operation == "combat" ? "original-combat-final.png" : operation == "input" ? "original-input-combat.png" : "original-first-frame.png"), 480, 800);
    }
    trace["ended"] = ended;
    trace["final"] = playerState(game);
    std::ofstream(output / (operation + "-result.json")) << trace.dump(2);
    log("ORIGINAL_FINAL " + trace.at("final").dump());
}

/** Drive the real original MENU/MAPMENU object with the host's Windows pointer. */
void originalMenuScene(const std::filesystem::path& output, const Scenario& scenario) {
    const bool scripted = scenario.operation == "menuauto";
    reinterpret_cast<void(__cdecl*)()>(findExport("PGL_init"))(); log("PGL_READY");
    glViewport(0, 0, 480, 800);
    auto menuAlloc = reinterpret_cast<void*(__cdecl*)()>(requireExport("MENU_alloc"));
    auto menuSetResolution = reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("MENU_setResolution"));
    auto menuChangePage = reinterpret_cast<void(__cdecl*)(void*, void*)>(requireExport("MENU_changePage"));
    auto menuShow = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_show"));
    auto menuUpdate = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_update"));
    auto menuRender = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_render"));
    auto worldSetResolution = reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("MENUWORLD_setResolution"));
    auto gameSetResolution = reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_setResolution"));
    auto gameUpdate = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"));
    auto gameRender = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"));
    auto gameHasEnded = reinterpret_cast<int(__cdecl*)(void*)>(requireExport("GAME_hasEnded"));
    auto gameSilence = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_silence"));
    auto menuEndGame = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_endGame"));
    void* menu = menuAlloc();
    if (!menu) throw std::runtime_error("Original MENU_alloc returned null");
    originalMenu = menu;
    activeGame = menu;
    *reinterpret_cast<void**>(hostView.data() + 0x40) = menu;
    log("ORIGINAL_MENU_ALLOC " + std::to_string(reinterpret_cast<std::uintptr_t>(menu)));
    // menu+0x110/+0x114 hold the FLOAT logical canvas size; MENU_render uses them as glOrthof bounds
    // and MENU_init passes them to CAMERA_setWindowSize. The original EAGLView passes the view size in
    // points, so use the host window size in FLOAT bit patterns: menu logical coordinates then equal
    // Windows pixels and no touch rescale is needed. Integer 480/800 here would be denormal floats.
    {
        const std::int32_t logicalWidth = static_cast<std::int32_t>(0x43f00000u);  // 480.0f
        const std::int32_t logicalHeight = static_cast<std::int32_t>(0x44480000u); // 800.0f
        menuSetResolution(menu, logicalWidth, logicalHeight);
        log("ORIGINAL_MENU_LOGICAL widthBits=" + std::to_string(logicalWidth) + " heightBits=" + std::to_string(logicalHeight));
        if (void* world = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x160)) worldSetResolution(world, logicalWidth, logicalHeight);
    }
    menuShow(menu);
    if (void* mainPage = findExport("mainMenuPage")) { menuChangePage(menu, mainPage); log("ORIGINAL_MENU_PAGE mainMenuPage"); }
    else log("ORIGINAL_MENU_PAGE_MISSING mainMenuPage");
    // Original vertical tab hit layout lives in libverde rodata (Ghidra 0x372e80.. -> ELF VA 0x362e80..).
    const auto* imageBase = reinterpret_cast<const unsigned char*>(images.front()->base);
    const auto rodataFloat = [&](std::uint32_t elfAddress) { return *reinterpret_cast<const float*>(imageBase + elfAddress); };
    const float tabFirstY = rodataFloat(0x362e80), tabPitch = rodataFloat(0x362e84);
    const float tabHitTop = rodataFloat(0x362ea8), tabHitBottom = rodataFloat(0x362eac);
    const float tabXMax = rodataFloat(0x362e8c);
    std::size_t tabCount = 0;
    if (const auto* pageBytes = static_cast<const unsigned char*>(*reinterpret_cast<void**>(menu)))
        while (tabCount < 16 && *reinterpret_cast<const void* const*>(pageBytes + 0x18 + 8 * tabCount)) ++tabCount;
    log("ORIGINAL_MENU_TABS count=" + std::to_string(tabCount) + " firstY=" + std::to_string(tabFirstY)
        + " pitch=" + std::to_string(tabPitch) + " hit=" + std::to_string(tabHitTop) + ".." + std::to_string(tabHitBottom)
        + " xMax=" + std::to_string(tabXMax));
    const auto savePath = saveStatePath(output);
    // "continue" runs the original GAME_fromIBS restore path over bytes written by an earlier run.
    const bool continueMode = scripted && (scenario.modeName == "continue" || scenario.modeName == "Continue");
    const bool longCampaign = scripted && (scenario.modeName == "campaign" || scenario.modeName == "Campaign");
    if (continueMode) {
        // Only read the bytes now; the original transition runs once MAPMENU exists in the loop below.
        if (readSaveState(savePath, loadedIbsBytes))
            log("ORIGINAL_SAVE_CONTINUE_READY file=" + savePath.string() + " bytes=" + std::to_string(loadedIbsBytes.size()));
        else log("ORIGINAL_SAVE_CONTINUE_NOFILE " + savePath.string());
    }
    const ULONGLONG start = GetTickCount64();
    // Bounded so the fixed bridge runner can finish; interactive clicks are still accepted while it runs.
    // Interactive menu runs until the user closes the window; scripted probes stay bounded.
    const ULONGLONG duration = scripted ? (longCampaign ? 900000 : 45000) : (std::numeric_limits<ULONGLONG>::max)();
    log("ORIGINAL_MENU_DURATION ms=" + std::to_string(duration) + " campaign=" + std::string(longCampaign ? "1" : "0")
        + " continue=" + std::string(continueMode ? "1" : "0"));
    bool capturedMain = false, capturedMap = false, capturedGame = false, capturedPause = false, windowClosed = false;
    bool hudCaptured = false, pauseVerified = false, tutorialCaptured = false, saveTested = false, combatCaptured = false, endCaptured = false;
    bool endCapturedLate = false;
    ULONGLONG endCapturedAt = 0, endClickAt = 0, endNextSample = 0;
    double lastEndNonWhite = 0.0;
    unsigned endClickStep = 0;
    bool pauseFrozen = false, resumed = false, resumeVerified = false;
    int tutorialTap = 0, buildStep = 0;
    bool tutorialDone = false, continueRestored = false, loggedPreUpdate = false;
    bool hotkeySaveTested = false, hotkeyLoadTested = false;
    bool continueFadeStart = false, continueFadeEnd = false, capturedTowers = false;
    unsigned long long frameCount = 0;
    ULONGLONG nextHeartbeat = 0;
    int guiBuildIndex = 0, guiBuildPhase = 0;
    ULONGLONG guiBuildPhaseAt = 0;
    int apiBuildStep = 0, autoWaveCount = 0;
    ULONGLONG nextTutorialTap = 0, tutorialDoneAt = 0;
    ULONGLONG nextStatLog = 0;
    void* lastPage = nullptr; void* lastGame = nullptr;
    ULONGLONG gameStartAt = 0, nextTabClick = 1800, mapSelectAt = 0, pauseAt = 0, resumeAt = 0;
    int pauseClockBefore = 0, pauseClock1500 = -1, pauseClock3000 = -1;
    std::size_t tabIndex = 0;
    int mapStep = 0;
    ULONGLONG mapStepAt = 0;
    // MAPMENU_touchesEnded only starts the selected entry when the tap is inside these rodata bounds.
    const float mapHitMinX = rodataFloat(0x362e1c), mapHitMinY = rodataFloat(0x362b58), mapHitMaxY = rodataFloat(0x362e54);
    log("ORIGINAL_MAP_HIT minX=" + std::to_string(mapHitMinX) + " minY=" + std::to_string(mapHitMinY) + " maxY=" + std::to_string(mapHitMaxY));
    if (!scripted) ShowWindow(windowHandle, SW_SHOW);
    while (GetTickCount64() - start < duration) {
        const ULONGLONG elapsed = GetTickCount64() - start;
        ++frameCount;
        // Heartbeat: distinguishes a stalled guest call from a merely slow run.
        if (scripted && longCampaign && elapsed >= nextHeartbeat) {
            nextHeartbeat = elapsed + 5000;
            void* heartbeatGame = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
            log("ORIGINAL_CAMPAIGN_HEARTBEAT frame=" + std::to_string(frameCount) + " "
                + (heartbeatGame ? playerState(heartbeatGame).dump() : std::string("nogame")));
        }
        // Frame boundary: nothing from the previous frame is in flight, so a replaced game can be freed.
        if (pendingGameFree) {
            void* dying = pendingGameFree;
            pendingGameFree = nullptr;
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("GAME_free"))(dying);
            log("ORIGINAL_GAME_FREED_AT_BOUNDARY old=" + std::to_string(reinterpret_cast<std::uintptr_t>(dying)));
        }
        void* game = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
        void* page = *reinterpret_cast<void**>(menu);
        const bool showing = *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4) != 0;
        if (page != lastPage) { lastPage = page; log("ORIGINAL_MENU_PAGE_CHANGED " + std::to_string(reinterpret_cast<std::uintptr_t>(page))); }
        if (page == findExport("startGamePage") && mapSelectAt == 0) mapSelectAt = elapsed;
        if (game && game != lastGame) {
            lastGame = game; gameStartAt = elapsed;
            nextStatLog = elapsed + 10000;
            nextTutorialTap = elapsed + 1500;
            gameSetResolution(game, 480, 800); glViewport(0, 0, 480, 800);
            log("ORIGINAL_MENU_GAME_STARTED " + std::to_string(reinterpret_cast<std::uintptr_t>(game)));
            log("ORIGINAL_GAME_INITIAL " + playerState(game).dump());
        }
        if (scripted && continueMode && game && gameStartAt && !resumeVerified && elapsed >= gameStartAt + 3000) {
            resumeVerified = true;
            log("ORIGINAL_SAVE_CONTINUE_CLOCK " + playerState(game).dump());
        }
        if (scripted && game && gameStartAt && !continueMode && !tutorialCaptured && elapsed >= gameStartAt + 2000) {
            capture(output / "menu-tutorial.png", 480, 800); tutorialCaptured = true;
        }
        // The original tutorial is a camera/tap state machine; TUTORIAL_isOver gates real gameplay.
        if (scripted && !continueMode && game && gameStartAt && !tutorialDone && tutorialTap < 16 && elapsed >= nextTutorialTap) {
            nextTutorialTap = elapsed + 2500;
            void* tutorial = *reinterpret_cast<void**>(static_cast<unsigned char*>(game) + 0xa3c);
            const auto tutorialIsOver = reinterpret_cast<int(__cdecl*)(void*)>(findExport("TUTORIAL_isOver"));
            if (tutorial && tutorialIsOver(tutorial)) {
                tutorialDone = true; tutorialDoneAt = elapsed;
                capture(output / "menu-tutorial-done.png", 480, 800);
                log("ORIGINAL_TUTORIAL_DONE " + playerState(game).dump());
            } else {
                const int step = tutorial ? reinterpret_cast<int*>(tutorial)[3] : -1;
                log("ORIGINAL_TUTORIAL_TAP step=" + std::to_string(step) + " tap=" + std::to_string(tutorialTap));
                SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(240, 635));
                SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(240, 635));
                ++tutorialTap;
            }
        }
        if (scripted && !continueMode && game && tutorialDone && !hudCaptured && elapsed >= tutorialDoneAt + 1500) {
            capture(output / "menu-hud.png", 480, 800);
            log("ORIGINAL_HUD " + playerState(game).dump());
            hudCaptured = true;
        }
        // Reuse the proven original input coordinates: build ring, category, tower, wave and speed.
        if (scripted && !continueMode && game && tutorialDone && buildStep < 5
            && elapsed >= tutorialDoneAt + 2500 + static_cast<ULONGLONG>(buildStep) * 1500) {
            static const int points[5][2] = {{340, 340}, {325, 400}, {260, 295}, {455, 774}, {460, 56}};
            const int pixelX = points[buildStep][0], pixelY = points[buildStep][1];
            log("ORIGINAL_BUILD_CLICK step=" + std::to_string(buildStep) + " " + std::to_string(pixelX) + "," + std::to_string(pixelY) + " " + playerState(game).dump());
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pixelX, pixelY));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(pixelX, pixelY));
            if (buildStep == 3) capture(output / "menu-built-after.png", 480, 800);
            if (buildStep == 2) capture(output / "menu-built.png", 480, 800);
            if (buildStep == 4) capture(output / "menu-wave-speed.png", 480, 800);
            ++buildStep;
        }
        if (scripted && !continueMode && game && tutorialDone && buildStep == 5 && !combatCaptured && elapsed >= tutorialDoneAt + 11000) {
            capture(output / "menu-combat.png", 480, 800);
            log("ORIGINAL_COMBAT " + playerState(game).dump());
            combatCaptured = true;
        }
        // Real Windows keyboard messages exercise the interactive save/continue hotkeys (short probes only:
        // swapping a live game mid-campaign slows the long run and is already verified).
        if (scripted && !continueMode && !longCampaign && game && tutorialDone && !hotkeySaveTested && elapsed >= tutorialDoneAt + 9500) {
            hotkeySaveTested = true;
            log("ORIGINAL_HOTKEY_TEST save");
            SendMessageW(windowHandle, WM_KEYDOWN, VK_F5, 0);
        }
        if (scripted && !continueMode && !longCampaign && game && tutorialDone && !hotkeyLoadTested && elapsed >= tutorialDoneAt + 10500) {
            hotkeyLoadTested = true;
            log("ORIGINAL_HOTKEY_TEST load");
            SendMessageW(windowHandle, WM_KEYDOWN, VK_F9, 0);
            log("ORIGINAL_HOTKEY_TEST result game=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c))));
        }
        if (scripted && longCampaign && game && gameStartAt && elapsed >= nextStatLog) {
            nextStatLog = elapsed + 10000;
            log("ORIGINAL_CAMPAIGN_STAT " + playerState(game).dump());
        }
        // Once the original reports the game over (or the endgame page is up), the scripted campaign input must
        // stop: a stray wave click there was routed into the menu and started a whole new game
        // (campaign-20261015-054301:1540-1551), which replaced the settlement page under the capture gate.
        const bool gameIsOver = game && reinterpret_cast<int(__cdecl*)(void*)>(findExport("GAME_hasEnded"))(game) != 0;
        // The verified three-tap first tower stays (BUILD_CLICK steps 0-2); the extra ring taps are off because
        // only 2 of 7 candidate cells ever charged money, while the original build entry point now demonstrably
        // does (see the API block below), so the campaign uses that path for the remaining towers.
        const bool guiMultiBuildEnabled = false;
        if (scripted && longCampaign && guiMultiBuildEnabled && game && tutorialDone && !gameIsOver && !endCaptured && guiBuildIndex < 6) {
            // Cells chosen around the proven (340,340) build so the ring opens on buildable field ground.
            static const int guiCells[6][2] = {{300, 340}, {380, 340}, {340, 300}, {340, 380}, {300, 420}, {380, 420}};
            const int cellX = guiCells[guiBuildIndex][0], cellY = guiCells[guiBuildIndex][1];
            const ULONGLONG due = tutorialDoneAt + 13000 + static_cast<ULONGLONG>(guiBuildIndex) * 3200;
            if (guiBuildPhase == 0 && elapsed >= due) {
                log("ORIGINAL_CAMPAIGN_GUI_BUILD cell=" + std::to_string(cellX) + "," + std::to_string(cellY) + " " + playerState(game).dump());
                SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cellX, cellY));
                SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(cellX, cellY));
                guiBuildPhase = 1; guiBuildPhaseAt = elapsed;
            } else if (guiBuildPhase == 1 && elapsed >= guiBuildPhaseAt + 800) {
                // Captured ring geometry (menu-gui-cell-*.png): the Potato Gun sits at ring centre + (+23,-75)
                // and the ring opens on the tapped cell, so one tap there selects and places it.
                capture(output / ("menu-gui-cell-" + std::to_string(guiBuildIndex) + ".png"), 480, 800);
                SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cellX + 23, cellY - 75));
                SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(cellX + 23, cellY - 75));
                guiBuildPhase = 2; guiBuildPhaseAt = elapsed;
            } else if (guiBuildPhase == 2 && elapsed >= guiBuildPhaseAt + 800) {
                capture(output / ("menu-gui-build-" + std::to_string(guiBuildIndex) + ".png"), 480, 800);
                log("ORIGINAL_CAMPAIGN_GUI_BUILD_DONE " + playerState(game).dump());
                ++guiBuildIndex; guiBuildPhase = 0;
            }
        }
        // Campaign verification: drive the ORIGINAL build entry point so the natural layout can hold 26 waves.
        // The cost/affordability check stays inside the original function; the host only supplies the request.
        // Re-enabled on evidence: the earlier libverde.so+0x267f08 fault came from the removed
        // TOWERS_getIdForCell probe (wrong first argument), not from GAME_buildTower itself.
        const bool apiBuildEnabled = true;
        if (scripted && longCampaign && apiBuildEnabled && game && tutorialDone && !gameIsOver && !endCaptured
            && apiBuildStep < 40 && elapsed >= tutorialDoneAt + 15000 + static_cast<ULONGLONG>(apiBuildStep) * 4000) {
            void* mode = *reinterpret_cast<void**>(game);
            // PlainPlane.map: width 11, height 16; terrain 1 (buildable field) spans cols 1..9, rows 4..11.
            // Measured: (5,8)/(8,8)/(6,8) charge the original 10 money cost while (2,5)/(5,5)/(8,5)/(2,8) do
            // not build, so the scan walks the field and only cells the original accepts take effect.
            // The first 12 steps build; the rest upgrade the cells that were built, because level-1 towers
            // alone could not hold the later waves (defeat at sim 97.85s, wave ~3 of 26).
            static const int cells[12][2] = {{5, 8}, {8, 8}, {6, 8}, {7, 8}, {4, 8}, {5, 9},
                                             {8, 9}, {4, 9}, {7, 9}, {6, 9}, {3, 8}, {9, 8}};
            const bool upgrade = apiBuildStep >= 12;
            const int cellIndex = upgrade ? (apiBuildStep - 12) % 6 : apiBuildStep % 12;
            const int towerId = nativeTowerIdOrNone(mode, "Kilowatt Laser");
            log(std::string("ORIGINAL_CAMPAIGN_API_TRY step=") + std::to_string(apiBuildStep)
                + " upgrade=" + std::to_string(upgrade) + " id=" + std::to_string(towerId)
                + " cell=" + std::to_string(cells[cellIndex][0]) + "," + std::to_string(cells[cellIndex][1])
                + " " + playerState(game).dump());
            if (upgrade) {
                // GAME_upgradeTower(game, x, y) enqueues command type 2 (pseudocode.c:4693-4710); the original
                // validates cost/level and charges the money itself.
                reinterpret_cast<void(__cdecl*)(void*, int, int)>(findExport("GAME_upgradeTower"))(
                    game, cells[cellIndex][0], cells[cellIndex][1]);
            } else if (towerId >= 0) {
                reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(findExport("GAME_buildTower"))(
                    game, towerId, cells[cellIndex][0], cells[cellIndex][1]);
            }
            log(std::string("ORIGINAL_CAMPAIGN_API_DONE step=") + std::to_string(apiBuildStep)
                + " upgrade=" + std::to_string(upgrade)
                + " cell=" + std::to_string(cells[cellIndex][0]) + "," + std::to_string(cells[cellIndex][1])
                + " " + playerState(game).dump());
            ++apiBuildStep;
        }
        // Evidence for where the API-assisted towers actually landed on the map.
        if (scripted && longCampaign && game && tutorialDone && !capturedTowers && elapsed >= tutorialDoneAt + 22000) {
            capturedTowers = true;
            capture(output / "menu-towers.png", 480, 800);
            log("ORIGINAL_CAMPAIGN_TOWERS " + playerState(game).dump());
        }
        // Once the original reports the game over (or the endgame page is up), the scripted campaign input must
        // stop: a stray wave click there was routed into the menu and started a whole new game
        // (campaign-20261015-054301:1540-1551), which replaced the settlement page under the capture gate.
        if (scripted && longCampaign && game && tutorialDone && !gameIsOver && !endCaptured
            && autoWaveCount < 25 && elapsed >= tutorialDoneAt + 20000 + static_cast<ULONGLONG>(autoWaveCount) * 22000) {
            ++autoWaveCount;
            log("ORIGINAL_CAMPAIGN_WAVE_CLICK " + std::to_string(autoWaveCount) + " " + playerState(game).dump());
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(455, 774));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(455, 774));
        }
        if (scripted && longCampaign && !endCaptured && page == findExport("endOfGamePage")) {
            capture(output / "menu-endgame.png", 480, 800);
            log(std::string("ORIGINAL_CAMPAIGN_ENDGAME page=") + std::to_string(reinterpret_cast<std::uintptr_t>(page))
                + " nonWhite=" + std::to_string(frameNonWhiteFraction(480, 800))
                + (game ? " " + playerState(game).dump() : std::string()));
            endCaptured = true;
            endCapturedAt = elapsed;
        }
        // A blank or washed-out first frame proves only that the page pointer changed. Keep the original
        // MENU_update/render running and require the SAME endOfGamePage plus ended==true to persist while the
        // fade settles; page/state mismatch is reported and the click probe is skipped instead of being
        // presented as a visible result page. Ratios never override a page/state mismatch.
        if (longCampaign && endCaptured && !endCapturedLate && elapsed >= endNextSample) {
            endNextSample = elapsed + 300;
            const void* currentPage = *reinterpret_cast<void**>(menu);
            const bool samePage = currentPage == findExport("endOfGamePage");
            const bool ended = game && reinterpret_cast<int(__cdecl*)(void*)>(findExport("GAME_hasEnded"))(game) != 0;
            const float fade = *reinterpret_cast<float*>(static_cast<unsigned char*>(menu) + 0xec);
            const double nonWhite = frameNonWhiteFraction(480, 800);
            const bool steady = std::fabs(nonWhite - lastEndNonWhite) <= 0.01;
            lastEndNonWhite = nonWhite;
            log("ORIGINAL_CAMPAIGN_ENDGAME_SAMPLE elapsed=" + std::to_string(elapsed - endCapturedAt)
                + " samePage=" + std::to_string(samePage) + " ended=" + std::to_string(ended)
                + " showing=" + std::to_string(*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4))
                + " fade=" + std::to_string(fade) + " nonWhite=" + std::to_string(nonWhite)
                + " page=" + std::to_string(reinterpret_cast<std::uintptr_t>(currentPage))
                + (game ? " " + playerState(game).dump() : std::string("nogame")));
            if (!samePage || !ended) {
                log("ORIGINAL_CAMPAIGN_ENDGAME_PAGE_LOST samePage=" + std::to_string(samePage)
                    + " ended=" + std::to_string(ended) + " (no stable capture, no click probe)");
                endCapturedLate = true;
                endClickStep = 3;
            } else if ((elapsed >= endCapturedAt + 2000 && fade <= 0.02f && steady && nonWhite >= 0.05)
                || elapsed >= endCapturedAt + 12000) {
                capture(output / "menu-endgame-stable.png", 480, 800);
                log("ORIGINAL_CAMPAIGN_ENDGAME_STABLE samePage=" + std::to_string(samePage)
                    + " ended=" + std::to_string(ended)
                    + " nonWhite=" + std::to_string(nonWhite)
                    + " fade=" + std::to_string(fade) + " steady=" + std::to_string(steady)
                    + " timedOut=" + std::to_string(elapsed >= endCapturedAt + 12000)
                    + " showing=" + std::to_string(*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4))
                    + " page=" + std::to_string(reinterpret_cast<std::uintptr_t>(currentPage))
                    + (game ? " " + playerState(game).dump() : std::string("nogame")));
                endCapturedLate = true;
                endClickAt = elapsed + 400;
            }
        }
        // Clickability check on the visible endgame page: real Windows input through the original touch router;
        // a changed page pointer after a click is the evidence (no page change is reported as unverified).
        // Clickability check on the visible endgame page: real Windows input through the original touch router.
        // Targets come from the verified frame (menu-endgame-stable.png: "MAIN MENU | PLAY AGAIN" row, y~770;
        // PLAY AGAIN on the right, MAIN MENU on the left). A changed page pointer or a fresh game pointer after
        // the click is the evidence; anything unchanged is reported as unverified.
        if (longCampaign && endCapturedLate && endClickStep < 2 && elapsed >= endClickAt) {
            static const int candidates[2][2] = {{200, 770}, {95, 770}};
            static const char* const labels[2] = {"PLAY_AGAIN", "MAIN_MENU"};
            const void* before = *reinterpret_cast<void**>(menu);
            const void* gameBefore = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON,
                MAKELPARAM(candidates[endClickStep][0], candidates[endClickStep][1]));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0,
                MAKELPARAM(candidates[endClickStep][0], candidates[endClickStep][1]));
            const void* after = *reinterpret_cast<void**>(menu);
            const void* gameAfter = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
            log(std::string("ORIGINAL_CAMPAIGN_ENDGAME_CLICK step=") + std::to_string(endClickStep)
                + " button=" + labels[endClickStep]
                + " at=" + std::to_string(candidates[endClickStep][0]) + "," + std::to_string(candidates[endClickStep][1])
                + " pageBefore=" + std::to_string(reinterpret_cast<std::uintptr_t>(before))
                + " pageAfter=" + std::to_string(reinterpret_cast<std::uintptr_t>(after))
                + " gameBefore=" + std::to_string(reinterpret_cast<std::uintptr_t>(gameBefore))
                + " gameAfter=" + std::to_string(reinterpret_cast<std::uintptr_t>(gameAfter))
                + (before != after ? " CHANGED" : " unchanged")
                + (gameBefore != gameAfter ? " GAME_REPLACED" : ""));
            ++endClickStep;
            endClickAt = elapsed + 600;
        }
        if (longCampaign && endCapturedLate && endClickStep >= 2) { log("ORIGINAL_CAMPAIGN_DONE elapsed=" + std::to_string(elapsed)); break; }
        if (!capturedMain && elapsed >= 1500) { capture(output / "menu-main.png", 480, 800); capturedMain = true; }
        // Scripted mode clicks each original tab centre (from the rodata hit layout) and records the page result.
        if (scripted && game == nullptr && capturedMain && tabIndex < tabCount && elapsed >= nextTabClick && page == findExport("mainMenuPage")) {
            nextTabClick = elapsed + 1200;
            const float clickY = tabFirstY + static_cast<float>(tabIndex) * tabPitch + (tabHitTop + tabHitBottom) * 0.5f;
            const int clickX = static_cast<int>(tabXMax * 0.5f), pixelY = static_cast<int>(clickY);
            const std::uintptr_t pageBefore = reinterpret_cast<std::uintptr_t>(page);
            log("MENU_TAB_CLICK index=" + std::to_string(tabIndex) + " " + std::to_string(clickX) + "," + std::to_string(pixelY));
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(clickX, pixelY));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(clickX, pixelY));
            const std::uintptr_t pageAfter = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(menu));
            log("MENU_TAB_RESULT before=" + std::to_string(pageBefore) + " after=" + std::to_string(pageAfter));
            ++tabIndex;
            // Any incidental page (options/scores) goes back to the main menu so the next tab is tried.
            if (*reinterpret_cast<void**>(menu) != findExport("mainMenuPage") && *reinterpret_cast<void**>(menu) != findExport("startGamePage")) {
                reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_backToMainMenu"))(menu);
                reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_show"))(menu);
                log("MENU_TAB_REVERT");
            }
        }
        // Scripted level-select flow: switch to the campaign tab, then tap the selected entry.
        if (scripted && game == nullptr && mapSelectAt != 0 && mapStep == 0 && elapsed >= mapSelectAt + 800) {
            mapStep = 1; mapStepAt = elapsed + 1200;
            log("MENU_MAP_TAB campaign 112,785");
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(112, 785));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(112, 785));
        }
        // Continue: rebuild the saved GAME and install it through the original restore entry (no MAPMENU callback:
        // that callback expects a GAMEMODE and would create a fresh game).
        if (scripted && continueMode && !continueRestored && game == nullptr && page == findExport("startGamePage") && mapStep >= 1 && elapsed >= mapStepAt) {
            continueRestored = true;
            if (loadedIbsBytes.empty()) {
                log("ORIGINAL_SAVE_CONTINUE_NOBYTES " + savePath.string());
            } else {
            void* ibs = reinterpret_cast<void*(__cdecl*)(void*, unsigned, unsigned)>(findExport("IBS_wrap"))(
                loadedIbsBytes.data(), static_cast<unsigned>(loadedIbsBytes.size()), 0);
            void* loaded = reinterpret_cast<void*(__cdecl*)(void*)>(findExport("GAME_fromIBS"))(ibs);
            log("ORIGINAL_SAVE_CONTINUE game=" + std::to_string(reinterpret_cast<std::uintptr_t>(loaded))
                + (loaded ? " state=" + playerState(loaded).dump() : std::string(" (failed)")));
            // Discriminating evidence for the QA save-audit: does the restored GAMEMODE carry a map pointer?
            {
                void* gamemode = loaded ? *reinterpret_cast<void**>(loaded) : nullptr;
                std::ostringstream modeLog;
                modeLog << "ORIGINAL_SAVE_CONTINUE_MODE game=0x" << std::hex << reinterpret_cast<std::uintptr_t>(loaded)
                        << " mode=0x" << reinterpret_cast<std::uintptr_t>(gamemode);
                if (gamemode) {
                    const auto* fields = reinterpret_cast<const std::uint32_t*>(gamemode);
                    modeLog << " field20=0x" << fields[0x20 / 4] << " field24=0x" << fields[0x24 / 4] << " field2c=0x" << fields[0x2c / 4];
                }
                log(modeLog.str());
            }
            if (loaded) {
                installRestoredGame(menu, loaded);
                log("ORIGINAL_SAVE_CONTINUE_STARTED game=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c)))
                    + " showing=" + std::to_string(*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4)));
            }
            }
        }
        if (scripted && !continueMode && game == nullptr && mapStep >= 1 && mapStep <= 3 && elapsed >= mapStepAt) {
            if (mapStep == 1) capture(output / "menu-campaign.png", 480, 800);
            // The campaign page draws the selected map with an explicit TAP TO START prompt; tap its tile centre.
            const int mapX = 240, mapY = 350;
            log("MENU_MAP_CLICK step=" + std::to_string(mapStep) + " " + std::to_string(mapX) + "," + std::to_string(mapY));
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(mapX, mapY));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(mapX, mapY));
            log("MENU_MAP_CLICK_RESULT game=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c))));
            ++mapStep; mapStepAt = elapsed + 2000;
        }
        if (scripted && !continueMode && !longCampaign && !capturedPause && game && tutorialDone && elapsed >= tutorialDoneAt + 13000) {
            pauseClockBefore = reinterpret_cast<int*>(game)[10];
            log("ORIGINAL_MENU_PAUSE_REQUESTED page=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(menu)))
                + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(game)) + " clock=" + std::to_string(pauseClockBefore));
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_backToMainMenu"))(menu);
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_show"))(menu);
            pauseAt = elapsed; capturedPause = true;
        }
        if (scripted && capturedPause && !pauseVerified && elapsed >= pauseAt + 1500) {
            capture(output / "menu-pause.png", 480, 800);
            pauseClock1500 = currentGameClock(menu);
            log(std::string("ORIGINAL_MENU_PAUSE_STATE page=") + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(menu)))
                + " showing=" + std::to_string(*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4))
                + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c)))
                + " clockBefore=" + std::to_string(pauseClockBefore) + " clockAfter=" + std::to_string(pauseClock1500));
            pauseVerified = true;
        }
        if (scripted && pauseVerified && !pauseFrozen && elapsed >= pauseAt + 3000) {
            pauseClock3000 = currentGameClock(menu);
            log("ORIGINAL_MENU_PAUSE_FREEZE clockAt1500=" + std::to_string(pauseClock1500) + " clockAt3000=" + std::to_string(pauseClock3000)
                + " frozen=" + std::string(pauseClock3000 == pauseClock1500 ? "1" : "0"));
            pauseFrozen = true;
        }
        if (scripted && pauseFrozen && !resumed && elapsed >= pauseAt + 4500) {
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_hide"))(menu);
            log("ORIGINAL_MENU_RESUME_REQUESTED showing=" + std::to_string(*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4)));
            resumed = true; resumeAt = elapsed;
        }
        if (scripted && resumed && !resumeVerified && elapsed >= resumeAt + 2000) {
            log(std::string("ORIGINAL_MENU_RESUME_STATE page=") + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(menu)))
                + " showing=" + std::to_string(*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4))
                + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c)))
                + " clockAtResume=" + std::to_string(currentGameClock(menu)));
            resumeVerified = true;
        }
        if (scripted && !continueMode && !longCampaign && game && capturedPause && !saveTested && elapsed >= pauseAt + 8000) {
            saveTested = true;
            void* obs = reinterpret_cast<void*(__cdecl*)(void*)>(findExport("GAME_save"))(game);
            const auto* header = reinterpret_cast<const std::uint32_t*>(obs);
            void* bytes = reinterpret_cast<void*>(header[0]);
            const std::uint32_t length = header[1];
            writeSaveState(savePath, bytes, length);
            std::ostringstream saveLog;
            saveLog << "ORIGINAL_SAVE file=" << savePath.string() << " bytes=" << length << " state=" << playerState(game).dump();
            log(saveLog.str());
            // Real round-trip: wrap the same original bytes in an IBS and rebuild through GAME_fromIBS.
            if (readSaveState(savePath, loadedIbsBytes)) {
                void* ibs = reinterpret_cast<void*(__cdecl*)(void*, unsigned, unsigned)>(findExport("IBS_wrap"))(
                    loadedIbsBytes.data(), static_cast<unsigned>(loadedIbsBytes.size()), 0);
                void* loaded = reinterpret_cast<void*(__cdecl*)(void*)>(findExport("GAME_fromIBS"))(ibs);
                log("ORIGINAL_LOAD_IBS bytes=" + std::to_string(loadedIbsBytes.size()) + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(loaded))
                    + (loaded ? " state=" + playerState(loaded).dump() : std::string(" (failed)")));
                if (loaded) {
                    reinterpret_cast<void(__cdecl*)(void*)>(findExport("GAME_resume"))(loaded);
                    *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c) = loaded;
                    reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_hide"))(menu);
                    log("ORIGINAL_LOAD_ACTIVE showing=" + std::to_string(*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4)));
                }
            }
        }
        if (game == nullptr || showing) {
            if (continueMode && continueRestored && !continueFadeStart) {
                continueFadeStart = true;
                const auto* bytes = static_cast<const unsigned char*>(menu);
                std::ostringstream fade;
                fade << "ORIGINAL_SAVE_CONTINUE_FADE c=" << static_cast<int>(bytes[0xc]) << "/" << static_cast<int>(bytes[0xd])
                     << " showing=" << static_cast<int>(bytes[4]) << " alpha=" << *reinterpret_cast<const float*>(bytes + 0xec);
                log(fade.str());
            }
            menuUpdate(menu); menuRender(menu);
        }
        else {
            if (continueMode && continueRestored && !continueFadeEnd) {
                continueFadeEnd = true;
                log("ORIGINAL_SAVE_CONTINUE_FADE_END");
            }
            if (!loggedPreUpdate) {
                loggedPreUpdate = true;
                const auto* bytes = static_cast<const unsigned char*>(game);
                std::ostringstream pre;
                pre << "ORIGINAL_GAME_PREUPDATE game=0x" << std::hex << reinterpret_cast<std::uintptr_t>(game)
                    << " simMs=0x" << *reinterpret_cast<const std::uint32_t*>(bytes + 0x28)
                    << " a38=0x" << *reinterpret_cast<const std::uint32_t*>(bytes + 0xa38)
                    << " be4=0x" << *reinterpret_cast<const std::uint32_t*>(bytes + 0xbe4)
                    << " a50=" << std::dec << *reinterpret_cast<const double*>(bytes + 0xa50)
                    << " hostMs=" << GetTickCount64();
                log(pre.str());
            }
            if (gameHasEnded(game)) {
                // The endgame score path reads the replay at game+0xb50 via REPLAY_getData and hashes it;
                // call that original accessor here so a fault is attributed to the replay, not to MENU_endGame.
                void* replay = *reinterpret_cast<void**>(static_cast<unsigned char*>(game) + 0xb50);
                log("ORIGINAL_MENU_ENDGAME_PRE replay=" + std::to_string(reinterpret_cast<std::uintptr_t>(replay))
                    + " " + playerState(game).dump());
                // A natural victory takes the same MENU_endGame path but must be reported distinctly, and the
                // unlock chain it runs writes the unlocked* keys this host persists.
                if (reinterpret_cast<int(__cdecl*)(void*)>(findExport("GAME_isWon"))(game))
                    log("ORIGINAL_MENU_GAME_WON " + playerState(game).dump());
                if (replay) {
                    if (auto replayGetData = reinterpret_cast<void*(__cdecl*)(void*, int*)>(findExport("REPLAY_getData"))) {
                        int replayLength = 0;
                        void* replayData = replayGetData(replay, &replayLength);
                        log("ORIGINAL_MENU_REPLAY data=" + std::to_string(reinterpret_cast<std::uintptr_t>(replayData))
                            + " length=" + std::to_string(replayLength));
                    }
                }
                gameSilence(game); menuEndGame(menu); log("ORIGINAL_MENU_GAME_ENDED");
            }
            else gameUpdate(game);
            gameRender(game);
            // NOTE: HeapValidate can itself raise the heap-corruption exception, which aborts a run that
            // would otherwise continue; it is deliberately not called per frame. Corruption is hunted by
            // tracking the guest allocator below instead.
        }
        if (!capturedMap && page && page == findExport("startGamePage")) { capture(output / "menu-mapselect.png", 480, 800); capturedMap = true; }
        if (!capturedGame && game) { capture(output / "menu-in-game.png", 480, 800); capturedGame = true; }
        SwapBuffers(windowDc);
        MSG message{}; bool closed = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) closed = true; TranslateMessage(&message); DispatchMessageW(&message); }
        if (closed) { windowClosed = true; break; }
        Sleep(16);
    }
    // Render exactly what the loop was rendering so the final frame is never a stale/undefined buffer.
    if (!windowClosed) {
        void* finalGameForCapture = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
        const bool finalShowing = *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4) != 0;
        if (finalGameForCapture && !finalShowing) { gameUpdate(finalGameForCapture); gameRender(finalGameForCapture); }
        else { menuUpdate(menu); menuRender(menu); }
        glFinish();
        capture(output / (scripted ? "menu-final.png" : "menu-pause.png"), 480, 800);
    }
    void* finalGame = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
    log(std::string("ORIGINAL_MENU_FINAL page=") + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(menu)))
        + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(finalGame))
        + (finalGame ? " state=" + playerState(finalGame).dump() : std::string()));
}

/** Report a native Windows fault with its corresponding original ELF address. */
LONG WINAPI nativeFault(EXCEPTION_POINTERS* exception) {
    const auto ip = static_cast<std::uint32_t>(exception->ContextRecord->Eip);
    std::ostringstream message; message << "NATIVE_FAULT code=" << std::hex << exception->ExceptionRecord->ExceptionCode << " ip=" << ip;
    for (const auto& image : images) {
        const auto base = reinterpret_cast<std::uint32_t>(image->base);
        if (ip >= base && ip < base + image->file.size() + 0x400000) message << " candidate=" << image->name << "+0x" << ip - base;
    }
    if (exception->ExceptionRecord->NumberParameters > 1) message << " access=" << exception->ExceptionRecord->ExceptionInformation[1];
    message << " symbol=" << describeImageAddress(ip);
    log(message.str());
    std::uint32_t frame = exception->ContextRecord->Ebp;
    const auto* thread = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    for (unsigned depth = 0; depth < 12; ++depth) {
        if (frame < reinterpret_cast<std::uintptr_t>(thread->StackLimit) || frame + 8 >= reinterpret_cast<std::uintptr_t>(thread->StackBase)) break;
        const auto* words = reinterpret_cast<const std::uint32_t*>(frame);
        const std::uint32_t caller = words[1];
        for (const auto& image : images) {
            const auto base = reinterpret_cast<std::uint32_t>(image->base);
            if (caller >= base && caller < base + image->file.size()) {
                std::ostringstream line;
                line << "NATIVE_FRAME " << image->name << "+0x" << std::hex << caller - base << " " << describeImageAddress(caller);
                log(line.str());
            }
        }
        if (words[0] <= frame) break; frame = words[0];
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

/** Locate the first matching byte, matching the original libverde libc import. */
void* __cdecl nativeMemchr(void* block, int value, std::size_t count) {
    auto* bytes = static_cast<unsigned char*>(block);
    for (std::size_t i = 0; i < count; ++i) if (bytes[i] == static_cast<unsigned char>(value)) return bytes + i;
    return nullptr;
}

/** bcopy(source, destination, count): the BSD copy with the reversed argument order. */
void __cdecl nativeBcopy(const void* source, void* destination, std::size_t count) {
    if (count) {
        log("PLATFORM_BCOPY src=" + std::to_string(reinterpret_cast<std::uintptr_t>(source))
            + " dst=" + std::to_string(reinterpret_cast<std::uintptr_t>(destination))
            + " n=" + std::to_string(count) + " caller=" + nativeCaller(_ReturnAddress()));
    }
    if (source && destination && count) std::memmove(destination, source, count);
}

/** NSLog(format, ...) writes to the evidence log; the host never opens a console window. */
int __cdecl nativeNSLog(const void* format, ...) {
    const char* text = cfStringBytes(format);
    log(std::string("NSLOG ") + (text ? text : "(unreadable format)"));
    return 0;
}

/** Forward ordinary allocation and arithmetic through ABI-compatible Win32 C calls. */
void registerC() {
    hooks["gettimeofday"] = reinterpret_cast<void*>(&nativeGettimeofday);
    hooks["lrand48"] = reinterpret_cast<void*>(&nativeLrand48); hooks["drand48"] = reinterpret_cast<void*>(&nativeDrand48);
    hooks["srand48"] = reinterpret_cast<void*>(&nativeSrand48);
    hooks["qsort"] = reinterpret_cast<void*>(&std::qsort);
    for (int value = 0; value < 256; ++value) {
        const bool upper = value >= 'A' && value <= 'Z', lower = value >= 'a' && value <= 'z';
        const bool digit = value >= '0' && value <= '9', space = value == ' ' || (value >= 9 && value <= 13);
        const bool control = value < 32 || (value >= 127 && value < 160), blank = value == ' ';
        const bool hex = (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f');
        const bool punctuation = (value >= 33 && value <= 126 && !upper && !lower && !digit) || value >= 160;
        ctypeTable[value + 1] = static_cast<unsigned char>(static_cast<int>(upper) | (lower << 1) | (digit << 2) | (space << 3) | (punctuation << 4) | (control << 5) | (hex << 6) | (blank << 7));
        upperTable[value + 1] = static_cast<short>(lower ? value - 32 : value);
        lowerTable[value + 1] = static_cast<short>(upper ? value + 32 : value);
    }
    upperTable[0] = lowerTable[0] = -1;
    hooks["_ctype_"] = const_cast<unsigned char**>(&ctypePointer);
    hooks["_toupper_tab_"] = const_cast<short**>(&upperPointer); hooks["_tolower_tab_"] = const_cast<short**>(&lowerPointer);
    hooks["pthread_mutex_lock"] = reinterpret_cast<void*>(&mutexLock); hooks["pthread_mutex_unlock"] = reinterpret_cast<void*>(&mutexUnlock);
    hooks["pthread_mutex_init"] = reinterpret_cast<void*>(&mutexInit); hooks["pthread_mutex_destroy"] = reinterpret_cast<void*>(&mutexDestroy);
    // The single-thread probe serializes reader/writer locks with Windows recursive locks.
    hooks["pthread_rwlock_rdlock"] = reinterpret_cast<void*>(&mutexLock); hooks["pthread_rwlock_wrlock"] = reinterpret_cast<void*>(&mutexLock);
    hooks["pthread_rwlock_unlock"] = reinterpret_cast<void*>(&mutexUnlock);
    hooks["pthread_rwlock_init"] = reinterpret_cast<void*>(&mutexInit); hooks["pthread_rwlock_destroy"] = reinterpret_cast<void*>(&mutexDestroy);
    hooks["malloc"] = reinterpret_cast<void*>(&std::malloc); hooks["calloc"] = reinterpret_cast<void*>(&std::calloc);
    hooks["realloc"] = reinterpret_cast<void*>(&std::realloc); hooks["free"] = reinterpret_cast<void*>(&std::free);
    hooks["memcpy"] = reinterpret_cast<void*>(&std::memcpy); hooks["memmove"] = reinterpret_cast<void*>(&std::memmove);
    hooks["memset"] = reinterpret_cast<void*>(&std::memset); hooks["memcmp"] = reinterpret_cast<void*>(&std::memcmp);
    hooks["memchr"] = reinterpret_cast<void*>(&nativeMemchr);
    hooks["strlen"] = reinterpret_cast<void*>(&std::strlen); hooks["strcmp"] = reinterpret_cast<void*>(&std::strcmp);
    hooks["strncmp"] = reinterpret_cast<void*>(&std::strncmp); hooks["strcpy"] = reinterpret_cast<void*>(&std::strcpy);
    hooks["strncpy"] = reinterpret_cast<void*>(&std::strncpy); hooks["strcat"] = reinterpret_cast<void*>(&std::strcat);
    hooks["strchr"] = reinterpret_cast<void*>(static_cast<char*(*)(char*, int)>(&std::strchr));
    hooks["strrchr"] = reinterpret_cast<void*>(static_cast<char*(*)(char*, int)>(&std::strrchr));
    hooks["atoi"] = reinterpret_cast<void*>(&std::atoi); hooks["strtol"] = reinterpret_cast<void*>(&std::strtol);
    hooks["strtoul"] = reinterpret_cast<void*>(&std::strtoul); hooks["strtod"] = reinterpret_cast<void*>(&std::strtod);
    hooks["strtof"] = reinterpret_cast<void*>(&std::strtof); hooks["strdup"] = reinterpret_cast<void*>(&_strdup);
    hooks["__errno"] = reinterpret_cast<void*>(&_errno);
    hooks["printf"] = reinterpret_cast<void*>(&std::printf); hooks["sprintf"] = reinterpret_cast<void*>(&std::sprintf);
    hooks["NSLog"] = reinterpret_cast<void*>(&nativeNSLog);
    hooks["bcopy"] = reinterpret_cast<void*>(&nativeBcopy);
    hooks["sscanf"] = reinterpret_cast<void*>(&std::sscanf); hooks["fprintf"] = reinterpret_cast<void*>(&std::fprintf);
    hooks["fputs"] = reinterpret_cast<void*>(&std::fputs); hooks["puts"] = reinterpret_cast<void*>(&std::puts);
    hooks["strcasecmp"] = reinterpret_cast<void*>(&_stricmp); hooks["strncasecmp"] = reinterpret_cast<void*>(&_strnicmp);
    hooks["snprintf"] = reinterpret_cast<void*>(&std::snprintf); hooks["vsnprintf"] = reinterpret_cast<void*>(&std::vsnprintf);
    hooks["fopen"] = reinterpret_cast<void*>(&std::fopen); hooks["fclose"] = reinterpret_cast<void*>(&std::fclose);
    hooks["fread"] = reinterpret_cast<void*>(&std::fread); hooks["fwrite"] = reinterpret_cast<void*>(&std::fwrite);
    hooks["fseek"] = reinterpret_cast<void*>(&std::fseek); hooks["ftell"] = reinterpret_cast<void*>(&std::ftell);
    hooks["fflush"] = reinterpret_cast<void*>(&std::fflush); hooks["abort"] = reinterpret_cast<void*>(&std::abort);
    hooks["sqrtf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::sqrt));
    hooks["sinf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::sin));
    hooks["cosf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::cos));
    hooks["atan2f"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::atan2));
    hooks["powf"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::pow));
    hooks["fabsf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::fabs));
    hooks["fmodf"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::fmod));
    hooks["floorf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::floor));
    hooks["ceilf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::ceil));
    hooks["acosf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::acos));
    hooks["asinf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::asin));
    hooks["atanf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::atan));
    hooks["tanf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::tan));
    hooks["logf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::log));
    hooks["expf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::exp));
    hooks["sqrt"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::sqrt));
    hooks["sin"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::sin));
    hooks["cos"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::cos));
    hooks["tan"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::tan));
    hooks["pow"] = reinterpret_cast<void*>(static_cast<double(*)(double,double)>(&std::pow));
    hooks["fmod"] = reinterpret_cast<void*>(static_cast<double(*)(double,double)>(&std::fmod));
    // Full CRT math surface used by the original camera/tutorial/render paths.
    hooks["ceil"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::ceil));
    hooks["floor"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::floor));
    hooks["round"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::round));
    hooks["trunc"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::trunc));
    hooks["fabs"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::fabs));
    hooks["exp"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::exp));
    hooks["log"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::log));
    hooks["log10"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::log10));
    hooks["asin"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::asin));
    hooks["acos"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::acos));
    hooks["atan"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::atan));
    hooks["atan2"] = reinterpret_cast<void*>(static_cast<double(*)(double,double)>(&std::atan2));
    hooks["sinh"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::sinh));
    hooks["cosh"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::cosh));
    hooks["tanh"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::tanh));
    hooks["cbrt"] = reinterpret_cast<void*>(static_cast<double(*)(double)>(&std::cbrt));
    hooks["hypot"] = reinterpret_cast<void*>(static_cast<double(*)(double,double)>(&std::hypot));
    hooks["copysign"] = reinterpret_cast<void*>(static_cast<double(*)(double,double)>(&std::copysign));
    hooks["fmin"] = reinterpret_cast<void*>(static_cast<double(*)(double,double)>(&std::fmin));
    hooks["fmax"] = reinterpret_cast<void*>(static_cast<double(*)(double,double)>(&std::fmax));
    hooks["ceilf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::ceil));
    hooks["floorf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::floor));
    hooks["roundf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::round));
    hooks["truncf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::trunc));
    hooks["fabsf"] = reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::fabs));
    hooks["powf"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::pow));
    hooks["fmodf"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::fmod));
    hooks["atan2f"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::atan2));
    hooks["copysignf"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::copysign));
    hooks["fminf"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::fmin));
    hooks["fmaxf"] = reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::fmax));
}

/** Enter the selected original export without using a CPU interpreter or Android. */
void run(const std::filesystem::path& package, const std::string& operation, const std::filesystem::path& outputDirectory) {
    const auto directory = package / "lib/x86"; assetsDirectory = package / "assets";
    convertedTextures = package.parent_path().parent_path().parent_path().parent_path() / "assets/textures";
    // Stable data directory: packaged `<runtime>/../../../../data` when writable, else agent/dsh/state.
    dataDirectory = resolveDataDirectory(package, outputDirectory);
    persistedStorePath = dataDirectory / "platform-store.txt";
    const Scenario scenario = parseScenario(operation);
    registerC(); mapImage(directory / "libverde.so");
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".so" && entry.path().filename() != "libverde.so") mapImage(entry.path());
    // Enumerate the original gamemode resources without starting a scene.
    if (scenario.operation == "levels") {
        std::vector<std::string> modes;
        for (const auto& entry : std::filesystem::directory_iterator(assetsDirectory))
            if (entry.path().extension() == ".gamemode") modes.push_back(entry.path().stem().string());
        std::sort(modes.begin(), modes.end());
        std::ofstream(outputDirectory / "levels.json") << nlohmann::json(modes).dump(2);
        log("ORIGINAL_MODE_FILES " + std::to_string(modes.size()));
        for (const auto& name : modes) log("ORIGINAL_MODE " + name);
        return;
    }
    if (scenario.operation != "map") { createGraphics(); registerGraphics(package.parent_path() / "gl-signatures.json"); }
    const auto nativeHooks = hooks;
    for (const auto& [name, replacement] : nativeHooks)
        {
            if (findExport(name) && name.starts_with("pthread_")) installHook(name, replacement);
            if (findExport("__wrap_" + name)) installHook("__wrap_" + name, replacement);
        }
    for (auto& image : images) relocate(*image);
    log("RELOCATION_COMPLETE unresolved=" + std::to_string(missingNames.size()));
    // Disabled on evidence: the runtime's own class lookup faults from the host (libSystem.so+0x42744 with
    // frames 0x4bf7b/0x4afb4/0x4f23a) and returns 0 for every class, because the host never runs library
    // constructors; the guest's own relocated class/selector slots are already correct, so patching is moot.
    constexpr bool resolveObjcClassSlotsFromRuntime = false;
    if (resolveObjcClassSlotsFromRuntime) resolveObjectiveCClasses();
    logObjectiveCSlots();
    std::ofstream unresolved(outputDirectory / "unresolved-imports.txt");
    for (const auto& name : missingNames) unresolved << name << '\n';
    if (scenario.operation == "map") return;
    redirectGs(package.parent_path() / "gs-instructions.json");
    for (const char* name : {"__wrap_malloc", "__wrap_calloc", "__wrap_realloc", "__wrap_free", "__wrap_memcpy",
                             "__wrap_memset", "__wrap_printf"}) {
        const std::string underlying = std::string(name).substr(7);
        if (const auto hook = hooks.find(underlying); hook != hooks.end()) installHook(name, hook->second);
    }
    installHook("VFS_getFile", reinterpret_cast<void*>(&nativeFile));
    installHook("VFS_getFilesOfType", reinterpret_cast<void*>(&nativeFiles));
    installHook("APPORTABLE_loadPng", reinterpret_cast<void*>(&nativePng));
    installHook("translate", reinterpret_cast<void*>(&nativeTranslate));
    installHook("get_current_realtime", reinterpret_cast<void*>(&nativeRealtime));
    installHook("objc_msgSend", reinterpret_cast<void*>(&nativeMessage));
    installHook("objc_msgSend_stret", reinterpret_cast<void*>(&nativeMessageStructBridge));
    installHook("objc_msgSend_fpret", reinterpret_cast<void*>(&nativeMessageFpret));
    installHook("SOUND_load", reinterpret_cast<void*>(&silentSound));
    for (const char* name : {"SOUND_play", "SOUND_stop", "SOUND_stopLoopingSounds", "SOUND_setListener"})
        installHook(name, reinterpret_cast<void*>(&silentPlayback));
    // The original MENU object must run its own code; only the direct-to-level probe uses the stubs.
    if (scenario.operation != "menu" && scenario.operation != "menuauto") {
        installHook("MENU_backToMainMenu", reinterpret_cast<void*>(&hostMenuButton));
        installHook("MENU_show", reinterpret_cast<void*>(&silentPlayback));
    }
    log("ENTER_ORIGINAL " + scenario.operation);
    if (scenario.operation == "menu" || scenario.operation == "menuauto") { originalMenuScene(outputDirectory, scenario); return; }
    if (scenario.operation == "scene" || scenario.operation == "combat" || scenario.operation == "play" || scenario.operation == "input"
        || scenario.operation == "upgrade" || scenario.operation == "flood" || scenario.operation == "autoplay") {
        originalScene(outputDirectory, scenario); return;
    }
    auto function = reinterpret_cast<void(__cdecl*)()>(findExport(scenario.operation));
    if (!function) throw std::runtime_error("Original export does not exist: " + scenario.operation);
    function(); log("RETURNED_ORIGINAL " + scenario.operation);
}
}

/** Keep each experiment process independent and preserve diagnostics on native failure. */
int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "Usage: host PACKAGE OPERATION LOG\n"); return 2; }
    logFile = std::fopen(argv[3], "w");
    // Every trace, PNG, and diagnostic stays beside the log the caller selected.
    std::filesystem::path outputDirectory = std::filesystem::path(argv[3]).parent_path();
    if (outputDirectory.empty()) outputDirectory = ".";
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(nativeFault);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try { run(argv[1], argv[2], outputDirectory); return 0; }
    catch (const std::exception& error) { log("HOST_ERROR " + std::string(error.what())); return 3; }
}
