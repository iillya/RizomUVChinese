#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../tooltip_hook.cpp"

#include <cassert>
#include <iostream>
#include <new>
#include <stdexcept>

using namespace rizomuv::localizer;
namespace {
std::wstring g_received;
int g_calls = 0, g_copies = 0, g_destroys = 0, g_frees = 0;
bool g_throw = false;
void* Copy(void* destination, const void* source) {
    ++g_copies;
    std::wstring text;
    assert(ReadWxString(source, text));
    auto input = HostStringInput(text);
    if (input.length >= 8) {
        auto* owned = new wchar_t[text.size() + 1];
        std::copy(text.begin(), text.end(), owned);
        owned[text.size()] = L'\0';
        input.data.pointer = owned;
    }
    std::memcpy(destination, &input, sizeof(input));
    auto* value = static_cast<HostWxString*>(destination);
    value->conversionBuffer = nullptr;
    value->conversionCapacity = 0;
    return destination;
}
void Destroy(void* value) {
    ++g_destroys;
    HostWideStorage storage;
    std::memcpy(&storage, value, sizeof(storage));
    if (storage.capacity >= 8) delete[] storage.data.pointer;
}
void Free(void* value) { ++g_frees; std::free(value); }
bool SetPage(void*, const void* value) {
    ++g_calls;
    assert(ReadWxString(value, g_received));
    if (g_throw) throw std::runtime_error("original SetPage failed");
    // Exercise a host-side lazily filled conversion cache too.
    auto* cache = const_cast<HostWxString*>(static_cast<const HostWxString*>(value));
    if (g_received.find(L"中文") != g_received.npos) cache->conversionBuffer = std::malloc(16);
    return true;
}
void CheckHtml(TooltipHtmlTranslator& translator, std::wstring_view input, std::wstring_view expected) {
    std::wstring output;
    assert(translator.Translate(input, output));
    assert(output == expected);
}

void TestHtmlAndBridge() {
    const auto path = std::filesystem::temp_directory_path() / L"rizom-tooltip-test.json";
    std::ofstream(path, std::ios::binary) << R"({"translations":{
      "Edge":"边", "Lets you select and work at edge level":"用于在边级别选择和操作",
      "Select & move":"中文 <移动>", "First line":"第一行", "Second line":"第二行",
      "ambiguous phrase":"甲", "ambiguous  phrase":"乙"}})";
    TranslationDictionary dictionary;
    std::wstring error;
    assert(dictionary.Load(path, error));
    std::filesystem::remove(path);
    TooltipHtmlTranslator translator;
    translator.Initialize(dictionary);
    CheckHtml(translator, L"<table><tr><td><i>Edge</i></td><td>F2</td></tr></table><p>Lets you select and work at edge level</p>",
              L"<table><tr><td><i>边</i></td><td>F2</td></tr></table><p>用于在边级别选择和操作</p>");
    CheckHtml(translator, L"<p>Lets you <b>select</b> and work at edge level</p>",
              L"<p>用于在边级别选择和操作<b></b></p>");
    CheckHtml(translator, L"<p>\n Lets\t you select&nbsp;and work at edge level \n</p>",
              L"<p>\n 用于在边级别选择和操作 \n</p>");
    CheckHtml(translator, L"<p> <b>Edge</b> </p>", L"<p><b> 边 </b></p>");
    CheckHtml(translator, L"<p>Select &amp; move</p>", L"<p>中文 &lt;移动&gt;</p>");
    CheckHtml(translator, L"<p>Select &#38; move</p>", L"<p>中文 &lt;移动&gt;</p>");
    CheckHtml(translator, L"<p>First line<br>Second line</p>", L"<p>第一行<br>第二行</p>");
    CheckHtml(translator, L"<a href='Edge' title='Edge > X'>Edge</a>",
              L"<a href='Edge' title='Edge > X'>边</a>");
    std::wstring output;
    for (const auto* unchanged : {L"<p>Unknown text F2 C:\\models\\Edge.obj</p>",
         L"<head><title>Edge</title></head><script>Edge</script><style>Edge</style>",
         L"<p>ambiguous phrase</p>", L"<p>Edge&unknown;</p>", L"<p title='unterminated>Edge"})
        assert(!translator.Translate(unchanged, output));

    g_tooltipTranslator.Initialize(dictionary);
    g_setPage = SetPage; g_copyString = Copy; g_destroyString = Destroy; g_hostFree = Free;
    HostWxString source{};
    const std::wstring html = L"<p>Select &amp; move</p>";
    const auto input = HostStringInput(html);
    Copy(&source, &input);
    std::wstring original;
    assert(ReadWxString(&source, original));
    g_calls = g_copies = g_destroys = g_frees = 0;
    assert(HookTooltipSetPage(nullptr, &source));
    assert(g_received == L"<p>中文 &lt;移动&gt;</p>");
    assert(g_calls == 1 && g_copies == 1 && g_destroys == 1 && g_frees == 1);
    assert(ReadWxString(&source, output) && output == original);
    g_throw = true;
    try { HookTooltipSetPage(nullptr, &source); assert(false); }
    catch (const std::runtime_error&) {}
    assert(g_calls == 2 && g_destroys == 2); // Never retry an original call.
    g_throw = false;
    Destroy(&source);
    assert(!ReadWxString(reinterpret_cast<void*>(1), output));
    HostWxString invalid{};
    assert(!ReadWxString(&invalid, output));
    for (const auto* small : {L"", L"A", L"中文", L"1234567", L"12345678"}) {
        const auto storage = HostStringInput(small);
        assert(ReadWxString(&storage, output) && output == small);
    }
    // Reinitialization cannot reuse an old cached translation.
    translator.Initialize(dictionary);
    CheckHtml(translator, L"<p>Edge</p>", L"<p>边</p>");
    output = L"<p>Edge</p>";
    assert(translator.Translate(output, output) && output == L"<p>边</p>");
    std::ofstream(path, std::ios::binary) << R"({"translations":{"Edge":"新译文","Face \ud83d\ude00":"表情面"}})";
    assert(dictionary.Load(path, error));
    translator.Initialize(dictionary);
    CheckHtml(translator, L"<p>Edge</p>", L"<p>新译文</p>");
    CheckHtml(translator, L"<p>Face &#x1F600;</p>", L"<p>表情面</p>");
    std::ofstream(path, std::ios::binary) << "{\"translations\":{\"Edge\":\"" << std::string(kMaxTooltipHtmlLength + 1, 'x') << "\"}}";
    assert(dictionary.Load(path, error));
    translator.Initialize(dictionary);
    assert(!translator.Translate(L"<p>Edge</p>", output) && output.empty());
    std::filesystem::remove(path);
    g_setPage = nullptr;
}

