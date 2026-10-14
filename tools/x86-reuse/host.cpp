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
    std::fprintf(logFile ? logFile : stdout, "%s\n", message.c_str());
    std::fflush(logFile ? logFile : stdout);
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

/** Redirect only exported platform boundaries in the private loaded image. */
void installHook(const std::string& name, void* replacement) {
    hooks[name] = replacement;
    for (const auto& image : images) {
        const auto symbol = image->exports.find(name);
        // Apportable's verified assembly dispatchers have STT_NOTYPE and size zero.
        const bool assemblyDispatcher = name == "objc_msgSend" || name == "objc_msgSend_stret";
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
HostString hostStrings[8];
int hostStringCount = 0;
std::map<std::string, int> hostAccountInts;
std::map<std::string, std::string> hostAccountStrings;

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

/** Create the next host string wrapper holding real UTF-8 bytes. */
HostString* nextHostString() {
    HostString* value = &hostStrings[hostStringCount++ % 8];
    value->text[0] = 0;
    return value;
}

/** Read a key argument that may be a host string, a C string, or an opaque CFString. */
std::string hostKeyText(const void* value) {
    for (const auto& candidate : hostStrings)
        if (value == &candidate) return candidate.text;
    const char* text = static_cast<const char*>(value);
    if (printableCString(text)) return text;
    char tag[32]; std::snprintf(tag, sizeof(tag), "obj:%p", value); return tag;
}

/** Implement only the pointer and platform selectors with known call-site semantics. */
std::uint32_t __cdecl nativeMessage(void* receiver, const char* selector, ...) {
    // Objective-C messages to nil have a defined zero result.
    if (!receiver) return 0;
    const std::string name = selectorName(selector);
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
    // UIKit lifecycle: the host owns the window and GL context, so only the idle timer is accepted.
    if (name == "sharedApplication") return reinterpret_cast<std::uint32_t>(&hostApplicationToken);
    if (name == "setIdleTimerDisabled:") return 0;
    if (name == "delegate") return 0; // A headless host has no UIKit delegate.
    // Purchase verification is explicitly out of scope; the full-version menu pages are required.
    if (name == "unlockedFullVersion") { log("PLATFORM_PURCHASE unlockedFullVersion -> 1 (full build)"); return 1; }
    // Store gating is out of scope; every shipped gamemode is startable in this full build.
    if (name == "canStartMapWithName:") {
        va_list arguments; va_start(arguments, selector);
        const char* mapName = va_arg(arguments, const char*); va_end(arguments);
        log(std::string("PLATFORM_STORE canStartMapWithName: ") + ((mapName && printableCString(mapName)) ? mapName : "(unprintable)") + " -> 1 (content shipped; purchase gate excluded)");
        return 1;
    }
    // Progress/account sync is kept as a real in-memory key/value contract for this run.
    if (name == "current") return reinterpret_cast<std::uint32_t>(&hostGameAccountToken);
    if (name == "isGameCenter") { log("PLATFORM_CAPABILITY isGameCenter -> 0 (no Game Center on this host)"); return 0; }
    if (name == "isGameCenterAvailable") { log("PLATFORM_CAPABILITY isGameCenterAvailable -> 0 (no Game Center on this host)"); return 0; }
    if (name == "flush") { log("PLATFORM_ACCOUNT flush"); return 0; }
    if (name == "name") {
        HostString* value = nextHostString();
        std::snprintf(value->text, sizeof(value->text), "host");
        log("PLATFORM_STRING name -> host");
        return reinterpret_cast<std::uint32_t>(value);
    }
    if (name == "UTF8String") {
        for (auto& candidate : hostStrings)
            if (receiver == &candidate) return reinterpret_cast<std::uint32_t>(candidate.text);
        log("PLATFORM_STRING UTF8String on a non-host receiver");
        return reinterpret_cast<std::uint32_t>("");
    }
    if (name == "setCurrentContext:") { log("PLATFORM_GL setCurrentContext: (host retains its context)"); return 0; }
    if (name == "stopAnimation") { log("PLATFORM_UI stopAnimation -> 0"); return 0; }
    // Campaign/DLC progress accessors: an empty store is a fresh profile (locked); purchase content is excluded.
    if (name.rfind("unlocked", 0) == 0) {
        const auto found = hostAccountInts.find(name);
        const int result = found == hostAccountInts.end() ? 0 : found->second;
        log("PLATFORM_PROGRESS " + name + " -> " + std::to_string(result) + " (fresh profile)");
        return static_cast<std::uint32_t>(result);
    }
    if (name == "stringWithUTF8String:") {
        va_list arguments; va_start(arguments, selector);
        const char* text = va_arg(arguments, const char*);
        va_end(arguments);
        HostString* value = nextHostString();
        std::snprintf(value->text, sizeof(value->text), "%s", printableCString(text) ? text : "");
        log("PLATFORM_STRING utf8 -> " + std::string(value->text));
        return reinterpret_cast<std::uint32_t>(value);
    }
    if (name == "stringWithFormat:") {
        va_list arguments; va_start(arguments, selector);
        const char* format = va_arg(arguments, const char*);
        const char* mode = va_arg(arguments, const char*);
        va_end(arguments);
        HostString* value = nextHostString();
        // The format constant is a CFString this host does not implement; build the documented key.
        std::snprintf(value->text, sizeof(value->text), "scores_%s", printableCString(mode) ? mode : "");
        log("PLATFORM_STRING format=" + std::to_string(reinterpret_cast<std::uintptr_t>(format)) + " -> " + std::string(value->text));
        return reinterpret_cast<std::uint32_t>(value);
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
        log("PLATFORM_ACCOUNT setString " + text + "=" + hostAccountStrings[text]);
        return 0;
    }
    if (name == "deleteSavegameForMode:") {
        va_list arguments; va_start(arguments, selector);
        const void* key = va_arg(arguments, const void*);
        va_end(arguments);
        const std::string text = hostKeyText(key);
        hostAccountInts.erase(text); hostAccountStrings.erase(text);
        log("PLATFORM_ACCOUNT deleteSavegame " + text);
        return 0;
    }
    // Ranking and DLC purchase endpoints are out of scope by project rules; log the exact selector.
    if (name == "reportScore:modename:") { log("PLATFORM_RANKING_EXCLUDED " + name); return 0; }
    if (name == "openURL:" || name == "URLWithString:") { log("PLATFORM_PURCHASE_EXCLUDED " + name); return 0; }
    log("UNIMPLEMENTED_OBJC_SELECTOR " + name + " receiver=" + std::to_string(reinterpret_cast<std::uintptr_t>(receiver)));
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
    const ULONGLONG start = GetTickCount64();
    // Bounded so the fixed bridge runner can finish; interactive clicks are still accepted while it runs.
    const ULONGLONG duration = scripted ? 30000 : 120000;
    bool capturedMain = false, capturedMap = false, capturedGame = false, capturedPause = false, windowClosed = false;
    void* lastPage = nullptr; void* lastGame = nullptr;
    ULONGLONG gameStartAt = 0, nextTabClick = 1800, mapSelectAt = 0;
    std::size_t tabIndex = 0;
    int mapStep = 0;
    ULONGLONG mapStepAt = 0;
    // MAPMENU_touchesEnded only starts the selected entry when the tap is inside these rodata bounds.
    const float mapHitMinX = rodataFloat(0x362e1c), mapHitMinY = rodataFloat(0x362b58), mapHitMaxY = rodataFloat(0x362e54);
    log("ORIGINAL_MAP_HIT minX=" + std::to_string(mapHitMinX) + " minY=" + std::to_string(mapHitMinY) + " maxY=" + std::to_string(mapHitMaxY));
    if (!scripted) ShowWindow(windowHandle, SW_SHOW);
    while (GetTickCount64() - start < duration) {
        const ULONGLONG elapsed = GetTickCount64() - start;
        void* game = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c);
        void* page = *reinterpret_cast<void**>(menu);
        const bool showing = *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(menu) + 4) != 0;
        if (page != lastPage) { lastPage = page; log("ORIGINAL_MENU_PAGE_CHANGED " + std::to_string(reinterpret_cast<std::uintptr_t>(page))); }
        if (page == findExport("startGamePage") && mapSelectAt == 0) mapSelectAt = elapsed;
        if (game && game != lastGame) {
            lastGame = game; gameStartAt = elapsed;
            gameSetResolution(game, 480, 800); glViewport(0, 0, 480, 800);
            log("ORIGINAL_MENU_GAME_STARTED " + std::to_string(reinterpret_cast<std::uintptr_t>(game)));
        }
        if (!capturedMain && elapsed >= 1500) { capture(output / "menu-main.png", 480, 800); capturedMain = true; }
        // Scripted mode clicks each original tab centre (from the rodata hit layout) and records the page result.
        if (scripted && capturedMain && tabIndex < tabCount && elapsed >= nextTabClick && page == findExport("mainMenuPage")) {
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
        if (scripted && game == nullptr && mapStep >= 1 && mapStep <= 3 && elapsed >= mapStepAt) {
            if (mapStep == 1) capture(output / "menu-campaign.png", 480, 800);
            // The campaign page draws the selected map with an explicit TAP TO START prompt; tap its tile centre.
            const int mapX = 240, mapY = 350;
            log("MENU_MAP_CLICK step=" + std::to_string(mapStep) + " " + std::to_string(mapX) + "," + std::to_string(mapY));
            SendMessageW(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(mapX, mapY));
            SendMessageW(windowHandle, WM_LBUTTONUP, 0, MAKELPARAM(mapX, mapY));
            log("MENU_MAP_CLICK_RESULT game=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c))));
            ++mapStep; mapStepAt = elapsed + 2000;
        }
        if (scripted && !capturedPause && game && gameStartAt && elapsed >= gameStartAt + 6000) {
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_backToMainMenu"))(menu);
            reinterpret_cast<void(__cdecl*)(void*)>(findExport("MENU_show"))(menu);
            log("ORIGINAL_MENU_PAUSE_REQUESTED");
            capturedPause = true;
        }
        if (game == nullptr || showing) { menuUpdate(menu); menuRender(menu); }
        else {
            if (gameHasEnded(game)) { gameSilence(game); menuEndGame(menu); log("ORIGINAL_MENU_GAME_ENDED"); }
            else gameUpdate(game);
            gameRender(game);
        }
        if (!capturedMap && page && page == findExport("startGamePage")) { capture(output / "menu-mapselect.png", 480, 800); capturedMap = true; }
        if (!capturedGame && game) { capture(output / "menu-in-game.png", 480, 800); capturedGame = true; }
        SwapBuffers(windowDc);
        MSG message{}; bool closed = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) closed = true; TranslateMessage(&message); DispatchMessageW(&message); }
        if (closed) { windowClosed = true; break; }
        Sleep(16);
    }
    if (!windowClosed) { menuRender(menu); glFinish(); capture(output / (scripted ? "menu-final.png" : "menu-pause.png"), 480, 800); }
    log(std::string("ORIGINAL_MENU_FINAL page=") + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(menu)))
        + " game=" + std::to_string(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x15c))));
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
                std::ostringstream line; line << "NATIVE_FRAME " << image->name << "+0x" << std::hex << caller - base; log(line.str());
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
}

/** Enter the selected original export without using a CPU interpreter or Android. */
void run(const std::filesystem::path& package, const std::string& operation, const std::filesystem::path& outputDirectory) {
    const auto directory = package / "lib/x86"; assetsDirectory = package / "assets";
    convertedTextures = package.parent_path().parent_path().parent_path().parent_path() / "assets/textures";
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
    std::ofstream unresolved(outputDirectory / "unresolved-imports.txt");
    for (const auto& name : missingNames) unresolved << name << '\n';
    if (scenario.operation == "map") return;
    redirectGs(package.parent_path() / "gs-instructions.json");
    for (const char* name : {"__wrap_malloc", "__wrap_calloc", "__wrap_realloc", "__wrap_free", "__wrap_memcpy", "__wrap_memset", "__wrap_printf"}) {
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
