#include "rizomuv_localizer/tooltip_html.h"

#include <unordered_set>
#include <vector>
#include <atomic>
#include <cstdint>

namespace rizomuv::localizer {
namespace {
std::atomic<unsigned long long> g_translatorRevision{0};
bool Space(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xa0;
}

std::wstring Normalize(std::wstring_view input) {
    std::wstring result;
    bool space = false;
    for (wchar_t c : input) {
        if (Space(c)) { space = !result.empty(); continue; }
        if (space) result += L' ';
        result += c;
        space = false;
    }
    return result;
}

bool Decode(std::wstring_view input, std::wstring& output) {
    output.clear();
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != L'&') { output += input[i]; continue; }
        const size_t end = input.find(L';', i + 1);
        if (end == input.npos || end - i > 16) return false;
        const auto name = input.substr(i + 1, end - i - 1);
        wchar_t c = 0;
        if (name == L"amp") c = L'&';
        else if (name == L"lt") c = L'<';
        else if (name == L"gt") c = L'>';
        else if (name == L"quot") c = L'"';
        else if (name == L"apos") c = L'\'';
        else if (name == L"nbsp") c = 0xa0;
        else if (!name.empty() && name[0] == L'#') {
            size_t p = 1;
            unsigned base = 10, value = 0;
            if (p < name.size() && (name[p] == L'x' || name[p] == L'X')) { base = 16; ++p; }
            if (p == name.size()) return false;
            for (; p < name.size(); ++p) {
                const wchar_t digit = name[p];
                unsigned v = 99;
                if (digit >= L'0' && digit <= L'9') v = digit - L'0';
                else if (digit >= L'a' && digit <= L'f') v = digit - L'a' + 10;
                else if (digit >= L'A' && digit <= L'F') v = digit - L'A' + 10;
                if (v >= base || value > (0x10ffff - v) / base) return false;
                value = value * base + v;
            }
            if (!value || (value >= 0xd800 && value <= 0xdfff)) return false;
            if (value > 0xffff) {
                value -= 0x10000;
                output += static_cast<wchar_t>(0xd800 + (value >> 10));
                output += static_cast<wchar_t>(0xdc00 + (value & 0x3ff));
                i = end;
                continue;
            }
            c = static_cast<wchar_t>(value);
        } else return false;
        output += c;
        i = end;
    }
    return true;
}

std::wstring Escape(std::wstring_view input) {
    std::wstring output;
    for (wchar_t c : input) {
        switch (c) {
        case L'&': output += L"&amp;"; break;
        case L'<': output += L"&lt;"; break;
        case L'>': output += L"&gt;"; break;
        case L'"': output += L"&quot;"; break;
        case L'\n': output += L"<br>"; break;
        case L'\r': break;
        default: output += c;
        }
    }
    return output;
}

struct Token { size_t start, end; bool text; std::wstring tag; bool closing; };

bool Tokenize(std::wstring_view html, std::vector<Token>& tokens) {
    size_t p = 0;
    while (p < html.size()) {
        const size_t begin = p;
        if (html[p] != L'<') {
            p = html.find(L'<', p);
            if (p == html.npos) p = html.size();
            tokens.push_back({begin, p, true, {}, false});
            continue;
        }
        if (html.substr(p, 4) == L"<!--") {
            const size_t end = html.find(L"-->", p + 4);
            if (end == html.npos) return false;
            p = end + 3;
            tokens.push_back({begin, p, false, L"!", false});
            continue;
        }
        wchar_t quote = 0;
        for (++p; p < html.size(); ++p) {
            if (quote) { if (html[p] == quote) quote = 0; }
            else if (html[p] == L'\'' || html[p] == L'"') quote = html[p];
            else if (html[p] == L'>') break;
        }
        if (p == html.size()) return false;
        ++p;
        size_t n = begin + 1;
        while (n < p && Space(html[n])) ++n;
        const bool closing = n < p && html[n] == L'/';
        if (closing) ++n;
        std::wstring name;
        while (n < p) {
            wchar_t c = html[n++];
            if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
            if ((c < L'a' || c > L'z') && (c < L'0' || c > L'9')) break;
            name += c;
        }
        tokens.push_back({begin, p, false, std::move(name), closing});
    }
    return true;
}

bool Inline(const Token& t) {
    // Keep formatting wrappers and links verbatim. Other tags delimit blocks.
    for (auto name : {L"b", L"i", L"u", L"em", L"strong", L"font", L"span", L"a", L"small", L"big"})
        if (t.tag == name) return true;
    return false;
}
} // namespace