void TestHostProfile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    assert(file && HashMatches(path));
    const size_t length = static_cast<size_t>(file.tellg());
    std::vector<unsigned char> bytes(length);
    file.seekg(0); assert(file.read(reinterpret_cast<char*>(bytes.data()), length));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    assert(nt->OptionalHeader.SizeOfImage == kImageSize);
    std::vector<unsigned char> image(kImageSize);
    std::memcpy(image.data(), bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        assert(section[i].PointerToRawData + section[i].SizeOfRawData <= bytes.size());
        assert(section[i].VirtualAddress + section[i].SizeOfRawData <= image.size());
        std::memcpy(image.data() + section[i].VirtualAddress,
                    bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    }
    auto* slot = reinterpret_cast<uintptr_t*>(image.data() + kVtable + kSetPageSlot * sizeof(void*));
    assert(*slot == nt->OptionalHeader.ImageBase + kSetPage);
    // Offline relocation of this checked slot; do not execute any host code.
    *slot = reinterpret_cast<uintptr_t>(image.data() + kSetPage);
    assert(ValidateMappedProfile(image.data(), image.size()));
    image[kTypeName] ^= 1;
    assert(!ValidateMappedProfile(image.data(), image.size()));
    image[kTypeName] ^= 1;
    image[kSetPage] ^= 1;
    assert(!ValidateMappedProfile(image.data(), image.size()));
    image[kSetPage] ^= 1;
    *slot = 0;
    assert(!ValidateMappedProfile(image.data(), image.size()));
    assert(!ValidateMappedProfile(image.data(), 32));
    std::cout << "PASS: actual 2027.0.598 file SHA256, tooltip vtable slot, code and ABI profile; mismatches rejected\n";
}
} // namespace

int main(int argc, char** argv) {
    TestHtmlAndBridge();
    if (argc > 1) {
        TranslationDictionary dictionary;
        std::wstring error;
        assert(dictionary.Load(std::filesystem::path(argv[1]), error));
        TooltipHtmlTranslator translator;
        translator.Initialize(dictionary);
        std::wstring output;
        assert(translator.Translate(L"<p>Lets you select and work at edge level</p>", output));
        assert(output.find(L"用于在边级别选择和操作") != output.npos);
        assert(output.find(L"Lets you") == output.npos);
        const std::wstring density = L"Switch to edit mode density multiplier map. Once edited, a value of the density multiplier different from zero will change the UV polygon's area after running optimize. Packing will also take account the density multiplier map by giving island different scales, and hotspot will do as well. With 'Live Optimize' enabled in the Brush panel (the default), each stroke re-optimizes the touched islands as you paint. The viewport shows the density factor while the brush is active, unless 'Show density map' is turned off in the Brush panel.";
        assert(translator.Translate(L"<table><tr><td><i>Density</i></td><td>F8</td></tr></table><p>" + density + L"</p>", output));
        assert(output.find(L"密度系数贴图绘制模式") != output.npos);
        assert(output.find(L"Switch to edit mode") == output.npos);
        assert(output.find(L"每次笔划") != output.npos);
        assert(output.find(L"F8") != output.npos);
        if (argc > 3) {
            TranslationDictionary additions;
            assert(additions.Load(std::filesystem::path(argv[3]), error));
            size_t count = 0;
            for (const auto& [key, value] : additions.Entries()) {
                // HTML whitespace is folded within a paragraph. Keep the
                // original text in one block to check every reviewed mapping.
                std::wstring body;
                for (wchar_t c : key) {
                    if (c == L'\n' || c == L'\r') body += L' ';
                    else if (c == L'&') body += L"&amp;";
                    else if (c == L'<') body += L"&lt;";
                    else if (c == L'>') body += L"&gt;";
                    else if (c == L'"') body += L"&quot;";
                    else body += c;
                }
                if (!translator.Translate(L"<p>" + body + L"</p>", output)) {
                    std::wcerr << L"Tooltip corpus miss: " << key << L"\n";
                    return 2;
                }
                ++count;
            }
            std::cout << "PASS: reviewed tooltip corpus, " << count << " whole-text and HTML fragment mappings\n";
        }
    }
    if (argc > 2) TestHostProfile(std::filesystem::path(argv[2]));
    std::cout << "PASS: HTML tooltip before word splitting, entities, formatting, shortcuts, unknown text, host string lifetime and exception cleanup\n";
}
