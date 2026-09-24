#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rizomuv::localizer {

// Heterogeneous lookup avoids allocating/copying the caller's UI text.
struct TranslationHash {
    using is_transparent = void;
    size_t operator()(std::wstring_view value) const noexcept {
        return std::hash<std::wstring_view>{}(value);
    }
};
using TranslationMap = std::unordered_map<std::wstring, std::wstring,
    TranslationHash, std::equal_to<>>;

class TranslationDictionary {
public:
    bool Load(const std::filesystem::path& path, std::wstring& error);
    const std::wstring* Find(std::wstring_view source) const noexcept;
    size_t Size() const noexcept { return translations_.size(); }

private:
    TranslationMap translations_;
};

} // namespace rizomuv::localizer
