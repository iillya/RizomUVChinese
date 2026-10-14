#pragma once

#include "translation_dictionary.h"

namespace rizomuv::localizer {

inline constexpr size_t kMaxTooltipHtmlLength = 131072;

// Translate complete HTML text before wxHTML splits it into word cells.
// This object is initialized before installing the tooltip hook, then read-only.
class TooltipHtmlTranslator {
public:
    void Initialize(const TranslationDictionary& dictionary);
    bool Translate(std::wstring_view html, std::wstring& output) const;

private:
    bool TranslateUncached(std::wstring_view html, std::wstring& output) const;
    // The dictionary is immutable and must outlive this translator.
    std::unordered_map<std::wstring, std::wstring_view,
                       TranslationHash, std::equal_to<>> normalized_;
    unsigned long long revision_ = 0;
};

} // namespace rizomuv::localizer
