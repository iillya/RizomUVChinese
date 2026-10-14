#include "../gdi_iat_hooks.cpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include "rizomuv_localizer/tooltip_html.h"

// Run this executable on the same corpus before/after a change. These are
// lookup costs, not a renderer/UV algorithm or total application benchmark.
int main(int argc, char** argv) {
    using namespace rizomuv::localizer;
    if (argc != 3) return 2;
    TranslationDictionary dictionary;
    std::wstring error;
    const auto start = std::chrono::steady_clock::now();
    if (!dictionary.Load(argv[1], error)) return 3;
    std::cout << "load_us=" << std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count() << '\n';
    g_dictionary = &dictionary;
    std::ifstream file(argv[2], std::ios::binary);
    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!file || count > 100000) return 4;
    std::vector<std::wstring> shortLabels, longLabels, misses, all;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t length = 0;
        file.read(reinterpret_cast<char*>(&length), sizeof(length));
        if (!file || length > 65535) return 5;
        std::wstring key(length, L'\0');
        file.read(reinterpret_cast<char*>(key.data()), length * sizeof(wchar_t));
        if (!file) return 6;
        if (key.size() <= 40) shortLabels.push_back(key);
        if (key.size() >= 200) longLabels.push_back(key);
        misses.push_back(L"Missing UI: " + key);
        all.push_back(std::move(key));
    }
    // Every corpus entry must remain semantically identical to dictionary
    // lookup and the display-layer path/CJK exclusion policy.
    for (const auto& key : all) {
        const auto result = Translate(key.data(), static_cast<int>(key.size()));
        const auto* expected = ShouldLookupTranslation(key.data(), static_cast<int>(key.size()))
            ? dictionary.Find(key) : nullptr;
        const std::wstring_view wanted = expected ? std::wstring_view(*expected) : std::wstring_view(key);
        if (std::wstring_view(result.text, result.length) != wanted) return 7;
    }
    FinishGdiStartupDiagnostics(); // Measure steady state, after startup logging.
    auto run = [](const char* name, const std::vector<std::wstring>& corpus, int workers) {
        if (corpus.empty()) return;
        constexpr int repeats = 60;
        std::vector<long long> samples;
        for (int round = 0; round < 7; ++round) {
            std::atomic<size_t> checksum{0};
            auto work = [&] {
                size_t local = 0;
                for (int pass = 0; pass < repeats; ++pass)
                    for (const auto& key : corpus) {
                        const auto value = Translate(key.data(), static_cast<int>(key.size()));
                        local += static_cast<size_t>(value.length);
                    }
                checksum.fetch_add(local, std::memory_order_relaxed);
            };
            const auto begin = std::chrono::steady_clock::now();
            if (workers == 1) work();
            else {
                std::vector<std::thread> threads;
                for (int i = 0; i < workers; ++i) threads.emplace_back(work);
                for (auto& thread : threads) thread.join();
            }
            samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin).count());
            if (!checksum.load()) std::abort();
        }
        std::sort(samples.begin(), samples.end());
        std::cout << name << " labels=" << corpus.size() << " workers=" << workers
            << " median_ns_per_lookup=" << samples[3] / (corpus.size() * repeats * workers) << '\n';
    };
    run("short_hit", shortLabels, 1);
    run("long_hit", longLabels, 1);
    run("mixed_miss", misses, 1);
    run("mixed_hit_4_threads", all, 4);

    // Include the GDI device filter, which the pure lookup benchmark omits.
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 8;
    const std::wstring displayMiss = L"An unlisted interface caption";
    const std::wstring displayChinese = L"已经翻译的界面标题";
    auto display = [&](const char* name, const std::wstring& text) {
        std::vector<long long> timings;
        for (int sample = 0; sample < 7; ++sample) {
            const auto begin = std::chrono::steady_clock::now();
            size_t checksum = 0;
            for (int i = 0; i < 100000; ++i)
                checksum += TranslateForDisplay(dc, text.data(), static_cast<int>(text.size())).length;
            if (!checksum) std::abort();
            timings.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin).count());
        }
        std::sort(timings.begin(), timings.end());
        std::cout << name << " median_ns_per_call=" << timings[3] / 100000 << '\n';
    };
    display("display_miss", displayMiss);
    display("display_chinese", displayChinese);
    DeleteDC(dc);

    TooltipHtmlTranslator tooltip;
    tooltip.Initialize(dictionary);
    const std::wstring html = L"<table><tr><td><b><i>Edge</i></b></td><td>F2</td></tr></table>"
                              L"<p>Lets you select and work at edge level</p>";
    std::wstring output;
    std::vector<long long> timings;
    for (int sample = 0; sample < 7; ++sample) {
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i)
            if (!tooltip.Translate(html, output)) return 9;
        timings.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - begin).count());
    }
    std::sort(timings.begin(), timings.end());
    std::cout << "tooltip_repeat median_ns_per_call=" << timings[3] / 10000 << '\n';
    g_dictionary = nullptr;
}
