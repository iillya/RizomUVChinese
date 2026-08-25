#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace rizomuv::localizer {

class TranslationDictionary;

bool InstallGdiIatHooks(HMODULE targetModule, const TranslationDictionary* dictionary,
                        std::wstring& error);
bool StartMissingTextCapture(const std::filesystem::path& outputDirectory,
                             std::wstring& error);
unsigned long long GetGdiTranslationHitCount();
size_t GetMissingTextCount();

} // namespace rizomuv::localizer
