#pragma once

#include <filesystem>
#include <string>

namespace rizomuv::localizer {

void InitializeRuntimeLog(const std::filesystem::path& runtimeDirectory);
void RuntimeLog(const std::wstring& message);

} // namespace rizomuv::localizer
