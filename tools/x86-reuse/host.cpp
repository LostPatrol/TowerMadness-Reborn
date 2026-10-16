// Execute the immutable game's x86 ELF code in an isolated native Windows host.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <wincodec.h>
#include <shellapi.h>
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
#include <cwchar>
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
#include "original-audio.hpp"

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
// The host creates a legacy OpenGL context; native audio is optional for scripted runs.
HWND windowHandle = nullptr;
HDC windowDc = nullptr;
HGLRC windowGl = nullptr;
// Windows pointer and wheel events reuse the original touch and camera handlers.
void* activeGame = nullptr;
struct HostTouch { float x = 0, y = 0, startX = 0, startY = 0, previousX = 0, previousY = 0; int tapCount = 1; };
// Small pointer jitter remains a tap; carousel drags end without activating the selected map.
constexpr float TouchTapSlop = 5.0f;
HostTouch hostTouch;
// Wheel gestures expose two stable UITouch identities to the original pinch logic.
HostTouch hostPinchTouch;
unsigned long hostTouchCount = 1, hostTouchMutation = 0;
std::array<HostTouch*, 2> hostTouchObjects{&hostTouch, &hostPinchTouch};
int hostTouchSet = 0;
// Original EAGLView fields used by its unchanged outer button dispatcher.
std::array<unsigned char, 128> hostView{};
std::array<unsigned char, 512> hostMenu{};
// Direct-play probes use a host pause; menu mode routes to the original MENU object.
int hostPauseRequested = 0;
// The real original MENU object (0x3b4 bytes), its current GAME, and dummy platform receivers.
void* originalMenu = nullptr;
int hostApplicationToken = 0, hostGameAccountToken = 0;
// UIDevice exists only as a capability receiver for the original Options page.
int hostDeviceToken = 0;
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

/** Redirect verified exports in the private loaded image without changing original files. */
void installHook(const std::string& name, void* replacement) {
    hooks[name] = replacement;
    for (const auto& image : images) {
        const auto symbol = image->exports.find(name);
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
        FlushInstructionCache(GetCurrentProcess(), entry, 5);
        log("PLATFORM_HOOK " + image->name + ":" + name);
    }
}

#include "tower-restore.hpp"
#include "point-sprite-correction.hpp"

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

