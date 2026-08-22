#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace rizomuv::localizer {

class TranslationDictionary {
public:
    bool Load(const std::filesystem::path& path, std::wstring& error);
    const std::wstring* Find(const std::wstring& source) const;
    size_t Size() const noexcept { return translations_.size(); }

private:
    std::unordered_map<std::wstring, std::wstring> translations_;
};

} // namespace rizomuv::localizer
