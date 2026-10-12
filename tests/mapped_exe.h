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
inline BOOL WINAPI hostAddOptionsPage(HMODULE, const wots4::LocalizedText* title, const wots4::OptionItem* items,
    uint32_t count, const wots4::OptionsPageCallbacks* callbacks) {
    pages.push_back({title->english, std::vector<wots4::OptionItem>(items, items + count), *callbacks});
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
inline bool claimedAt(uintptr_t preferred) {
    for (const Claim& claim : claims)
        if (claim.address == va(preferred)) return true;
    return false;
}
inline bool loggedText(const char* text) { return logged.find(text) != std::string::npos; }

// The options of every installed tweak share one page, and each ModInitialize adds a new one;
// the tests look up their own item on the latest page by its English label.
constexpr uint32_t noItem = ~0u;
inline uint32_t optionIndex(const wchar_t* englishLabel) {
    if (pages.empty()) return noItem;
    const std::vector<wots4::OptionItem>& items = pages.back().items;
    for (uint32_t index = 0; index < items.size(); ++index)
        if (std::wstring(items[index].label.english) == englishLabel) return index;
    return noItem;
}
inline const wots4::OptionItem& optionItem(uint32_t index) { return pages.back().items[index]; }
inline uint32_t optionValue(uint32_t index) {
    const wots4::OptionsPageCallbacks& callbacks = pages.back().callbacks;
    std::vector<uint32_t> values(pages.back().items.size());
    callbacks.read(callbacks.context, values.data(), static_cast<uint32_t>(values.size()));
    return values[index];
}
inline void changeOption(uint32_t index, uint32_t value) {
    const wots4::OptionsPageCallbacks& callbacks = pages.back().callbacks;
    callbacks.changed(callbacks.context, index, value);
}

// A folder under %TEMP% for the Mod's config.ini, so the tests write nothing elsewhere.
inline std::wstring testDirectory;
inline bool createTestDirectory(const wchar_t* testName) {
    wchar_t temporary[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temporary)) return false;
    testDirectory = std::wstring(temporary) + testName + L"_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    return CreateDirectoryW(testDirectory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
inline std::wstring configPath() { return testDirectory + L"config.ini"; }
inline void removeTestDirectory() {
    DeleteFileW(configPath().c_str());
    RemoveDirectoryW(testDirectory.c_str());
}

// Initializes mod.dll as the ModLoader would, on the mapped copy. Returns false if it is not a Mod.
inline wots4::Initialize modInitialize;
inline HMODULE modModule;
inline bool loadMod(const char* path) {
    modModule = LoadLibraryA(path);
    modInitialize = modModule ? reinterpret_cast<wots4::Initialize>(GetProcAddress(modModule, "Wots4ModInitialize"))
                              : nullptr;
    return modInitialize != nullptr;
}
inline bool initializeMod() {
    const wots4::ModContext context{sizeof(context), wots4::modApiVersion, nullptr, modModule,
        reinterpret_cast<HMODULE>(imageBase), testDirectory.c_str(), &host};
    return modInitialize(&context) == TRUE;
}

inline uint8_t& codeByte(uintptr_t preferred) { return *reinterpret_cast<uint8_t*>(va(preferred)); }
constexpr uint8_t jumpOpcode = 0xe9;  // What MinHook writes over a hooked instruction
constexpr uint8_t returnOpcode = 0xc3;

inline int finish(const char* testName) {
    removeTestDirectory();
    std::printf("%s: %d checks passed, %d failed. Mapped exe copy in this process only, config.ini only under "
                "%%TEMP%%; no game installation, saves, or game process were written.\n",
        testName, passed, failed);
    return failed ? 1 : 0;
}

}  // namespace mapped_exe
