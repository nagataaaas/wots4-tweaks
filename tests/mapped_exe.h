// Maps a copy of the decrypted exe into the test process (sections and base relocations only)
// and records what mod.dll asks of a fake ModLoader. No PE entry point, TLS, Steam imports,
// save, or game assets are executed.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <wots4/mod_api.h>

namespace mapped_exe {

constexpr uintptr_t preferredBase = 0x400000;
inline uintptr_t imageBase;
inline uintptr_t va(uintptr_t preferred) { return imageBase + preferred - preferredBase; }

inline int passed, failed;
inline void check(bool condition, const char* name) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    condition ? ++passed : ++failed;
}

inline bool applyRelocations(unsigned char* base, const IMAGE_NT_HEADERS32& headers) {
    const IMAGE_DATA_DIRECTORY relocations = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const uint32_t delta = static_cast<uint32_t>(imageBase - preferredBase);
    for (uint32_t position = relocations.VirtualAddress; position < relocations.VirtualAddress + relocations.Size;) {
        auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(base + position);
        if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) ||
            position + block->SizeOfBlock > headers.OptionalHeader.SizeOfImage)
            return false;
        const auto* entries = reinterpret_cast<const WORD*>(block + 1);
        const uint32_t entryCount = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        for (uint32_t index = 0; index < entryCount; ++index) {
            const uint32_t type = entries[index] >> 12;
            if (type == IMAGE_REL_BASED_HIGHLOW)
                *reinterpret_cast<uint32_t*>(base + block->VirtualAddress + (entries[index] & 0xfff)) += delta;
            else if (type != IMAGE_REL_BASED_ABSOLUTE)
                return false;
        }
        position += block->SizeOfBlock;
    }
    return true;
}

inline bool mapImage(const char* path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() < 4096) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        static_cast<size_t>(dos->e_lfanew) > bytes.size() - sizeof(IMAGE_NT_HEADERS32))
        return false;
    const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE || headers->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        headers->OptionalHeader.ImageBase != preferredBase)
        return false;
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, headers->OptionalHeader.SizeOfImage,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!base) return false;
    imageBase = reinterpret_cast<uintptr_t>(base);
    std::memcpy(base, bytes.data(), headers->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(headers);
    for (uint32_t index = 0; index < headers->FileHeader.NumberOfSections; ++index) {
        const IMAGE_SECTION_HEADER& section = sections[index];
        if (static_cast<size_t>(section.PointerToRawData) + section.SizeOfRawData > bytes.size() ||
            static_cast<size_t>(section.VirtualAddress) + section.SizeOfRawData > headers->OptionalHeader.SizeOfImage)
            return false;
        std::memcpy(base + section.VirtualAddress, bytes.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    return applyRelocations(base, *headers);
}

// ---------------------------------------------------------------- fake ModLoader

struct Claim {
    uintptr_t address;
    uint32_t length;
};
struct RecordedPage {
    std::wstring title;
    std::vector<wots4::OptionItem> items;
    wots4::OptionsPageCallbacks callbacks;
};

inline std::vector<Claim> claims;
inline std::vector<RecordedPage> pages;
inline std::string logged;
inline wots4::GameEventCallback optionsSavedCallback;
inline void* optionsSavedContext;

inline void WINAPI hostLog(HMODULE, const char* message) {
    logged += message;
    logged += "\n";
    std::printf("  log: %s\n", message);
}
inline BOOL WINAPI hostClaim(HMODULE, uintptr_t address, uint32_t length) {
    claims.push_back({address, length});
    return TRUE;
}
inline void WINAPI hostReleaseClaims(HMODULE) { claims.clear(); }
inline BOOL WINAPI hostAddOptionsPage(HMODULE, const wchar_t* title, const wots4::OptionItem* items, uint32_t count,
    const wots4::OptionsPageCallbacks* callbacks) {
    pages.push_back({title, std::vector<wots4::OptionItem>(items, items + count), *callbacks});
    return TRUE;
}
inline BOOL WINAPI hostOnFrame(HMODULE, wots4::GameEventCallback, void*) { return TRUE; }
inline BOOL WINAPI hostOnOptionsSaved(HMODULE, wots4::GameEventCallback callback, void* context) {
    optionsSavedCallback = callback;
    optionsSavedContext = context;
    return TRUE;
}

inline const wots4::HostApi host{sizeof(wots4::HostApi), wots4::modApiVersion, hostLog, hostClaim,
    hostReleaseClaims, hostAddOptionsPage, hostOnFrame, hostOnOptionsSaved};

inline bool claimed(uintptr_t preferred, uint32_t length) {
    for (const Claim& claim : claims)
        if (claim.address == va(preferred) && claim.length == length) return true;
    return false;
}
inline bool loggedText(const char* text) { return logged.find(text) != std::string::npos; }

}  // namespace mapped_exe
