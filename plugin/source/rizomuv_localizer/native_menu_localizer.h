#pragma once

#include <windows.h>

#include <string>

namespace rizomuv::localizer {

class TranslationDictionary;

size_t TranslateNativeMenus(DWORD processId, const TranslationDictionary& dictionary);

} // namespace rizomuv::localizer
