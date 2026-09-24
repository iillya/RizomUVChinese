#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../gdi_iat_hooks.cpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <fstream>
#include <shellapi.h>
#include <chrono>
#include "rizomuv_localizer/command_line.h"

namespace {
BOOL WINAPI Extent(HDC, LPCWSTR, int count, int, LPINT fit, LPINT dx, LPSIZE size) {
    assert(count == 3);
    if (fit) *fit = 2;
    if (dx) for (int i = 0; i < count; ++i) dx[i] = i;
    size->cx = 3;
    return TRUE;
}
BOOL WINAPI Glyphs(HDC, int, int, UINT flags, const RECT*, LPCWSTR text, UINT count, const INT*) {
    assert(flags & ETO_GLYPH_INDEX);
    assert(count == 3 && text[0] == L'A');
    return TRUE;
}
int WINAPI MutableBasic(HDC, LPCWSTR text, int count, LPRECT, UINT flags) {
    assert((flags & DT_MODIFYSTRING) && count == 3);
    const_cast<LPWSTR>(text)[0] = L'Z';
    return 1;
}
int WINAPI MutableText(HDC, LPWSTR text, int count, LPRECT, UINT flags, LPDRAWTEXTPARAMS) {
    assert(flags & DT_MODIFYSTRING);
    assert(count == 3);
    text[0] = L'Z';
    return 1;
}
}
void TestDictionaryAndArguments() {
    using namespace rizomuv::localizer;
    const auto path = std::filesystem::temp_directory_path() /
        (L"rizomuv-dictionary-test-" + std::to_wstring(GetCurrentProcessId()) + L".json");
    TranslationDictionary dictionary;
    std::wstring error;
    auto load = [&](const std::string& json) {
        { std::ofstream file(path, std::ios::binary); file << json; }
        return dictionary.Load(path, error);
    };
    const std::string valid = R"({"meta":[true,false,null,-1.25e+2,{"x":"y"}], "translations":{"ABC":"更长的中文测试","Blank":"","Emoji":"\ud83d\ude00"}})";
    assert(load(valid));
    assert(dictionary.Size() == 2 && dictionary.Find(L"ABC"));
    for (const auto* json : {
        R"({"translations":{"A":"甲","A":"乙"}})",
        R"({"translations":{"A":"甲",}})",
        R"({"translations":{"A":"甲"},})",
        R"({"translations":{"A":"甲"},"translations":{"B":"乙"}})",
        R"({"translations":{"A":"甲"},"meta":[true,]})",
        R"({"translations":{"A":"甲"},"meta":01})",
        R"({"translations":{"A":"甲"},"meta":falsehood})",
        R"({"translations":{"A":"\ud800"}})",
        R"({"translations":{"A":"\udc00"}})",
        R"({"translations":{"A":"甲"}} trailing)",
        R"({"translations":{"A":42}})",
        R"({"translations":{}})"}) {
        assert(!load(json) && !error.empty());
        assert(dictionary.Find(L"ABC")); // Failed replacement is transactional.
    }
    assert(!load(std::string("\xff", 1)));
    std::filesystem::remove(path);
    const std::vector<std::wstring> args = {
        L"", L"a b", L"C:\\folder name\\", L"quoted \"text\"", L"中文参数", L"back\\\"slash"};
    std::wstring command = L"launcher.exe";
    for (const auto& arg : args) command += L" " + QuoteArgument(arg);
    int count = 0;
    auto parsed = CommandLineToArgvW(command.c_str(), &count);
    assert(parsed && count == static_cast<int>(args.size() + 1));
    for (size_t i = 0; i < args.size(); ++i) assert(args[i] == parsed[i + 1]);
    LocalFree(parsed);
    g_dictionary = &dictionary;
    const auto first = Translate(L"ABC", 3);
    assert(first.length == 7 && std::wstring(first.text, first.length) == L"更长的中文测试");
    Translate(L"not present", 11);
    assert(std::wstring(first.text, first.length) == L"更长的中文测试");
    assert(Translate(L"C:\\ABC", 6).text != first.text);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) { Translate(L"ABC", 3); Translate(L"Unknown UI label", 16); }
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Lookup benchmark (100000 hits + 100000 misses): " << micros << " us\n";
    assert(Translate(L"Unknown", -1).length == -1);
    g_getTextExtentExPointW = Extent;
    g_extTextOutW = Glyphs;
    g_drawTextExW = MutableText;
    g_drawTextW = MutableBasic;
    int fit = 0, dx[] = {-1, -1, -1, 123456};
    SIZE size{};
    assert(HookGetTextExtentExPointW(nullptr, L"ABC", 3, 10, &fit, dx, &size));
    assert(fit == 2 && dx[3] == 123456);
    assert(HookExtTextOutW(nullptr, 0, 0, ETO_GLYPH_INDEX, nullptr, L"ABC", 3, nullptr));
    wchar_t mutableText[8] = L"ABC";
    assert(HookDrawTextExW(nullptr, mutableText, 3, nullptr, DT_MODIFYSTRING, nullptr));
    assert(mutableText[0] == L'Z');
    mutableText[0] = L'A';
    assert(HookDrawTextW(nullptr, mutableText, 3, nullptr, DT_MODIFYSTRING));
    assert(mutableText[0] == L'Z');

    g_dictionary = nullptr;
}
int main(int argc, char** argv) {
    TestDictionaryAndArguments();
    using namespace rizomuv::localizer;
    if (argc > 1) {
        TranslationDictionary official;
        std::wstring error;
        assert(official.Load(std::filesystem::path(argv[1]), error));
        std::cout << "Official dictionary entries: " << official.Size() << "\n";
    }
    std::vector<unsigned char> image(4096);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + 128);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {512, 40};
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image.data() + 512);
    descriptor->Name = 900; descriptor->FirstThunk = 1024;
    // No OriginalFirstThunk: address-based scanning must still work.
    auto slots = reinterpret_cast<uintptr_t*>(image.data() + 1024);
    slots[0] = 0x123456; slots[1] = 0x789abc;
    auto visit = [](void** slot) { return reinterpret_cast<uintptr_t>(*slot) == 0x123456; };
    assert(VisitImportSlots(image.data(), image.size(), visit) == 1);
    // The DLL alias and import-by-name/ordinal metadata are intentionally unused.
    std::memcpy(image.data() + 900, "api-ms-win-test.dll", 19);
    assert(VisitImportSlots(image.data(), image.size(), visit) == 1);
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT] = {600, 64};
    auto delay = reinterpret_cast<DWORD*>(image.data() + 600);
    delay[0] = 1; delay[1] = 900; delay[3] = 1104;
    auto delayed = reinterpret_cast<uintptr_t*>(image.data() + 1104);
    delayed[0] = 0x777777; // Unresolved delay stub.
    assert(VisitImportSlots(image.data(), image.size(), visit) == 1);
    delayed[0] = 0x123456;
    assert(VisitImportSlots(image.data(), image.size(), visit) == 2);
    descriptor->FirstThunk = 4092; // Misaligned/out-of-bounds thunk is ignored.
    assert(VisitImportSlots(image.data(), image.size(), visit) == 1);
    assert(VisitImportSlots(image.data(), 20, visit) == 0);
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    assert(VisitImportSlots(image.data(), image.size(), visit) == 0);

    std::cout << "PASS: dictionary validation, argument escaping, stable lookup, import compatibility and GDI output guards\n";
}