void TooltipHtmlTranslator::Initialize(const TranslationDictionary& dictionary) {
    decltype(normalized_) result;
    result.reserve(dictionary.Size());
    std::unordered_set<std::wstring> conflicts;
    for (const auto& [source, value] : dictionary.Entries()) {
        if (source.find(L'$') != source.npos || source.find(L'\0') != source.npos) continue;
        auto normalized = Normalize(source);
        if (normalized.empty() || conflicts.contains(normalized)) continue;
        auto [it, inserted] = result.emplace(normalized, value);
        if (!inserted && Normalize(it->second) != Normalize(value)) {
            result.erase(it);
            conflicts.insert(std::move(normalized));
        }
    }
    normalized_ = std::move(result);
    revision_ = g_translatorRevision.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool TooltipHtmlTranslator::Translate(std::wstring_view html, std::wstring& output) const {
    // A popup's width/height passes call SetPage repeatedly with identical HTML.
    // Keep only one bounded entry per thread, and invalidate on reinitialization.
    struct Cache {
        unsigned long long revision = 0;
        std::wstring input, output;
        bool changed = false;
    };
    thread_local Cache cache;
    constexpr size_t cacheLimit = 16384;
    if (html.size() <= cacheLimit && cache.revision == revision_ && cache.input == html) {
        output = cache.output;
        return cache.changed;
    }
    // Support callers passing a view of their previous output buffer.
    std::wstring aliasCopy;
    const auto begin = reinterpret_cast<uintptr_t>(output.data());
    const auto end = begin + output.size() * sizeof(wchar_t);
    const auto source = reinterpret_cast<uintptr_t>(html.data());
    if (source >= begin && source < end) { aliasCopy.assign(html); html = aliasCopy; }
    const bool changed = TranslateUncached(html, output);
    if (html.size() <= cacheLimit && output.size() <= cacheLimit) {
        cache.input.assign(html);
        cache.output = output;
        cache.changed = changed;
        cache.revision = revision_;
    }
    return changed;
}

bool TooltipHtmlTranslator::TranslateUncached(std::wstring_view html, std::wstring& output) const {
    output.clear();
    if (html.empty() || html.size() > kMaxTooltipHtmlLength || html.find(L'\0') != html.npos) return false;
    std::vector<Token> tokens;
    if (!Tokenize(html, tokens)) return false;
    std::vector<std::wstring> replacement(tokens.size());
    std::vector<bool> replaced(tokens.size());
    std::vector<size_t> textIndices;
    std::wstring combined, decoded;
    bool changed = false, valid = true, tooLarge = false;
    auto flush = [&] {
        if (valid && !textIndices.empty()) {
            const auto found = normalized_.find(Normalize(combined));
            if (found != normalized_.end()) {
                if (found->second.size() > kMaxTooltipHtmlLength) {
                    tooLarge = true;
                    textIndices.clear(); combined.clear(); valid = true;
                    return;
                }
                // A sentence containing inline formatting may be split across
                // nodes. Translate it as a whole; retain every original tag.
                // There is no reliable bilingual word-to-word style alignment.
                size_t first = textIndices.front();
                for (auto index : textIndices) {
                    const auto text = html.substr(tokens[index].start, tokens[index].end - tokens[index].start);
                    if (text.find_first_not_of(L" \t\r\n") != text.npos) {
                        first = index; break;
                    }
                }
                for (auto index : textIndices) replaced[index] = true;
                size_t leading = 0, trailing = combined.size();
                while (leading < combined.size() && Space(combined[leading])) ++leading;
                while (trailing > leading && Space(combined[trailing - 1])) --trailing;
                replacement[first] = combined.substr(0, leading) + Escape(found->second) + combined.substr(trailing);
                changed = true;
            }
        }
        textIndices.clear(); combined.clear(); valid = true;
    };
    std::wstring opaque;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (!opaque.empty()) {
            if (!token.text && token.closing && token.tag == opaque) opaque.clear();
            continue;
        }
        if (!token.text && (token.tag == L"script" || token.tag == L"style" || token.tag == L"head")) {
            flush();
            if (!token.closing) opaque = token.tag;
            continue;
        }
        if (token.text) {
            textIndices.push_back(i);
            if (!Decode(html.substr(token.start, token.end - token.start), decoded)) valid = false;
            else combined += decoded;
        } else if (!Inline(token)) flush();
    }
    flush();
    if (!changed || tooLarge) return false;
    output.reserve(html.size());
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& t = tokens[i];
        const std::wstring_view part = replaced[i] ? std::wstring_view(replacement[i])
                                                  : html.substr(t.start, t.end - t.start);
        if (part.size() > kMaxTooltipHtmlLength - output.size()) { output.clear(); return false; }
        output += part;
    }
    return output != html;
}
} // namespace rizomuv::localizer
