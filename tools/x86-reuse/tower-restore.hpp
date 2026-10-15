// Correct the original x86 tower reader's undersized runtime allocation in the private image.
#pragma once

// TOWER_alloc reserves 0x70 bytes; saves contain only the 0x68-byte synchronized prefix.
constexpr std::size_t TowerRuntimeBytes = 0x70;
// Original Zero 1.22 TOWERS_fromIBS calls IBS_readBlock8Alloc at this symbol-relative offset.
constexpr std::size_t TowerRestoreReadCallOffset = 0xc3;

/** Read the unchanged serialized prefix into a complete runtime tower object. */
unsigned __cdecl readRestoredTower(void* ibs, void** destination) {
    const auto* input = static_cast<const std::uint32_t*>(ibs);
    const auto* bytes = reinterpret_cast<const unsigned char*>(input[0]);
    const unsigned length = bytes[input[3]];
    // Use the registered allocator so local diagnostic builds observe this allocation too.
    auto allocate = reinterpret_cast<void*(__cdecl*)(std::size_t)>(hooks.at("malloc"));
    *destination = allocate((std::max)(TowerRuntimeBytes, static_cast<std::size_t>(length)));
    if (!*destination) throw std::bad_alloc();
    auto read = reinterpret_cast<unsigned(__cdecl*)(void*, void*, unsigned char)>(requireExport("IBS_readBlock8"));
    return read(ibs, *destination, static_cast<unsigned char>(length));
}

/** Correct only the faulty allocation call, retaining the original tower restore and checksum logic. */
void correctTowerRestoreAllocation() {
    auto* call = static_cast<unsigned char*>(requireExport("TOWERS_fromIBS")) + TowerRestoreReadCallOffset;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    // Bind this correction to the verified reader and its first out-of-bounds write (+0x68).
    const unsigned char following[] = {0x8b, 0x45, 0xf0, 0xc7, 0x40, 0x68, 0xff, 0xff, 0xff, 0xff};
    if (call[0] != 0xe8 || call + 5 + displacement != requireExport("IBS_readBlock8Alloc")
        || std::memcmp(call + 5, following, sizeof(following)) != 0)
        throw std::runtime_error("Unsupported original TOWERS_fromIBS allocation call");
    displacement = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&readRestoredTower)
        - reinterpret_cast<std::uintptr_t>(call + 5));
    std::memcpy(call + 1, &displacement, sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    log("ORIGINAL_TOWER_RESTORE_ALLOCATION runtime=112 serialized=unchanged");
}