/** Route Windows input through the unchanged original touch handlers. */
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (activeGame && message == WM_MOUSEWHEEL) {
        const bool menuShowing = originalMenu && *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu) + 4) != 0;
        void* game = originalMenu ? *reinterpret_cast<void**>(static_cast<unsigned char*>(originalMenu) + 0x15c) : activeGame;
        if (!game || menuShowing || GetCapture() == window) return 0;
        // A symmetric nonzero span lets the original code own zoom rate and camera bounds.
        constexpr float PinchHalfSpan = 80.0f, WheelSpanStep = 8.0f;
        POINT center{static_cast<short>(LOWORD(second)), static_cast<short>(HIWORD(second))};
        ScreenToClient(window, &center);
        const float steps = static_cast<short>(HIWORD(first)) / 120.0f;
        const float delta = std::clamp(steps * WheelSpanStep, -PinchHalfSpan / 2, PinchHalfSpan / 2);
        hostTouchCount = 2;
        hostTouch = HostTouch{}; hostPinchTouch = HostTouch{};
        hostTouch.x = hostTouch.previousX = center.x - PinchHalfSpan;
        hostPinchTouch.x = hostPinchTouch.previousX = center.x + PinchHalfSpan;
        hostTouch.y = hostTouch.previousY = hostPinchTouch.y = hostPinchTouch.previousY = static_cast<float>(center.y);
        hostTouch.tapCount = hostPinchTouch.tapCount = 0;
        reinterpret_cast<void(__cdecl*)(void*, void*)>(findExport("GAME_touchesBegan"))(game, &hostTouchSet);
        hostTouch.x -= delta; hostPinchTouch.x += delta;
        reinterpret_cast<void(__cdecl*)(void*, void*)>(findExport("GAME_touchesMoved"))(game, &hostTouchSet);
        reinterpret_cast<void(__cdecl*)(void*, void*)>(findExport("GAME_touchesEnded"))(game, &hostTouchSet);
        hostTouchCount = 1;
        log("WINDOWS_WHEEL pinchDelta=" + std::to_string(delta));
        return 0;
    }
    if (activeGame && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || (message == WM_MOUSEMOVE && (first & MK_LBUTTON)))) {
        hostTouch.previousX = hostTouch.x; hostTouch.previousY = hostTouch.y;
        hostTouch.x = static_cast<float>(static_cast<short>(LOWORD(second)));
        hostTouch.y = static_cast<float>(static_cast<short>(HIWORD(second)));
        if (message == WM_LBUTTONDOWN) {
            hostTouch.startX = hostTouch.x; hostTouch.startY = hostTouch.y; hostTouch.tapCount = 1;
            hostTouch.previousX = hostTouch.x; hostTouch.previousY = hostTouch.y;
        } else {
            const float dx = hostTouch.x - hostTouch.startX, dy = hostTouch.y - hostTouch.startY;
            if (dx * dx + dy * dy > TouchTapSlop * TouchTapSlop) hostTouch.tapCount = 0;
        }
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
    windowHandle = CreateWindowW(definition.lpszClassName, L"TowerMadness", WindowStyle,
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

/** Suppress direct-scene menu rendering where no original MENU object exists. */
void __cdecl silentPlayback() {}

/** Render a shipped original HTML page with the user's Windows browser. */
bool openOriginalPage(const char* filename) {
    const auto path = assetsDirectory / filename;
    if (!std::filesystem::exists(path)) { log("PLATFORM_HELP_MISSING " + path.string()); return false; }
    const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(windowHandle, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    log("PLATFORM_HELP page=" + std::string(filename) + " launched=" + std::to_string(result > 32));
    return result > 32;
}

/** Replace the original UIKit-only Credits presentation with its unchanged HTML content. */
void __cdecl nativeCredits(void*) { openOriginalPage("credits.html"); }

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
// Temporary serialized input; the original IBS readers copy into owned runtime objects.
std::vector<std::uint8_t> loadedIbsBytes;
// NSUserDefaults surface and the on-disk platform store (real content keys, persisted across processes).
int hostUserDefaultsToken = 0;
std::filesystem::path persistedStorePath;
std::filesystem::path dataDirectory;
// Window callbacks queue restoration before the next frame caches its GAME pointer.
bool pendingRestore = false;
std::filesystem::path pendingRestorePath;
std::string pendingRestoreTag;
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

/** Compare UTF-8 Foundation strings, including the numeric option used for version checks. */
int compareHostText(const char* left, const char* right, unsigned options) {
    const auto wide = [](const char* text) {
        const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        std::wstring value(static_cast<std::size_t>((std::max)(length, 1)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, value.data(), length);
        return value;
    };
    const auto first = wide(left), second = wide(right);
    // NSCaseInsensitiveSearch=1; NSNumericSearch=64. Windows returns 1/2/3 for ascending/equal/descending.
    const DWORD flags = (options & 1 ? NORM_IGNORECASE : 0) | (options & 64 ? SORT_DIGITSASNUMBERS : 0);
    const int order = CompareStringEx(LOCALE_NAME_INVARIANT, flags, first.c_str(), -1, second.c_str(), -1, nullptr, nullptr, 0);
    if (!order) throw std::runtime_error("Windows could not compare the platform strings");
    return order - CSTR_EQUAL;
}

/**
 * Decode an original CFString/NSString to its C-string bytes. Constants use either {isa, cstr} or
 * {isa, flags, cstr, length}; heap NSStrings keep the same pointer slots. Both are probed safely.
 */
const char* cfStringBytes(const void* value) {
    // CFString isa bytes can accidentally look like a short C string after ASLR.
    // Prefer its real cstr slot whenever both the object and its isa belong to guest images.
    if (readableBytes(value, 12)) {
        const auto address = reinterpret_cast<std::uintptr_t>(value);
        const auto isa = *reinterpret_cast<const std::uint32_t*>(value);
        bool mappedObject = false, mappedClass = false;
        for (const auto& image : images) {
            const auto base = reinterpret_cast<std::uintptr_t>(image->base);
            mappedObject |= address >= base && address < base + image->file.size();
            mappedClass |= isa >= base && isa < base + image->file.size();
        }
        if (mappedObject && mappedClass) {
            const auto bytes = reinterpret_cast<const char* const*>(value)[2];
            std::string text;
            if (tryReadText(bytes, text)) return bytes;
        }
    }
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
    if (name == "currentDevice" && objcClassName(receiver) == "UIDevice")
        return reinterpret_cast<std::uint32_t>(&hostDeviceToken);
    if (receiver == &hostDeviceToken && name == "systemVersion") {
        // This is a UIKit compatibility level, not a claim that Windows is running iOS.
        // Level 5 enables the original MusicManager volume control supported by this adapter.
        static void* version = copyHostText("5.0");
        return reinterpret_cast<std::uint32_t>(version);
    }
    if ((name == "setBaseVolume:" || name == "setTemporaryVolumeOverride:" || name == "fadeToVolume:")
        && objcClassName(receiver) == "MusicManager") {
        va_list arguments; va_start(arguments, selector);
        const unsigned raw = va_arg(arguments, unsigned); va_end(arguments);
        float volume = 0.0f;
        std::memcpy(&volume, &raw, sizeof(volume));
        if (name == "setBaseVolume:") original_audio::baseVolume = volume;
        if (name == "setTemporaryVolumeOverride:") original_audio::temporaryVolume = volume;
        if (name == "fadeToVolume:") original_audio::fadeTarget = volume;
        original_audio::refreshMusic();
        log("PLATFORM_MUSIC volume=" + std::to_string(volume) + " selector=" + name);
        return 0;
    }
    // The original i18n reader asks NSLocale for an NSArray of NSString language tags.
    if (name == "preferredLanguages" && objcClassName(receiver) == "NSLocale") {
        static HostContainer* languages = nullptr;
        if (!languages) {
            languages = newHostContainer();
            languages->kind = 'A';
            ULONG count = 0, length = 0;
            GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &length);
            std::vector<wchar_t> buffer(length);
            if (length && GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &length)) {
                for (const wchar_t* language = buffer.data(); *language; language += std::wcslen(language) + 1) {
                    const int size = WideCharToMultiByte(CP_UTF8, 0, language, -1, nullptr, 0, nullptr, nullptr);
                    if (size <= 1) continue;
                    std::string tag(static_cast<std::size_t>(size), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, language, -1, tag.data(), size, nullptr, nullptr);
                    tag.pop_back();
                    languages->items.push_back(copyHostText(tag));
                    log("PLATFORM_LANGUAGE " + tag);
                }
            }
            if (languages->items.empty()) {
                languages->items.push_back(copyHostText("en"));
                log("PLATFORM_LANGUAGE en (Windows preference unavailable)");
            }
        }
        return reinterpret_cast<std::uint32_t>(languages);
    }
    if ((name == "resetTimeForTune:" || name == "playMusic:loop:" || name == "rememberTimeForPlayingTune"
        || name == "updateFading") && objcClassName(receiver) == "MusicManager") {
        if (name == "resetTimeForTune:" || name == "playMusic:loop:") {
            va_list arguments; va_start(arguments, selector);
            const void* argument = va_arg(arguments, const void*);
            const bool loop = name == "playMusic:loop:" ? va_arg(arguments, int) != 0 : false;
            va_end(arguments);
            const std::string tune = hostKeyText(argument);
            if (name == "resetTimeForTune:") original_audio::positions[tune] = 0;
            else original_audio::selectMusic(tune, loop);
            return 0;
        }
        if (name == "rememberTimeForPlayingTune") { original_audio::rememberMusic(); return 0; }
        if (name == "updateFading") { original_audio::update(); return 0; }
    }
    // The host pointer objects retain the original NSSet/UITouch contract.
    if (receiver == &hostTouchSet) {
        if (name == "count") return hostTouchCount;
        if (name == "anyObject") return reinterpret_cast<std::uint32_t>(&hostTouch);
        if (name == "countByEnumeratingWithState:objects:count:") {
            va_list arguments; va_start(arguments, selector);
            auto* state = va_arg(arguments, unsigned long*);
            va_arg(arguments, void**); const auto capacity = va_arg(arguments, unsigned long);
            va_end(arguments);
            if (state[0]) return 0;
            state[0] = 1;
            state[1] = reinterpret_cast<unsigned long>(hostTouchObjects.data());
            state[2] = reinterpret_cast<unsigned long>(&hostTouchMutation);
            return std::min(hostTouchCount, capacity);
        }
    }
    if (receiver == &hostTouch || receiver == &hostPinchTouch) {
        if (name == "view") return reinterpret_cast<std::uint32_t>(hostView.data());
        if (name == "tapCount") return static_cast<std::uint32_t>(static_cast<HostTouch*>(receiver)->tapCount);
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
        if (name == "compare:options:") {
            va_list arguments; va_start(arguments, selector);
            const void* other = va_arg(arguments, const void*);
            const unsigned options = va_arg(arguments, unsigned); va_end(arguments);
            const char* bytes = cfStringBytes(other);
            return static_cast<std::uint32_t>(compareHostText(candidate.text, bytes ? bytes : "", options));
        }
        log("PLATFORM_STRING_UNHANDLED " + name);
        ExitProcess(31);
    }
    // NSUserDefaults content keys persist in the selected development or portable data directory.
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
        // These eight StoreManager selectors gate purchased maps/weapons, not campaign achievements.
        static const std::set<std::string> ShippedPacks = {
            "unlockedMarshMadness", "unlockedMoon", "unlockedLava", "unlockedSpooky", "unlockedCoast",
            "unlockedFlamethrower", "unlockedFlashbait", "unlockedProximityMine"
        };
        if (ShippedPacks.count(name) && objcClassName(receiver) == "StoreManager") {
            log("PLATFORM_CONTENT " + name + " -> 1 (shipped content; purchase gate excluded)");
            return 1;
        }
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
    if (name == "URLWithString:") {
        va_list arguments; va_start(arguments, selector);
        const void* value = va_arg(arguments, const void*); va_end(arguments);
        return reinterpret_cast<std::uint32_t>(copyHostText(hostKeyText(value)));
    }
    if (name == "openURL:") {
        va_list arguments; va_start(arguments, selector);
        const void* value = va_arg(arguments, const void*); va_end(arguments);
        const auto url = hostKeyText(value);
        // The original remote Guide has an equivalent complete local page in the APK.
        if (url == "http://www.limbic.com/tm/guide.html") return openOriginalPage("guide.html") ? 1 : 0;
        log("PLATFORM_EXTERNAL_URL_UNAVAILABLE " + url);
        return 0;
    }
    log("UNIMPLEMENTED_OBJC_SELECTOR " + name + " receiver=" + std::to_string(reinterpret_cast<std::uintptr_t>(receiver))
        + " class=" + objcClassName(receiver) + " caller=" + nativeCaller(_ReturnAddress()));
    ExitProcess(31);
}

/** Convert Windows cursor coordinates through the original CGPoint return ABI. */
void __cdecl nativeMessageStruct(float* point, void* receiver, const char* selector, void*) {
    const std::string name = selectorName(selector);
    if (receiver == &hostTouch || receiver == &hostPinchTouch) {
        const auto& touch = *static_cast<HostTouch*>(receiver);
        if (name == "locationInView:") { point[0] = touch.x; point[1] = touch.y; return; }
        if (name == "previousLocationInView:") { point[0] = touch.previousX; point[1] = touch.previousY; return; }
    }
    if (receiver == hostView.data() && name == "bounds") { point[0] = point[1] = 0; point[2] = 480; point[3] = 800; return; }
    log("UNIMPLEMENTED_OBJC_STRUCT_SELECTOR " + name); ExitProcess(32);
}

/** Implement floating-point platform selectors with Foundation value semantics. */
double __cdecl nativeMessageFpret(void* receiver, const char* selector, ...) {
    if (!receiver) return 0.0;
    const std::string name = selectorName(selector);
    if (name == "timeIntervalSinceReferenceDate") {
        for (const auto& container : hostContainers)
            if (receiver == &container && container.kind == 'T') return std::atof(container.text.c_str());
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
        // NSDate stores reference-date seconds; Unix conversion remains correct for every caller.
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

/** Fix SCORES_load's original epoch mismatch at its selector load, leaving NSDate semantics intact. */
void correctScoreDateReader() {
    // Zero 1.22 loads the 1970 selector at SCORES_load+0x1ae, but SCORES_save stores 2001 seconds.
    constexpr std::size_t SelectorLoadOffset = 0x1ae;
    const unsigned char original[] = {0x8b, 0x83, 0x3c, 0x6b, 0x01, 0x00, 0x89, 0x85, 0x5c, 0xff, 0xff, 0xff};
    auto* instruction = static_cast<unsigned char*>(requireExport("SCORES_load")) + SelectorLoadOffset;
    if (std::memcmp(instruction, original, sizeof(original)) != 0)
        throw std::runtime_error("Unsupported original SCORES_load date selector");
    // EBX anchors ELF 0x3c5608; 0x16cbc addresses the existing reference-date selector at 0x3dc2c4.
    constexpr std::uint32_t ReferenceDateDisplacement = 0x16cbc;
    std::memcpy(instruction + 2, &ReferenceDateDisplacement, sizeof(ReferenceDateDisplacement));
    FlushInstructionCache(GetCurrentProcess(), instruction, 6);
    log("ORIGINAL_SCORE_DATE_READER epoch=2001 original-files=unchanged");
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
    return {{"map", reinterpret_cast<const char*>(*reinterpret_cast<void**>(*reinterpret_cast<void**>(game)))},
        {"money", getter("PLAYER_getMoney")}, {"score", getter("PLAYER_getScore")}, {"lives", getter("PLAYER_getLife")},
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
    if (std::filesystem::exists(package.parent_path() / "portable-runtime.json")) {
        const auto portable = package.parent_path().parent_path() / "data";
        std::filesystem::create_directories(portable, error);
        if (!error) return portable;
        log("STATE_WARNING Portable data directory is unavailable; using the selected log directory.");
        error.clear();
    }
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
 * set the resolution from the menu's logical size, go back to the
 * in-progress main menu (MENU_backToMainMenu), MENU_hide, then clear the fade state and set
 * showing=0 directly so the game runs on the next frame instead of through the menu fade.
 */
void installRestoredGame(void* userMenu, void* loaded) {
    if (!userMenu || !loaded) return;
    auto* bytes = static_cast<unsigned char*>(userMenu);
    auto* gameSlot = reinterpret_cast<void**>(bytes + 0x15c);
    // The restore entry has already detached and freed the old GAME before building this one.
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
    reinterpret_cast<void(__cdecl*)(void*)>(requireExport("OBS_free"))(obs);
}

/** Queue F9 without replacing objects while the current frame still holds their pointers. */
void hotkeyLoadState(void* userMenu) {
    if (!userMenu) return;
    pendingRestorePath = dataDirectory.empty() ? saveStatePath({}) : dataDirectory / "menu-save-hotkey.bin";
    pendingRestoreTag = "ORIGINAL_LOAD_HOTKEY";
    pendingRestore = true;
    log("ORIGINAL_LOAD_HOTKEY_REQUESTED file=" + pendingRestorePath.string());
}

/** Replace a GAME at a frame boundary, retaining the original free-before-create ownership order. */
void restoreSavedGame(void* userMenu, const std::filesystem::path& path, const std::string& tag) {
    if (!readSaveState(path, loadedIbsBytes)) { log("ORIGINAL_LOAD_HOTKEY_NOFILE " + path.string()); return; }
    // Preserve individual lifecycle events even when the allocator reuses an address and log() deduplicates.
    static unsigned restoreSequence = 0;
    const unsigned sequence = ++restoreSequence;
    void*& slot = *reinterpret_cast<void**>(static_cast<unsigned char*>(userMenu) + 0x15c);
    if (slot) {
        void* old = slot;
        slot = nullptr;
        reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_free"))(old);
        log("ORIGINAL_GAME_FREED_BEFORE_RESTORE old=" + std::to_string(reinterpret_cast<std::uintptr_t>(old))
            + " restore=" + std::to_string(sequence));
    }
    void* ibs = reinterpret_cast<void*(__cdecl*)(void*, unsigned, unsigned)>(findExport("IBS_wrap"))(
        loadedIbsBytes.data(), static_cast<unsigned>(loadedIbsBytes.size()), 0);
    void* loaded = reinterpret_cast<void*(__cdecl*)(void*)>(findExport("GAME_fromIBS"))(ibs);
    reinterpret_cast<void(__cdecl*)(void*)>(requireExport("IBS_free"))(ibs);
    log(tag + " bytes=" + std::to_string(loadedIbsBytes.size()) + " restore=" + std::to_string(sequence)
        + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(loaded))
        + (loaded ? " " + playerState(loaded).dump() : std::string(" (failed)")));
    if (!loaded) throw std::runtime_error("Original GAME_fromIBS rejected the saved game");
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

/** Exercise original build/upgrade/sell without changing resources or player state. */
void originalWeaponsProbe(void* game, void* mode, const std::filesystem::path& output) {
    // Each pair represents a distinct original weapon family and its first legal upgrade.
    static constexpr const char* Cases[][2] = {
        {"Plasmatron I", "Plasmatron II"}, {"Kilowatt Laser", "Megawatt Laser"},
        {"Mortar I", "Mortar II"}, {"Flak I", "Flak II"},
        {"Guided Missile I", "Guided Missile II"}, {"ElectroSlow I", "ElectroSlow II"},
        {"ElectroBoost I", "ElectroBoost II"}, {"ElectroShock I", "ElectroShock II"},
        {"Railgun I", "Railgun II"}, {"Proximity Mine I", "Proximity Mine II"},
        {"Flamethrower I", "Flamethrower II"}, {"Nuke", "H Bomb"}
    };
    auto update = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"));
    auto render = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"));
    auto build = reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(requireExport("GAME_buildTower"));
    auto upgrade = reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_upgradeTower"));
    auto sell = reinterpret_cast<void(__cdecl*)(void*, int)>(requireExport("GAME_sellTower"));
    auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
    auto towerForId = reinterpret_cast<int*(__cdecl*)(void*, int)>(requireExport("TOWERS_getTowerForId"));
    auto canBuild = reinterpret_cast<int(__cdecl*)(void*, int, int, int, int)>(requireExport("TOWERS_canBuildHere"));
    auto* towers = reinterpret_cast<void**>(game)[4];
    const auto* technology = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint32_t*>(mode)[7]);
    const auto* types = reinterpret_cast<const std::uint32_t* const*>(technology[15]);
    nlohmann::json result{{"initial", playerState(game)}, {"cases", nlohmann::json::array()},
        {"scope", "Original API construction, upgrade, sale, and rendering; no waves or combat effects"}};
    // Keep native clock progression, message handling, and drawing identical to scene mode.
    auto tick = [&]() {
        update(game); render(game); SwapBuffers(windowDc);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(16);
    };
    for (const auto& item : Cases) {
        // Nuke is reached through the original mortar prerequisites, never built directly.
        const int base = nativeTowerId(mode, !std::strcmp(item[0], "Nuke") ? "Mortar I" : item[0]);
        const int target = nativeTowerId(mode, item[1]);
        int x = -1, y = -1;
        // Peach's shipped 20x19 terrain is expanded by the original loader to 40x38 cells.
        for (int row = 10; row < 30 && x < 0; ++row) for (int col = 10; col < 30; ++col) {
            if (idForCell(towers, col, row) == -1 && canBuild(towers, col, row, 0, types[base][34]) == 0) {
                x = col; y = row; break;
            }
        }
        if (x < 0) throw std::runtime_error("No original legal cell for weapon probe");
        nlohmann::json entry{{"name", item[0]}, {"target", item[1]}, {"baseTypeId", base},
            {"targetTypeId", target}, {"cell", {x, y}}, {"before", playerState(game)}};
        log("WEAPON_REQUEST " + entry.dump());
        build(game, base, x, y);
        // Original pre-wave construction uses its own 500ms completion path.
        ULONGLONG until = GetTickCount64() + 70000;
        int id = -1;
        while (GetTickCount64() < until) {
            tick(); id = idForCell(towers, x, y);
            int* tower = id == -1 ? nullptr : towerForId(towers, id);
            if (tower && tower[1] == base && !tower[16]) break;
        }
        int* tower = id == -1 ? nullptr : towerForId(towers, id);
        if (!tower || tower[1] != base || tower[16]) throw std::runtime_error("Original weapon build failed or timed out");
        entry["instanceId"] = id; entry["built"] = playerState(game);
        capture(output / (std::string("weapon-") + std::to_string(base) + "-built.png"), 480, 800);
        entry["upgradeChain"] = nlohmann::json::array();
        while (tower[1] != target) {
            const auto* current = types[tower[1]];
            if (current[25] != 1) throw std::runtime_error("Probe requires a unique original catalog upgrade edge");
            const int next = *reinterpret_cast<const int*>(current[27]);
            upgrade(game, id, next);
            until = GetTickCount64() + 70000;
            while (GetTickCount64() < until) {
                tick(); tower = towerForId(towers, id);
                if (tower && tower[1] == next && !tower[16]) break;
            }
            if (!tower || tower[1] != next || tower[16]) throw std::runtime_error("Original weapon upgrade failed or timed out");
            nlohmann::json stage{{"typeId", next}, {"name", reinterpret_cast<const char*>(types[next][1])},
                {"cost", types[next][15]}, {"state", playerState(game)}};
            entry["upgradeChain"].push_back(stage);
            log("WEAPON_CHAIN_STAGE " + stage.dump());
        }
        if (!tower || tower[1] != target || tower[16]) throw std::runtime_error("Original weapon upgrade failed or timed out");
        entry["upgraded"] = playerState(game);
        capture(output / (std::string("weapon-") + std::to_string(target) + "-upgraded.png"), 480, 800);
        sell(game, id);
        until = GetTickCount64() + 10000;
        while (GetTickCount64() < until && idForCell(towers, x, y) != -1) tick();
        if (idForCell(towers, x, y) != -1) throw std::runtime_error("Original weapon sale failed or timed out");
        entry["sold"] = playerState(game);
        result["cases"].push_back(entry);
        std::ofstream(output / "weapons-result.json") << result.dump(2);
        log("WEAPON_VERIFIED " + entry.dump());
    }
    result["final"] = playerState(game);
    std::ofstream(output / "weapons-result.json") << result.dump(2);
    log("WEAPONS_DONE count=" + std::to_string(result["cases"].size()));
}
/** Replay only Windows mouse clicks; native readers record the resulting original GUI state. */
void originalGuiWeaponsProbe(void* game, const std::filesystem::path& output) {
    const char* scriptPath = std::getenv("TOWERMADNESS_GUI_SCRIPT");
    if (!scriptPath) throw std::runtime_error("GUI probe needs TOWERMADNESS_GUI_SCRIPT");
    std::ifstream input(scriptPath);
    nlohmann::json script; input >> script;
    std::ofstream(output / "gui-script.json") << script.dump(2);
    auto update = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"));
    auto render = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"));
    auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
    auto towerForId = reinterpret_cast<int*(__cdecl*)(void*, int)>(requireExport("TOWERS_getTowerForId"));
    nlohmann::json result{{"initial", playerState(game)}, {"steps", nlohmann::json::array()}};
    for (const auto& step : script) {
        if (step.contains("click")) {
            const int x = step["click"][0], y = step["click"][1];
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        }
        const ULONGLONG until = GetTickCount64() + step.value("waitMs", 900);
        while (GetTickCount64() < until) {
            update(game); render(game); SwapBuffers(windowDc);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            Sleep(16);
        }
        nlohmann::json record = step;
        record["state"] = playerState(game);
        const auto* raw = static_cast<int*>(game);
        const auto* pie = reinterpret_cast<const int*>(raw[0xa48 / 4]);
        record["pieKind"] = pie[5];
        record["selectedCell"] = {raw[0xaa4 / 4], raw[0xaa8 / 4]};
        record["towers"] = nlohmann::json::array();
        auto* towers = reinterpret_cast<void**>(game)[4];
        // Read every possible Peach cell without altering selection or construction state.
        for (int y = 0; y < 38; ++y) for (int x = 0; x < 40; ++x) {
            const int id = idForCell(towers, x, y);
            if (id == -1) continue;
            const int* tower = towerForId(towers, id);
            if (tower && tower[4] == x && tower[5] == y)
                record["towers"].push_back({{"instanceId", id}, {"typeId", tower[1]}, {"cell", {x, y}}, {"building", tower[16]}});
        }
        const std::string name = step.at("name");
        capture(output / ("gui-" + name + ".png"), 480, 800);
        result["steps"].push_back(record);
        std::ofstream(output / "gui-result.json") << result.dump(2);
        log("GUI_WEAPONS_STEP " + record.dump());
        if (step.contains("expectMoney") && record["state"]["money"] != step["expectMoney"])
            throw std::runtime_error("GUI probe money mismatch at " + name);
        if (step.contains("expectType")) {
            const int wanted = step["expectType"];
            if (wanted == -1 ? !record["towers"].empty()
                : record["towers"].size() != 1 || record["towers"][0]["typeId"] != wanted || record["towers"][0]["building"] != 0)
                throw std::runtime_error("GUI probe original tower mismatch at " + name);
        }
        if (step.contains("expectPie") && record["pieKind"] != step["expectPie"])
            throw std::runtime_error("GUI probe menu category mismatch at " + name);
    }
    log("GUI_WEAPONS_DONE steps=" + std::to_string(script.size()));
}
#include "weapon-combat.hpp"

/** Resolve an optional upgrade target without aborting a scenario when a tech omits it. */
int nativeTowerIdOrNone(void* mode, const char* name) {
    try { return nativeTowerId(mode, name); } catch (const std::exception&) { return -1; }
}

/** Drive a legal Madness strategy with original Plasma, Slow and Boost technology. */
void campaignMadnessDefense(void* game) {
    // Keep the original nine legal firing positions; the first five form the opening.
    static constexpr int Cells[][2] = {{8, 24}, {12, 24}, {8, 20}, {12, 20}, {10, 18}, {16, 24}, {16, 20}, {6, 16}, {14, 16}};
    auto* towers = reinterpret_cast<void**>(game)[4];
    auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
    auto towerForId = reinterpret_cast<int*(__cdecl*)(void*, int)>(requireExport("TOWERS_getTowerForId"));
    void* mode = *reinterpret_cast<void**>(game);
    const int plasma = nativeTowerId(mode, "Plasmatron I");
    const auto* technology = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint32_t*>(mode)[7]);
    const auto* types = reinterpret_cast<const std::uint32_t* const*>(technology[15]);
    const int money = reinterpret_cast<int(__cdecl*)(void*)>(requireExport("PLAYER_getMoney"))(reinterpret_cast<void**>(game)[1]);
    // Derive readiness from this GAME so restores and restarts cannot inherit stale strategy state.
    bool supportReady = false;
    if (!supportReady) {
        supportReady = true;
        const int gauss = nativeTowerId(mode, "Gauss Cannon");
        for (int index = 0; index < 5; ++index) {
            const auto& cell = Cells[index];
            const int id = idForCell(towers, cell[0], cell[1]);
            const int* tower = id == -1 ? nullptr : towerForId(towers, id);
            if (!tower || tower[1] != gauss || tower[16]) supportReady = false;
        }
    }
    /** Build or advance one original support tower when affordable, stopping at the named level. */
    const auto support = [&](int x, int y, const char* baseName, const char* capName) {
        const int id = idForCell(towers, x, y);
        const int base = nativeTowerId(mode, baseName);
        if (id == -1) {
            if (money < static_cast<int>(types[base][15])) return true;
            reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(requireExport("GAME_buildTower"))(game, base, x, y);
            log("ORIGINAL_SUPPORT_BUILD " + std::string(baseName) + " cell=" + std::to_string(x) + "," + std::to_string(y));
            return true;
        }
        const int* tower = towerForId(towers, id);
        if (!tower || tower[16] || tower[17] || tower[25] != -1 || tower[1] == nativeTowerId(mode, capName)) return false;
        int chain = base;
        bool belowCap = false;
        while (chain != nativeTowerId(mode, capName)) {
            if (chain == tower[1]) belowCap = true;
            if (!types[chain][25]) break;
            chain = *reinterpret_cast<const int*>(types[chain][27]);
        }
        if (!belowCap) return false;
        const auto* type = types[tower[1]];
        if (!type[25]) return false;
        const int target = *reinterpret_cast<const int*>(type[27]);
        if (money < static_cast<int>(types[target][15])) return true;
        reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_upgradeTower"))(game, id, target);
        log("ORIGINAL_SUPPORT_UPGRADE instance=" + std::to_string(id) + " target=" + std::to_string(target));
        return true;
    };
    if (supportReady && support(10, 24, "ElectroSlow I", "Muscle Melter")) return;
    if (supportReady && support(10, 22, "ElectroBoost I", "ElectroBoost VI")) return;
    // Queue missing towers together before the first wave; the original processes affordability in order.
    bool missing = false;
    const int cellCount = supportReady ? 9 : 5;
    for (int index = 0; index < cellCount; ++index) {
        const auto& cell = Cells[index];
        if (idForCell(towers, cell[0], cell[1]) != -1) continue;
        missing = true;
        reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(requireExport("GAME_buildTower"))(
            game, plasma, cell[0], cell[1]);
        log("ORIGINAL_CAMPAIGN_BUILD cell=" + std::to_string(cell[0]) + "," + std::to_string(cell[1]));
    }
    if (missing && money >= static_cast<int>(types[plasma][15])) return;
    for (int index = 0; index < cellCount; ++index) {
        const auto& cell = Cells[index];
        const int id = idForCell(towers, cell[0], cell[1]);
        const int* tower = id == -1 ? nullptr : towerForId(towers, id);
        // Original tower fields: +0x40 building, +0x44 selling, +0x64 missile in flight.
        if (!tower || tower[16] || tower[17] || tower[25] != -1) continue;
        if (tower[1] == nativeTowerId(mode, "Gauss Cannon")) continue;
        const auto* type = types[tower[1]];
        if (type[25] == 0) continue;
        const int target = *reinterpret_cast<const int*>(type[27]);
        if (money < static_cast<int>(types[target][15])) continue;
        reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_upgradeTower"))(game, id, target);
        log("ORIGINAL_CAMPAIGN_UPGRADE instance=" + std::to_string(id) + " target=" + std::to_string(target));
        return;
    }
}

/** Submit legal campaign build/upgrade requests; costs and combat stay in the original code. */
void campaignDefense(void* game) {
    if (playerState(game).at("map") == "Madness") { campaignMadnessDefense(game); return; }
    // PlainPlane's navigation grid is 22x32; the JSON terrain tiles cover 2x2 navigation cells.
    static constexpr int Cells[][2] = {{8, 12}, {12, 12}, {8, 16}, {12, 16}, {10, 20}};
    // DividedAttention's shared approach is farther down the original navigation grid.
    static constexpr int MediumCells[][2] = {{8, 24}, {12, 24}, {8, 20}, {12, 20}, {10, 16}};
    // Madness has water at (10,16); use the legal cell immediately below it.
    static constexpr int InsaneCells[][2] = {{8, 24}, {12, 24}, {8, 20}, {12, 20}, {10, 18}};
    const auto map = playerState(game).at("map").get<std::string>();
    const auto& cells = map == "PlainPlane" ? Cells : map == "Madness" ? InsaneCells : MediumCells;
    auto* towers = reinterpret_cast<void**>(game)[4];
    auto idForCell = reinterpret_cast<int(__cdecl*)(void*, int, int)>(requireExport("TOWERS_getIdForCell"));
    auto towerForId = reinterpret_cast<int*(__cdecl*)(void*, int)>(requireExport("TOWERS_getTowerForId"));
    void* mode = *reinterpret_cast<void**>(game);
    const int laser = nativeTowerId(mode, "Kilowatt Laser");
    const auto* technology = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint32_t*>(mode)[7]);
    const auto* types = reinterpret_cast<const std::uint32_t* const*>(technology[15]);
    const int money = reinterpret_cast<int(__cdecl*)(void*)>(requireExport("PLAYER_getMoney"))(reinterpret_cast<void**>(game)[1]);
    // Queue missing towers together before the first wave; the original processes affordability in order.
    bool missing = false;
    for (const auto& cell : cells) {
        if (idForCell(towers, cell[0], cell[1]) != -1) continue;
        missing = true;
        reinterpret_cast<void(__cdecl*)(void*, int, int, int)>(requireExport("GAME_buildTower"))(
            game, laser, cell[0], cell[1]);
        log("ORIGINAL_CAMPAIGN_BUILD cell=" + std::to_string(cell[0]) + "," + std::to_string(cell[1]));
    }
    if (missing && money >= static_cast<int>(types[laser][15])) return;
    for (const auto& cell : cells) {
        const int id = idForCell(towers, cell[0], cell[1]);
        const int* tower = id == -1 ? nullptr : towerForId(towers, id);
        // Original tower fields: +0x40 building, +0x44 selling, +0x64 missile in flight.
        if (!tower || tower[16] || tower[17] || tower[25] != -1) continue;
        const auto* type = types[tower[1]];
        if (type[25] == 0) continue;
        const int target = *reinterpret_cast<const int*>(type[27]);
        if (money < static_cast<int>(types[target][15])) continue;
        reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_upgradeTower"))(game, id, target);
        log("ORIGINAL_CAMPAIGN_UPGRADE instance=" + std::to_string(id) + " target=" + std::to_string(target));
        return;
    }
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
    // Original EAGLView initialization passes zero; an omitted argument randomly preloads unused custom bosses.
    reinterpret_cast<void(__cdecl*)(int)>(requireExport("WAVES_init"))(0); log("WAVES_READY customBosses=0");
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
        : operation == "speed" ? 18000
        : operation == "flood" ? 240000
        : operation == "autoplay" ? 900000
        : 1000;
    bool windowClosed = false;
    // Each speed interval excludes one second of input/renderer settling.
    constexpr ULONGLONG SpeedEvents[] = {2000, 5000, 6000, 9000, 10000, 13000, 14000, 17000};
    constexpr double ExpectedSpeeds[] = {1.0, 2.0, 4.0, 1.0};
    unsigned speedEvent = 0;
    int speedClock = 0;
    ULONGLONG speedHostAt = 0;
    activeGame = game;
    *reinterpret_cast<void**>(hostView.data() + 0x40) = hostMenu.data();
    *reinterpret_cast<void**>(hostMenu.data() + 0x15c) = game;
    if (operation == "weapons") { originalWeaponsProbe(game, mode, output); return; }
    if (operation == "guiweapons") { originalGuiWeaponsProbe(game, output); return; }
    if (operation == "weaponcombat") { originalWeaponCombat(game, mode, output); return; }
    if (operation == "play") ShowWindow(windowHandle, SW_SHOW);
    while (GetTickCount64() - start < duration) {
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (operation == "speed" && speedEvent < std::size(SpeedEvents) && elapsed >= SpeedEvents[speedEvent]) {
            const int clock = reinterpret_cast<int*>(game)[10];
            if (speedEvent % 2 == 0) { speedClock = clock; speedHostAt = elapsed; }
            else {
                const double expected = ExpectedSpeeds[speedEvent / 2];
                const double measured = static_cast<double>(clock - speedClock) / static_cast<double>(elapsed - speedHostAt);
                nlohmann::json sample{{"expected", expected}, {"measured", measured},
                    {"simulationDeltaMs", clock - speedClock}, {"hostDeltaMs", elapsed - speedHostAt}};
                trace["speeds"].push_back(sample);
                log("ORIGINAL_SPEED_SAMPLE " + sample.dump());
                capture(output / ("speed-" + std::to_string(speedEvent / 2) + ".png"), 480, 800);
                if (measured < expected * 0.8 || measured > expected * 1.2)
                    throw std::runtime_error("Original speed control did not change the simulation clock as expected");
                if (speedEvent < 7) {
                    SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(460, 56));
                    SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(460, 56));
                }
            }
            ++speedEvent;
        }
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
            log("ORIGINAL_COMMAND togglePlaySpeed x2 issued; actual speed requires measurement");
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
        original_audio::update();
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

/** Exercise the original score serializer across reloads, retaining date and replay bytes. */
void originalScoreRoundtrip() {
    auto allocate = reinterpret_cast<void*(__cdecl*)()>(requireExport("SCORES_alloc"));
    auto load = reinterpret_cast<void(__cdecl*)(void*, const char*)>(requireExport("SCORES_load"));
    auto save = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("SCORES_save"));
    auto release = reinterpret_cast<void(__cdecl*)(void*)>(requireExport("SCORES_free"));
    auto add = reinterpret_cast<int(__cdecl*)(void*, const char*, int, double, const void*, unsigned)>(requireExport("SCORES_add"));
    // A separate test key never overwrites actual map scores, even in a reused evidence directory.
    constexpr const char* Key = "scores_host_epoch_check";
    // Include a fractional second and nonempty replay payload to expose lossy platform serialization.
    constexpr double ReferenceSeconds = 800000000.125;
    const unsigned char replay[] = {0x01, 0x23, 0x45, 0x67, 0x89};
    for (int cycle = 0; cycle < 3; ++cycle) {
        void* scores = allocate();
        load(scores, Key);
        auto* table = static_cast<std::uint32_t*>(scores);
        if (cycle == 0 && table[10] == 0) add(scores, "epoch-check", 1234, ReferenceSeconds, replay, sizeof(replay));
        if (table[10] != 1) throw std::runtime_error("Original score roundtrip lost its record");
        const auto* record = reinterpret_cast<const std::uint32_t*>(table[0]);
        const double date = *reinterpret_cast<const double*>(record + 4);
        if (date != ReferenceSeconds || record[0] != 1234 || record[3] != sizeof(replay)
            || std::memcmp(reinterpret_cast<const void*>(record[2]), replay, sizeof(replay)) != 0)
            throw std::runtime_error("Original score roundtrip changed date, score, or replay bytes");
        log("ORIGINAL_SCORE_ROUNDTRIP cycle=" + std::to_string(cycle) + " date=" + std::to_string(date)
            + " replayBytes=" + std::to_string(record[3]));
        save(scores);
        release(scores);
    }
    log("ORIGINAL_SCORE_ROUNDTRIP_DONE cycles=3");
}

/** Select one shipped DLC map using only original menu clicks and carousel drags. */
void originalDlcSelection(void* menu, const std::filesystem::path& output, const std::string& wanted) {
    const auto click = [&](int x, int y) {
        SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    };
    const ULONGLONG start = GetTickCount64();
    ULONGLONG nextAction = 1800, startedAt = 0;
    int stage = 0;
    while (GetTickCount64() - start < 180000) {
        const auto elapsed = GetTickCount64() - start;
        void* game = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
        auto* selection = *reinterpret_cast<std::uint32_t**>(static_cast<unsigned char*>(menu) + 0x164);
        if (game) {
            if (playerState(game).at("map") != wanted) throw std::runtime_error("DLC GUI started wrong map");
            if (!startedAt) startedAt = elapsed;
            reinterpret_cast<void(__cdecl*)(void*, int, int)>(requireExport("GAME_setResolution"))(game, 480, 800);
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_update"))(game);
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_render"))(game);
            if (elapsed >= startedAt + 1500) {
                capture(output / (wanted + "-game.png"), 480, 800);
                log("ORIGINAL_DLC_GUI_DONE " + playerState(game).dump());
                return;
            }
        } else {
            if (elapsed >= nextAction) {
                nextAction = elapsed + 900;
                if (stage == 0) { click(45, 210); ++stage; }
                else if (selection && stage == 1) { click(450, 785); ++stage; }
                else if (selection && stage == 2) { if (selection[25]) click(30, 50); ++stage; }
                else if (selection) {
                    // MAPMENU owns the list, selected index, and sorting; inspect without changing them.
                    const unsigned index = selection[11];
                    const char* selected = index < selection[9] ? *reinterpret_cast<const char**>(selection[5] + index * 40) : "";
                    if (wanted == selected) click(240, 350);
                    else {
                        unsigned target = 0;
                        while (target < selection[9] && wanted != *reinterpret_cast<const char**>(selection[5] + target * 40)) ++target;
                        if (target == selection[9]) throw std::runtime_error("DLC map absent from original list");
                        const int from = target > index ? 400 : 100, to = target > index ? 200 : 300;
                        SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from, 350));
                        SendMessageW(windowHandle, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((from + to) / 2, 350));
                        SendMessageW(windowHandle, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(to, 350));
                        // Release inside the tile: a real drag must not activate an intermediate map.
                        SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(to, 350));
                    }
                }
            }
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_update"))(menu);
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_render"))(menu);
        }
        original_audio::update(); SwapBuffers(windowDc);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(16);
    }
    throw std::runtime_error("DLC GUI selection timed out");
}

