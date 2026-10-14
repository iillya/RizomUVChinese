#pragma once

#include <windows.h>
#include <string>

namespace rizomuv::localizer {
class TranslationDictionary;

// Exact-build profile: fail closed rather than guess a foreign wxString ABI.
bool InstallTooltipHtmlHook(HMODULE host, const TranslationDictionary& dictionary,
                            std::wstring& status);
} // namespace rizomuv::localizer
