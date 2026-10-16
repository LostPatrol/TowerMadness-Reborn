// Keep zero-area effects out of the original point-sprite batch without replacing it.
#pragma once

// Original ELF32 ABI: material ID followed by position, color, and pixel size.
using OriginalPointSpriteSubmit = void(__cdecl*)(int, float, float, float, float, float, float, float, float);
// The trampoline preserves the original batching and material-cache implementation.
OriginalPointSpriteSubmit originalPointSpriteSubmit = nullptr;

/** Omit invisible primitives; all visible submissions retain the original implementation. */
void __cdecl correctedPointSpriteSubmit(int material, float x, float y, float z,
    float red, float green, float blue, float alpha, float size) {
    if (size <= 0.0f) return;
    originalPointSpriteSubmit(material, x, y, z, red, green, blue, alpha, size);
}

/** Redirect the verified original entry while keeping its complete five-byte prologue. */
void correctPointSpriteSubmission() {
    auto* entry = static_cast<unsigned char*>(findExport("PGL_addPointSprite"));
    // These complete instructions precede the original PIC setup and contain no relative operands.
    constexpr unsigned char OriginalPrologue[] = {0x55, 0x89, 0xe5, 0x53, 0x57};
    if (!entry || std::memcmp(entry, OriginalPrologue, sizeof(OriginalPrologue)) != 0)
        throw std::runtime_error("Unexpected original point-sprite entry");
    // Five copied bytes plus a five-byte relative jump back to the original function.
    constexpr unsigned TrampolineBytes = sizeof(OriginalPrologue) + 5;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, TrampolineBytes,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!trampoline) throw std::runtime_error("Point-sprite trampoline allocation failed");
    std::memcpy(trampoline, entry, sizeof(OriginalPrologue));
    trampoline[sizeof(OriginalPrologue)] = 0xe9;
    const auto displacement = reinterpret_cast<std::uint32_t>(entry + sizeof(OriginalPrologue))
        - reinterpret_cast<std::uint32_t>(trampoline + TrampolineBytes);
    std::memcpy(trampoline + sizeof(OriginalPrologue) + 1, &displacement, sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(), trampoline, TrampolineBytes);
    originalPointSpriteSubmit = reinterpret_cast<OriginalPointSpriteSubmit>(trampoline);
    installHook("PGL_addPointSprite", reinterpret_cast<void*>(&correctedPointSpriteSubmit));
    log("POINT_SPRITE_CORRECTION zero-area submissions omitted; original batch retained");
}