/** Check original sound pool, stop, looping and effects-setting contracts on the real mixer. */
void originalAudioProbe() {
    if (!original_audio::enabled) throw std::runtime_error("Audio verification requires -Audio");
    auto load = reinterpret_cast<int(__cdecl*)(const char*)>(requireExport("SOUND_load"));
    auto play = reinterpret_cast<int(__cdecl*)(int, float, float, float, float, float, int)>(requireExport("SOUND_play"));
    auto playing = reinterpret_cast<int(__cdecl*)(int)>(requireExport("SOUND_sourceIsPlaying"));
    auto stop = reinterpret_cast<void(__cdecl*)(int)>(requireExport("SOUND_stop"));
    const int ui = load("achievement"), looping = load("flamethrower");
    std::array<int, 3> uiSources{};
    // Low verification gain avoids changing the user's actual account volume setting.
    for (auto& source : uiSources) source = play(ui, 0, 0, 0, 0.02f, 0, 1);
    const int exhausted = play(ui, 0, 0, 0, 0.02f, 0, 1);
    if (exhausted != -1) throw std::runtime_error("Original UI sound pool did not enforce its three sources");
    const int loopSource = play(looping, 0, 0, 0, 0.02f, 0, 1);
    Sleep(200);
    for (int source : uiSources) if (source < 0 || !playing(source)) throw std::runtime_error("Native UI voice is not playing");
    if (loopSource < 0 || !playing(loopSource)) throw std::runtime_error("Native effect loop is not playing");
    reinterpret_cast<void(__cdecl*)()>(requireExport("SOUND_stopLoopingSounds"))();
    if (playing(loopSource) || !playing(uiSources[0])) throw std::runtime_error("Original loop pool stop changed unrelated voices");
    for (int source : uiSources) { stop(source); if (playing(source)) throw std::runtime_error("Source did not stop"); }
    reinterpret_cast<void(__cdecl*)()>(requireExport("SOUND_disableEffects"))();
    if (play(ui, 0, 0, 0, 0.02f, 0, 0) != -1) throw std::runtime_error("Disabled effects still allocated a source");
    reinterpret_cast<void(__cdecl*)(float)>(requireExport("SOUND_enableEffects"))(1.0f);
    original_audio::baseVolume = 0.02f;
    original_audio::selectMusic("game", true);
    // Returning to the active tune must cancel an unfinished transition, as in the original manager.
    original_audio::selectMusic("build", true);
    original_audio::selectMusic("game", true);
    for (int tick = 0; tick < 25; ++tick) { Sleep(20); original_audio::update(); }
    const double position = original_audio::backend.musicPosition();
    if (!original_audio::musicPlaying || position <= 0.1 || original_audio::tune != "game" || !original_audio::pendingTune.empty())
        throw std::runtime_error("Native music did not retain the active tune after cancelling its pending transition");
    original_audio::baseVolume = 0; original_audio::refreshMusic();
    const double remembered = original_audio::positions["game"];
    original_audio::baseVolume = 0.02f; original_audio::refreshMusic();
    Sleep(100);
    if (!original_audio::musicPlaying || original_audio::backend.musicPosition() < remembered)
        throw std::runtime_error("Native music unmute did not resume the remembered position");
    // An unreadable music resource must leave healthy effect playback available.
    original_audio::selectMusic("__verification_missing_tune__", true);
    for (int tick = 0; tick < 35; ++tick) { Sleep(20); original_audio::update(); }
    const int healthyEffect = play(ui, 0, 0, 0, 0.02f, 0, 1);
    if (!original_audio::musicFailed || healthyEffect < 0 || !playing(healthyEffect))
        throw std::runtime_error("A failed music resource disabled unrelated effect playback");
    stop(healthyEffect);
    log("ORIGINAL_AUDIO_DONE effects=" + std::to_string(original_audio::effectStarts)
        + " music=" + std::to_string(original_audio::musicStarts) + " remembered=" + std::to_string(remembered) + " musicIsolation=1 pendingCancellation=1");
}

