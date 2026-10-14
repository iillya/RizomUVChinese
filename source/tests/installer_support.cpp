#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../inno/support.cpp"

#include <cassert>
#include <fstream>
#include <iostream>

namespace {
void FakeHost(const std::filesystem::path& path, WORD machine) {
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = sizeof(dos);
    const DWORD signature = IMAGE_NT_SIGNATURE;
    IMAGE_FILE_HEADER pe{};
    pe.Machine = machine;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&dos), sizeof(dos));
    file.write(reinterpret_cast<const char*>(&signature), sizeof(signature));
    file.write(reinterpret_cast<const char*>(&pe), sizeof(pe));
}
} // namespace

int main() {
    const auto workspace = std::filesystem::canonical(std::filesystem::current_path());
    const auto root = workspace / L"build" / (L"support-boundary-" + std::to_wstring(GetCurrentProcessId()));
    if (std::filesystem::exists(root)) return 2;
    std::filesystem::create_directories(root);
    assert(std::filesystem::canonical(root).parent_path() == workspace / L"build");
    assert(safePath(root.wstring()));
    assert(!safePath(root.root_path().wstring()));
    assert(!safePath((root / L".." / root.filename()).wstring()));
    assert(!safePath(L"\\\\server\\share\\RizomUV"));
    assert(!safePath(root.wstring() + L":stream"));
    FakeHost(root / L"rizomuv.exe", IMAGE_FILE_MACHINE_AMD64);
    assert(regularFile((root / L"rizomuv.exe").wstring()));
    assert(amd64((root / L"rizomuv.exe").wstring()));
    FakeHost(root / L"x86.exe", IMAGE_FILE_MACHINE_I386);
    assert(!amd64((root / L"x86.exe").wstring()));
    std::ofstream(root / L"broken.exe", std::ios::binary) << "not a PE";
    assert(!amd64((root / L"broken.exe").wstring()));
    unsigned count = 0;
    assert(safeTree(root / L"missing", 0, count));
    const auto plugin = root / L"ChineseLauncher";
    std::filesystem::create_directories(plugin);
    std::ofstream(plugin / L"custom.json") << "user data";
    assert(safeTree(plugin, 0, count));
    count = 10000;
    assert(!safeTree(plugin, 0, count));
    auto nested = plugin;
    for (int i = 0; i < 17; ++i) nested /= L"nested";
    std::filesystem::create_directories(nested);
    count = 0;
    assert(!safeTree(plugin, 0, count));
    // root was created by this process, canonicalized and scoped to build.
    std::filesystem::remove_all(root);
    std::cout << "PASS: installer local path, roots/devices/UNC, x64/x86/bad PE, missing files and bounded tree checks\n";
}
