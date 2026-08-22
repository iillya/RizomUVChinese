#pragma once

#include <windows.h>

#include <string>

namespace rizomuv::localizer {

class TranslationDictionary;

bool InstallGdiIatHooks(HMODULE targetModule, const TranslationDictionary* dictionary,
                        std::wstring& error);
unsigned long long GetGdiTranslationHitCount();

} // namespace rizomuv::localizer