#include "late-battle-probe.hpp"

/** Drive the real original MENU/MAPMENU object with the host's Windows pointer. */
void originalMenuScene(const std::filesystem::path& output, const Scenario& scenario) {
    const bool scripted = scenario.operation == "menuauto";
    const bool optionsMode = scripted && scenario.modeName == "options";
    const bool helpMode = scripted && scenario.modeName == "help";
    const bool newGameMode = scripted && scenario.modeName == "newgame";
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
    if (!scripted) {
        // Restore the player's F5 checkpoint into the original paused Resume menu on cold startup.
        // Scripted continue keeps its separate canonical save contract and isolated state.
        const auto checkpoint = dataDirectory / "menu-save-hotkey.bin";
        std::vector<std::uint8_t> saved;
        if (readSaveState(checkpoint, saved)) {
            restoreSavedGame(menu, checkpoint, "ORIGINAL_STARTUP_LOAD");
            auto& loaded = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
            if (loaded && gameHasEnded(loaded)) {
                // A terminal checkpoint must not replay settlement or grant progression a second time.
                void* ended = loaded; loaded = nullptr;
                reinterpret_cast<void(__cdecl*)(void*)>(requireExport("GAME_free"))(ended);
                log("ORIGINAL_STARTUP_TERMINAL_SAVE ignored");
            }
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("MENU_backToMainMenu"))(menu);
            menuShow(menu);
            if (loaded) log("ORIGINAL_STARTUP_RESUME_READY " + playerState(loaded).dump());
        } else if (std::filesystem::exists(checkpoint)) {
            log("STATE_WARNING The saved checkpoint could not be read; the original new-game menu remains available.");
        }
    }
    if (scripted && scenario.modeName.rfind("dlc-", 0) == 0) {
        originalDlcSelection(menu, output, scenario.modeName.substr(4));
        return;
    }
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
    const bool lateBattle = scripted && scenario.modeName == "latebattle";
    late_battle_probe::Driver lateBattleDriver;
    const auto savePath = scenario.modeName == "continuehotkey" || lateBattle ? dataDirectory / "menu-save-hotkey.bin" : saveStatePath(output);
    // "continue" runs the original GAME_fromIBS restore path over bytes written by an earlier run.
    const bool continueMode = scripted && (scenario.modeName == "continue" || scenario.modeName == "Continue" || scenario.modeName == "continuehotkey" || lateBattle);
    const bool allCampaign = scripted && scenario.modeName == "campaignall";
    const bool fullCampaign = allCampaign || (scripted && scenario.modeName == "campaignfull");
    const bool longCampaign = scripted && (fullCampaign || scenario.modeName == "campaign" || scenario.modeName == "Campaign");
    const bool restoreStress = scripted && scenario.modeName == "restorestress";
    if (continueMode) {
        // Only read the bytes now; the original transition runs once MAPMENU exists in the loop below.
        if (readSaveState(savePath, loadedIbsBytes))
            log("ORIGINAL_SAVE_CONTINUE_READY file=" + savePath.string() + " bytes=" + std::to_string(loadedIbsBytes.size()));
        else throw std::runtime_error("Continue requires an original save: " + savePath.string());
    }
    const ULONGLONG start = GetTickCount64();
    // Bounded so the fixed bridge runner can finish; interactive clicks are still accepted while it runs.
    // Interactive menu runs until the user closes the window; scripted probes stay bounded.
    ULONGLONG duration = scripted ? (allCampaign ? 1800000 : longCampaign ? 900000 : optionsMode ? 12000 : helpMode ? 9000 : 45000) : (std::numeric_limits<ULONGLONG>::max)();
    if (newGameMode) duration = 60000;
    if (lateBattle) duration = 600000;
    // The final save is tutorialDoneAt+21s; leave 8s for the queued restore and observation.
    constexpr ULONGLONG ScriptedSaveTailMs = 29000;
    // New Game begins at +26s, then needs menu/tile delays and a later frame to verify replacement.
    constexpr ULONGLONG NewGameSelectionTailMs = 8000;
    // Repeat the real F9 path on a started game, leaving several seconds after its last restore.
    constexpr int RestoreStressCycles = 12;
    int restoreCycles = 0;
    log("ORIGINAL_MENU_DURATION ms=" + std::to_string(duration) + " campaign=" + std::string(longCampaign ? "1" : "0")
        + " continue=" + std::string(continueMode ? "1" : "0"));
    bool capturedMain = false, capturedMap = false, capturedGame = false, capturedPause = false, windowClosed = false;
    bool hudCaptured = false, pauseVerified = false, tutorialCaptured = false, saveTested = false, combatCaptured = false, endCaptured = false;
    bool endCapturedLate = false;
    ULONGLONG endCapturedAt = 0, endClickAt = 0, endNextSample = 0;
    double lastEndNonWhite = 0.0;
    unsigned endClickStep = 0;
    unsigned optionStep = 0;
    unsigned helpStep = 0;
    // Replace an active restored game using only the original New Game/tile buttons.
    unsigned newGameStep = 0, newGameTaps = 0;
    ULONGLONG newGameActionAt = 0;
    int replacedGameClock = 0;
    // Follow the original victory -> unlock slides -> next map -> active save/restore chain.
    ULONGLONG nextMapAt = 0;
    int nextMapClock = -1;
    bool nextMapSaved = false, nextMapRestored = false;
    bool nextMapRestoreVerified = false, mediumSpeedSet = false;
    ULONGLONG mediumNextWave = 0, mediumEndAt = 0;
    // Later stages: 0=medium, 1/3=original continuation, 2/4=hard/insane combat.
    int laterStage = 0, laterClicks = 0;
    ULONGLONG laterClickAt = 0, laterNextWave = 0, laterEndAt = 0;
    bool pauseFrozen = false, resumed = false, resumeVerified = false;
    int tutorialTap = 0, buildStep = 0;
    bool tutorialDone = false, continueRestored = false, loggedPreUpdate = false;
    bool hotkeySaveTested = false, hotkeyLoadTested = false;
    bool continueFadeStart = false, continueFadeEnd = false, capturedTowers = false;
    unsigned long long frameCount = 0;
    ULONGLONG nextHeartbeat = 0;
    int guiBuildIndex = 0, guiBuildPhase = 0;
    ULONGLONG guiBuildPhaseAt = 0;
    int autoWaveCount = 0;
    // One request batch per second is enough; the original owns command timing and build delays.
    ULONGLONG nextDefenseAt = 0;
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
        if (helpMode && helpStep < 2 && elapsed >= 2000 + helpStep * 3000) {
            const int x = helpStep == 0 ? 45 : 450, y = helpStep == 0 ? 314 : 780;
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            ++helpStep;
        }
        if (optionsMode && optionStep < 6 && elapsed >= 2000 + optionStep * 1500) {
            // New-game main menu has Options at y=262; its five original setting rows are spaced by 50px.
            const int x = optionStep == 0 ? 45 : 300;
            const int y = optionStep == 0 ? 262 : 100 + static_cast<int>(optionStep) * 50;
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            log("ORIGINAL_OPTIONS_CLICK step=" + std::to_string(optionStep));
            ++optionStep;
        }
        // Heartbeat: distinguishes a stalled guest call from a merely slow run.
        if (scripted && longCampaign && elapsed >= nextHeartbeat) {
            nextHeartbeat = elapsed + 5000;
            void* heartbeatGame = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
            log("ORIGINAL_CAMPAIGN_HEARTBEAT frame=" + std::to_string(frameCount) + " "
                + (heartbeatGame ? playerState(heartbeatGame).dump() : std::string("nogame")));
        }
        // Serve queued input before reading any GAME pointer for this frame.
        if (pendingRestore) {
            pendingRestore = false;
            restoreSavedGame(menu, pendingRestorePath, pendingRestoreTag);
        }
        void* game = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
        if (lateBattle) lateBattleDriver.verifyRestored(game, elapsed);
        void* page = *reinterpret_cast<void**>(menu);
        bool showing = *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4) != 0;
        if (newGameMode && saveTested && tutorialDone && newGameStep == 0 && elapsed >= tutorialDoneAt + 26000) {
            replacedGameClock = currentGameClock(menu);
            SendMessageW(windowHandle, WM_KEYDOWN, VK_ESCAPE, 0);
            newGameStep = 1; newGameActionAt = elapsed + 1500;
        } else if (newGameMode && newGameStep && elapsed >= newGameActionAt) {
            if (newGameStep == 1) {
                SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(45, 210));
                SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(45, 210));
                newGameStep = 2; newGameActionAt = elapsed + 1500;
            } else if (newGameStep == 2) {
                if (page != findExport("startGamePage")) throw std::runtime_error("Original New Game button did not open map selection");
                capture(output / "newgame-selection.png", 480, 800);
                newGameStep = 3;
            } else if (game && !showing && !playerState(game).at("gameStarted").get<bool>() && currentGameClock(menu) < replacedGameClock) {
                capture(output / "newgame-replaced.png", 480, 800);
                log("ORIGINAL_NEWGAME_GUI_DONE previousClock=" + std::to_string(replacedGameClock) + " " + playerState(game).dump());
                break;
            } else {
                if (++newGameTaps > 4) throw std::runtime_error("Original New Game tile did not replace the active game");
                SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(240, 350));
                SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(240, 350));
                newGameActionAt = elapsed + 2000;
            }
            // Input may have replaced GAME; acquire all frame-local pointers again.
            continue;
        }
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
        if (scripted && !continueMode && !longCampaign && tutorialDone)
            duration = (std::max)(duration, tutorialDoneAt + (restoreStress ? ScriptedSaveTailMs + 7000
                : newGameMode ? ScriptedSaveTailMs + NewGameSelectionTailMs : ScriptedSaveTailMs));
        if (restoreStress && saveTested && restoreCycles < RestoreStressCycles && elapsed >= pauseAt + 10000 + restoreCycles * 750) {
            ++restoreCycles;
            log("ORIGINAL_RESTORE_STRESS request=" + std::to_string(restoreCycles));
            SendMessageW(windowHandle, WM_KEYDOWN, VK_F9, 0);
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
        // restoration is served at the next frame boundary).
        if (scripted && !continueMode && !longCampaign && game && tutorialDone && !hotkeySaveTested && elapsed >= tutorialDoneAt + 9500) {
            hotkeySaveTested = true;
            log("ORIGINAL_HOTKEY_TEST save");
            SendMessageW(windowHandle, WM_KEYDOWN, VK_F5, 0);
        }
        if (scripted && !continueMode && !longCampaign && game && tutorialDone && !hotkeyLoadTested && elapsed >= tutorialDoneAt + 10500) {
            hotkeyLoadTested = true;
            log("ORIGINAL_HOTKEY_TEST load");
            SendMessageW(windowHandle, WM_KEYDOWN, VK_F9, 0);
            log("ORIGINAL_HOTKEY_TEST load queued");
        }
        if (scripted && longCampaign && game && gameStartAt && elapsed >= nextStatLog) {
            nextStatLog = elapsed + 10000;
            log("ORIGINAL_CAMPAIGN_STAT " + playerState(game).dump());
        }
        // Once the original reports the game over (or the endgame page is up), the scripted campaign input must
        // stop: a stray wave click there was routed into the menu and started a whole new game
        // (campaign-20261015-054301:1540-1551), which replaced the settlement page under the capture gate.
        const bool gameIsOver = game && reinterpret_cast<int(__cdecl*)(void*)>(findExport("GAME_hasEnded"))(game) != 0;
        if (lateBattle && continueRestored && lateBattleDriver.tick(menu, game, page, gameIsOver, elapsed, output)) break;
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
        if (scripted && longCampaign && game && tutorialDone && !gameIsOver && !endCaptured
            && elapsed >= tutorialDoneAt + 6000 && elapsed >= nextDefenseAt) {
            nextDefenseAt = elapsed + 1000;
            campaignDefense(game);
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
                throw std::runtime_error("Original settlement page disappeared before capture");
            } else if (elapsed >= endCapturedAt + 2000 && fade >= 0.98f && steady && nonWhite >= 0.05) {
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
            } else if (elapsed >= endCapturedAt + 12000) {
                throw std::runtime_error("Original settlement did not finish fading in");
            }
        }
        if (longCampaign && endCapturedLate && !nextMapAt && elapsed >= endClickAt) {
            if (game && !gameIsOver && playerState(game).at("map") == "DividedAttention") {
                nextMapAt = elapsed;
                nextMapClock = currentGameClock(menu);
                log("ORIGINAL_CAMPAIGN_NEXT_MAP " + playerState(game).dump());
                SendMessageW(windowHandle, WM_KEYDOWN, VK_SPACE, 0);
            } else {
                if (page == findExport("endOfGamePage") && !playerState(game).at("won").get<bool>())
                    throw std::runtime_error("Campaign defense lost the original first map");
                if (++endClickStep > 14) throw std::runtime_error("Original victory continuation did not start the second map");
                // The unlock callbacks advance their own slide counters; no host progress flags are written.
                capture(output / ("menu-continue-" + std::to_string(endClickStep) + ".png"), 480, 800);
                const int clickY = page == findExport("endOfGamePage") ? 770
                    : page == findExport("startGamePage") ? 350 : 650;
                SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(240, clickY));
                SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(240, clickY));
                log("ORIGINAL_CAMPAIGN_CONTINUE_CLICK step=" + std::to_string(endClickStep)
                    + " y=" + std::to_string(clickY) + " pageChanged="
                    + std::to_string(page != *reinterpret_cast<void**>(menu)));
                endClickAt = elapsed + 2000;
            }
        }
        if (longCampaign && nextMapAt && game && !nextMapSaved && elapsed >= nextMapAt + 2500) {
            if (currentGameClock(menu) <= nextMapClock || !playerState(game).at("gameStarted").get<bool>())
                throw std::runtime_error("Second map did not start advancing");
            nextMapSaved = true;
            capture(output / "menu-second-map.png", 480, 800);
            SendMessageW(windowHandle, WM_KEYDOWN, VK_F5, 0);
            nextMapClock = currentGameClock(menu);
        }
        if (longCampaign && nextMapSaved && !nextMapRestored && elapsed >= nextMapAt + 3000) {
            nextMapRestored = true;
            SendMessageW(windowHandle, WM_KEYDOWN, VK_F9, 0);
        }
        // GAME_resume deliberately waits three seconds for a started game; observe beyond that countdown.
        if (longCampaign && nextMapRestored && !laterStage && elapsed >= nextMapAt + 9000) {
            if (!nextMapRestoreVerified) {
                if (currentGameClock(menu) <= nextMapClock || playerState(game).at("map") != "DividedAttention")
                    throw std::runtime_error("Second map restore did not preserve map and advance");
                nextMapRestoreVerified = true;
                capture(output / "menu-second-restored.png", 480, 800);
                log("ORIGINAL_CAMPAIGN_DONE " + playerState(game).dump());
            }
            if (!fullCampaign) break;
            if (!gameIsOver) {
                if (!mediumSpeedSet) {
                    // Use the original button twice, giving the real 1x -> 2x -> 4x transitions.
                    for (int click = 0; click < 2; ++click) {
                        SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(460, 56));
                        SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(460, 56));
                    }
                    mediumSpeedSet = true;
                    mediumNextWave = elapsed + 15000;
                }
                campaignDefense(game);
                if (elapsed >= mediumNextWave) {
                    mediumNextWave = elapsed + 15000;
                    SendMessageW(windowHandle, WM_KEYDOWN, VK_SPACE, 0);
                    log("ORIGINAL_MEDIUM_WAVE " + playerState(game).dump());
                }
            } else if (page == findExport("endOfGamePage")) {
                if (!playerState(game).at("won").get<bool>())
                    throw std::runtime_error("Original second-map campaign defense lost");
                if (!mediumEndAt) mediumEndAt = elapsed;
                const float fade = *reinterpret_cast<float*>(static_cast<unsigned char*>(menu) + 0xec);
                if (elapsed >= mediumEndAt + 2500 && fade >= 0.98f) {
                    capture(output / "menu-medium-victory.png", 480, 800);
                    log("ORIGINAL_MEDIUM_CAMPAIGN_DONE " + playerState(game).dump());
                    if (!allCampaign) break;
                    laterStage = 1; laterClickAt = elapsed + 500;
                }
            }
        }
        if (allCampaign && laterStage && game) {
            const std::string wanted = laterStage <= 2 ? "MuttonsFolly" : "Madness";
            if (laterStage % 2) {
                if (!gameIsOver && playerState(game).at("map") == wanted) {
                    ++laterStage;
                    laterNextWave = elapsed + (wanted == "Madness" ? 25000 : 15000);
                    laterEndAt = 0;
                    // Keep an authentic fresh-map checkpoint for independent continuation checks.
                    SendMessageW(windowHandle, WM_KEYDOWN, VK_F5, 0);
                    if (wanted != "Madness") SendMessageW(windowHandle, WM_KEYDOWN, VK_SPACE, 0);
                    for (int click = 0; click < 2; ++click) {
                        SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(460, 56));
                        SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(460, 56));
                    }
                    capture(output / (wanted + "-started.png"), 480, 800);
                    log("ORIGINAL_LATER_CAMPAIGN_STARTED " + playerState(game).dump());
                } else if (elapsed >= laterClickAt) {
                    if (++laterClicks > 14) throw std::runtime_error("Original later campaign continuation did not advance");
                    const int y = page == findExport("endOfGamePage") ? 770
                        : page == findExport("startGamePage") ? 350 : 650;
                    SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(240, y));
                    SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(240, y));
                    laterClickAt = elapsed + 2000;
                }
            } else if (!gameIsOver) {
                campaignDefense(game);
                if (elapsed >= laterNextWave) {
                    laterNextWave = elapsed + (wanted == "Madness" ? 25000 : 15000);
                    SendMessageW(windowHandle, WM_KEYDOWN, VK_SPACE, 0);
                    log("ORIGINAL_LATER_CAMPAIGN_WAVE " + playerState(game).dump());
                }
            } else if (page == findExport("endOfGamePage")) {
                if (!playerState(game).at("won").get<bool>())
                    throw std::runtime_error("Original later campaign defense lost on " + wanted);
                if (!laterEndAt) laterEndAt = elapsed;
                const float fade = *reinterpret_cast<float*>(static_cast<unsigned char*>(menu) + 0xec);
                if (elapsed >= laterEndAt + 2500 && fade >= 0.98f) {
                    capture(output / (wanted + "-victory.png"), 480, 800);
                    log("ORIGINAL_LATER_CAMPAIGN_WON " + playerState(game).dump());
                    if (laterStage == 4) {
                        log("ORIGINAL_ALL_CAMPAIGN_DONE settings=" + nlohmann::json(hostAccountInts).dump());
                        break;
                    }
                    ++laterStage; laterClicks = 0; laterClickAt = elapsed + 500;
                }
            }
        }
        if (!capturedMain && elapsed >= 1500) { capture(output / "menu-main.png", 480, 800); capturedMain = true; }
        // Scripted mode clicks each original tab centre (from the rodata hit layout) and records the page result.
        if (scripted && !optionsMode && !helpMode && game == nullptr && capturedMain && tabIndex < tabCount && elapsed >= nextTabClick && page == findExport("mainMenuPage")) {
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
            pendingRestore = true;
            pendingRestorePath = savePath;
            pendingRestoreTag = "ORIGINAL_SAVE_CONTINUE";
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
            // Exercise the original Resume tab rather than bypassing its hit test.
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(45, 262));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(45, 262));
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
            reinterpret_cast<void(__cdecl*)(void*)>(requireExport("OBS_free"))(obs);
            pendingRestore = true;
            pendingRestorePath = savePath;
            pendingRestoreTag = "ORIGINAL_LOAD_IBS";
        }
        // Synchronous menu clicks can start/restart a GAME; render only the current menu-owned object.
        game = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
        page = *reinterpret_cast<void**>(menu);
        showing = *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4) != 0;
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
                // MENU_endGame may free replay games; the original outer dispatcher renders the menu next.
                menuUpdate(menu); menuRender(menu);
            }
            else { gameUpdate(game); gameRender(game); }
        }
        if (!capturedMap && page && page == findExport("startGamePage")) { capture(output / "menu-mapselect.png", 480, 800); capturedMap = true; }
        if (!capturedGame && game) { capture(output / "menu-in-game.png", 480, 800); capturedGame = true; }
        original_audio::update();
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
    if (optionsMode && optionStep == 6 && *reinterpret_cast<void**>(menu) == findExport("optionsMenuPage"))
        log("ORIGINAL_OPTIONS_DONE settings=" + nlohmann::json(hostAccountInts).dump()
            + " floats=" + nlohmann::json(hostAccountFloats).dump());
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
    if (std::filesystem::exists(package.parent_path() / "textures")) convertedTextures = package.parent_path() / "textures";
    // Portable saves stay beside Play.cmd; development runs use their selected evidence state directory.
    dataDirectory = resolveDataDirectory(package, outputDirectory);
    persistedStorePath = dataDirectory / "platform-store.txt";
    const Scenario scenario = parseScenario(operation);
    // "map" previously returned without loading anything and falsely looked like a passing probe.
    const std::set<std::string> supported = {"play", "scene", "input", "combat", "upgrade", "flood", "autoplay", "menu", "menuauto", "levels", "scores", "speed", "audio", "weapons", "guiweapons", "weaponcombat"};
    if (!supported.count(scenario.operation)) throw std::runtime_error("Unsupported probe operation: " + scenario.operation);
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
    createGraphics(); registerGraphics(package.parent_path() / "gl-signatures.json");
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
    redirectGs(package.parent_path() / "gs-instructions.json");
    for (const char* name : {"__wrap_malloc", "__wrap_calloc", "__wrap_realloc", "__wrap_free", "__wrap_memcpy",
                             "__wrap_memset", "__wrap_printf"}) {
        const std::string underlying = std::string(name).substr(7);
        if (const auto hook = hooks.find(underlying); hook != hooks.end()) installHook(name, hook->second);
    }
    correctTowerRestoreAllocation();
    correctPointSpriteSubmission();
    correctScoreDateReader();
    installHook("VFS_getFile", reinterpret_cast<void*>(&nativeFile));
    installHook("VFS_getFilesOfType", reinterpret_cast<void*>(&nativeFiles));
    installHook("APPORTABLE_loadPng", reinterpret_cast<void*>(&nativePng));
    installHook("translate", reinterpret_cast<void*>(&nativeTranslate));
    installHook("get_current_realtime", reinterpret_cast<void*>(&nativeRealtime));
    installHook("objc_msgSend", reinterpret_cast<void*>(&nativeMessage));
    installHook("objc_msgSend_stret", reinterpret_cast<void*>(&nativeMessageStructBridge));
    installHook("objc_msgSend_fpret", reinterpret_cast<void*>(&nativeMessageFpret));
    original_audio::assets = assetsDirectory;
    original_audio::trace = &log;
    char audioSetting[8]{};
    original_audio::enabled = GetEnvironmentVariableA("TOWERMADNESS_X86_AUDIO", audioSetting, sizeof(audioSetting))
        && audioSetting[0] == '1';
    const std::pair<const char*, void*> audioHooks[] = {
        {"SOUND_loadSoundFile", reinterpret_cast<void*>(&original_audio::loadSound)},
        {"alGenBuffers", reinterpret_cast<void*>(&original_audio::genBuffers)},
        {"alDeleteBuffers", reinterpret_cast<void*>(&original_audio::deleteBuffers)},
        {"alGenSources", reinterpret_cast<void*>(&original_audio::genSources)},
        {"alDeleteSources", reinterpret_cast<void*>(&original_audio::deleteSources)},
        {"alSourcei", reinterpret_cast<void*>(&original_audio::sourceInt)},
        {"alSourcef", reinterpret_cast<void*>(&original_audio::sourceFloat)},
        {"alSource3f", reinterpret_cast<void*>(&original_audio::sourcePosition)},
        {"alSourcePlay", reinterpret_cast<void*>(&original_audio::play)},
        {"alSourceStop", reinterpret_cast<void*>(&original_audio::stop)},
        {"alGetSourcei", reinterpret_cast<void*>(&original_audio::getSource)},
        {"alListenerf", reinterpret_cast<void*>(&original_audio::listenerFloat)},
        {"alListener3f", reinterpret_cast<void*>(&original_audio::listenerPosition)},
        {"alDistanceModel", reinterpret_cast<void*>(&original_audio::distanceModel)},
        {"alcOpenDevice", reinterpret_cast<void*>(&original_audio::openDevice)},
        {"alcCreateContext", reinterpret_cast<void*>(&original_audio::createContext)},
        {"alcMakeContextCurrent", reinterpret_cast<void*>(&original_audio::makeCurrent)},
        {"alcCloseDevice", reinterpret_cast<void*>(&original_audio::closeDevice)},
        {"alcDestroyContext", reinterpret_cast<void*>(&original_audio::contextOperation)},
        {"alcSuspendContext", reinterpret_cast<void*>(&original_audio::contextOperation)},
        {"alcProcessContext", reinterpret_cast<void*>(&original_audio::contextOperation)},
        {"alcMacOSXMixerOutputRateProc", reinterpret_cast<void*>(&original_audio::mixerRate)},
        {"AudioSessionInitialize", reinterpret_cast<void*>(&original_audio::sessionInit)},
        {"AudioSessionSetProperty", reinterpret_cast<void*>(&original_audio::sessionProperty)},
        {"AudioSessionSetActive", reinterpret_cast<void*>(&original_audio::sessionActive)}
    };
    for (const auto& [name, function] : audioHooks) installHook(name, function);
    installHook("MENU_showCredits", reinterpret_cast<void*>(&nativeCredits));
    reinterpret_cast<void(__cdecl*)()>(requireExport("SOUND_init"))();
    loadPersistedStore();
    const auto soundLevel = hostAccountInts.find("soundvolume");
    if (soundLevel != hostAccountInts.end())
        reinterpret_cast<void(__cdecl*)(float)>(requireExport("SOUND_enableEffects"))(static_cast<float>(soundLevel->second));
    const auto musicLevel = hostAccountFloats.find("musicvolume");
    if (musicLevel != hostAccountFloats.end()) original_audio::baseVolume = static_cast<float>(musicLevel->second);
    log("SOUND_READY enabled=" + std::to_string(original_audio::enabled));
    // The original MENU object must run its own code; only the direct-to-level probe uses the stubs.
    if (scenario.operation != "menu" && scenario.operation != "menuauto") {
        installHook("MENU_backToMainMenu", reinterpret_cast<void*>(&hostMenuButton));
        installHook("MENU_show", reinterpret_cast<void*>(&silentPlayback));
    }
    log("ENTER_ORIGINAL " + scenario.operation);
    if (scenario.operation == "audio") { originalAudioProbe(); return; }
    if (scenario.operation == "scores") { originalScoreRoundtrip(); return; }
    if (scenario.operation == "menu" || scenario.operation == "menuauto") { originalMenuScene(outputDirectory, scenario); return; }
    if (scenario.operation == "scene" || scenario.operation == "combat" || scenario.operation == "play" || scenario.operation == "input"
        || scenario.operation == "upgrade" || scenario.operation == "flood" || scenario.operation == "autoplay" || scenario.operation == "speed" || scenario.operation == "weapons" || scenario.operation == "guiweapons" || scenario.operation == "weaponcombat") {
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
