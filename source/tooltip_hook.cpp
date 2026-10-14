#include "rizomuv_localizer/tooltip_hook.h"
#include "rizomuv_localizer/tooltip_html.h"

#include <bcrypt.h>
#include <psapi.h>

#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

namespace rizomuv::localizer {
namespace {
// Verified from the installed 2027.0.598 x64 executable, not from addresses
// of a running process. A different build retains the legacy GDI fallback.
constexpr DWORD kTimestamp = 0x6ac63dfe;
constexpr DWORD kImageSize = 0x08217000;
constexpr size_t kVtable = 0x06dc02a8;
constexpr size_t kSetPage = 0x068bc980;
constexpr size_t kCopyString = 0x059ca410;
constexpr size_t kDestroyString = 0x059d00e0;
constexpr size_t kFreeImport = 0x06d39c20;
constexpr size_t kTypeName = 0x07f21408;
constexpr size_t kSetPageSlot = 233;
constexpr char kType[] = ".?AVCToolTipHtmlWindow@?A0x22fc67be@@";
constexpr char kSha256[] = "58c4ed2ff72bf989e819fe2db4a5f0ac6304eb880956f5f6f56ff9fbaf24a407";

// This ABI is used only after the entire host file and its vtable/code have
// passed the exact-build check. The original const object is never altered.
// Host copy constructor/destroy/free keep all host allocations on its CRT.
struct alignas(8) HostWxString {
    std::array<std::byte, 32> wide;
    void* conversionBuffer{};
    size_t conversionCapacity{};
};
static_assert(sizeof(HostWxString) == 48);
struct HostWideStorage {
    union { wchar_t inlineText[8]; const wchar_t* pointer; } data{};
    size_t length = 0;
    size_t capacity = 7;
};
static_assert(sizeof(HostWideStorage) == 32);

HostWideStorage HostStringInput(std::wstring_view text) noexcept {
    HostWideStorage input;
    input.length = text.size();
    if (text.size() < 8) std::memcpy(input.data.inlineText, text.data(), text.size() * sizeof(wchar_t));
    else { input.data.pointer = text.data(); input.capacity = text.size(); }
    return input;
}
using SetPageFn = bool (*)(void*, const void*);
using CopyStringFn = void* (*)(void*, const void*);
using DestroyStringFn = void (*)(void*);
using FreeFn = void (*)(void*);
SetPageFn g_setPage = nullptr;
CopyStringFn g_copyString = nullptr;
DestroyStringFn g_destroyString = nullptr;
FreeFn g_hostFree = nullptr;
TooltipHtmlTranslator g_tooltipTranslator;
std::mutex g_installMutex;

bool GuardedCopy(void* destination, const void* source, size_t count) noexcept {
    __try { std::memcpy(destination, source, count); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadWxString(const void* source, std::wstring& result) {
    HostWideStorage storage;
    if (!source || !GuardedCopy(&storage, source, sizeof(storage))) return false;
    if (storage.length > kMaxTooltipHtmlLength || storage.capacity < storage.length ||
        storage.capacity > (1ULL << 40)) return false;
    const wchar_t* text = static_cast<const wchar_t*>(source);
    if (storage.capacity >= 8) text = storage.data.pointer;
    else if (storage.capacity != 7 || storage.length > 7) return false;
    result.resize(storage.length);
    if (storage.length && !GuardedCopy(result.data(), text, storage.length * sizeof(wchar_t))) return false;
    return result.find(L'\0') == result.npos;
}

bool HookTooltipSetPage(void* window, const void* source) {
    std::wstring text, translated;
    bool prepared = false;
    try {
        prepared = ReadWxString(source, text) && g_tooltipTranslator.Translate(text, translated);
    } catch (...) { prepared = false; }
    if (!prepared) return g_setPage(window, source);

    HostWxString local{};
    try {
        // Describe the host ABI explicitly: our own Debug/Release STL layout
        // never crosses the boundary. The host owns the copied allocation.
        const auto input = HostStringInput(translated);
        g_copyString(&local, &input);
    } catch (...) { return g_setPage(window, source); }
    struct Cleanup {
        HostWxString& value;
        ~Cleanup() {
            if (value.conversionBuffer) g_hostFree(value.conversionBuffer);
            g_destroyString(&value);
        }
    } cleanup{local};
    // Do not catch/retry the original method: it may already have changed the
    // page. Cleanup still runs if the original reports failure or throws.
    return g_setPage(window, &local);
}

bool HashMatches(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    struct Close {
        BCRYPT_ALG_HANDLE& algorithm;
        BCRYPT_HASH_HANDLE& hash;
        ~Close() {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } close{algorithm, hash};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    DWORD length = 0, used = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
                         sizeof(length), &used, 0) < 0 || length > 65536) return false;
    object.resize(length);
    if (BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) < 0) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::array<unsigned char, 65536> block{};
    while (file.read(reinterpret_cast<char*>(block.data()), block.size()) || file.gcount()) {
        if (BCryptHashData(hash, block.data(), static_cast<ULONG>(file.gcount()), 0) < 0) return false;
    }
    if (!file.eof()) return false;
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return false;
    const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < digest.size(); ++i)
        if (hex[digest[i] >> 4] != kSha256[i * 2] || hex[digest[i] & 15] != kSha256[i * 2 + 1]) return false;
    return true;
}

bool ValidateMappedProfile(const unsigned char* base, size_t size) noexcept {
    if (!base || size != kImageSize) return false;
    IMAGE_DOS_HEADER dos{};
    if (!GuardedCopy(&dos, base, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || static_cast<size_t>(dos.e_lfanew) > size - sizeof(IMAGE_NT_HEADERS64)) return false;
    IMAGE_NT_HEADERS64 nt{};
    if (!GuardedCopy(&nt, base + dos.e_lfanew, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.FileHeader.TimeDateStamp != kTimestamp ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage != kImageSize)
        return false;
    char name[sizeof(kType)]{};
    if (!GuardedCopy(name, base + kTypeName, sizeof(name)) || std::memcmp(name, kType, sizeof(name))) return false;
    const struct { size_t rva; std::array<unsigned char, 8> bytes; } signatures[] = {
        {kSetPage, {0x48,0x8d,0x81,0x38,0x03,0x00,0x00,0x45}},
        {kCopyString, {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b}},
        {kDestroyString, {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b}},
    };
    for (const auto& signature : signatures) {
        std::array<unsigned char, 8> bytes{};
        if (!GuardedCopy(bytes.data(), base + signature.rva, bytes.size()) || bytes != signature.bytes) return false;
    }
    const void* current = nullptr;
    if (!GuardedCopy(&current, base + kVtable + kSetPageSlot * sizeof(void*), sizeof(current)) ||
        current != base + kSetPage) return false;
    return true;
}
} // namespace

bool InstallTooltipHtmlHook(HMODULE host, const TranslationDictionary& dictionary, std::wstring& status) {
    std::lock_guard<std::mutex> lock(g_installMutex);
    if (g_setPage) { status = L"HTML 提示入口已接入"; return true; }
    MODULEINFO info{};
    if (!host || !GetModuleInformation(GetCurrentProcess(), host, &info, sizeof(info)) ||
        !ValidateMappedProfile(static_cast<const unsigned char*>(info.lpBaseOfDll), info.SizeOfImage)) {
        status = L"未匹配已验证的 HTML 提示配置，保留 GDI 翻译";
        return false;
    }
    wchar_t path[32768]{};
    const DWORD count = GetModuleFileNameW(host, path, 32768);
    if (!count || count >= 32768 || !HashMatches(path)) {
        status = L"HTML 提示宿主完整性不匹配，未修改提示入口";
        return false;
    }
    auto* base = static_cast<unsigned char*>(info.lpBaseOfDll);
    FreeFn hostFree = nullptr;
    if (!GuardedCopy(&hostFree, base + kFreeImport, sizeof(hostFree)) || !hostFree) {
        status = L"无法确认宿主字符串清理入口，未修改提示入口"; return false;
    }
    g_tooltipTranslator.Initialize(dictionary);
    g_copyString = reinterpret_cast<CopyStringFn>(base + kCopyString);
    g_destroyString = reinterpret_cast<DestroyStringFn>(base + kDestroyString);
    g_hostFree = hostFree;
    auto* slot = reinterpret_cast<void**>(base + kVtable + kSetPageSlot * sizeof(void*));
    const auto original = reinterpret_cast<SetPageFn>(base + kSetPage);
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) {
        status = L"HTML 提示入口无法写入，保留原入口"; return false;
    }
    g_setPage = original;
    const bool installed = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(HookTooltipSetPage),
                                                             reinterpret_cast<void*>(original)) == reinterpret_cast<void*>(original);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), protection, &ignored);
    if (!installed) g_setPage = nullptr;
    status = installed ? L"HTML 提示整段翻译已接入（2027.0.598），在拆词和排版之前替换正文"
                       : L"HTML 提示入口已被其他模块修改，未覆盖";
    return installed;
}
} // namespace rizomuv::localizer
