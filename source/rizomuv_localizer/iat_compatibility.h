#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace rizomuv::localizer {
// Visit resolved IAT slots, independently of import DLL names, ordinals and
// OriginalFirstThunk. Unresolved delay-loader stubs are left untouched by the
// caller's canonical API-address comparison and revisited on a later scan.
template<class Visitor>
size_t VisitImportSlots(unsigned char* base, size_t size, Visitor visit) {
    auto fits = [size](size_t at, size_t count) { return at <= size && count <= size - at; };
    if (!base || !fits(0, sizeof(IMAGE_DOS_HEADER))) return 0;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        !fits(static_cast<size_t>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64))) return 0;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64)) return 0;
    size_t changed = 0;
    auto thunks = [&](DWORD rva) {
        if (!rva || rva % alignof(uintptr_t)) return;
        for (size_t at = rva; fits(at, sizeof(uintptr_t)); at += sizeof(uintptr_t)) {
            auto slot = reinterpret_cast<void**>(base + at);
            if (!*slot) break;
            if (visit(slot)) ++changed;
        }
    };
    auto directory = [&](unsigned index) -> IMAGE_DATA_DIRECTORY {
        if (index >= nt->OptionalHeader.NumberOfRvaAndSizes) return {};
        const auto entry = nt->OptionalHeader.DataDirectory[index];
        return entry.VirtualAddress && fits(entry.VirtualAddress, entry.Size) ? entry : IMAGE_DATA_DIRECTORY{};
    };
    const auto imports = directory(IMAGE_DIRECTORY_ENTRY_IMPORT);
    for (size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imports.Size;
         offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress + offset);
        if (!descriptor->Name && !descriptor->FirstThunk) break;
        thunks(descriptor->FirstThunk);
    }
    struct DelayImport { DWORD attributes, name, module, iat, names, bound, unload, timestamp; };
    const auto delay = directory(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    for (size_t offset = 0; offset + sizeof(DelayImport) <= delay.Size; offset += sizeof(DelayImport)) {
        const auto descriptor = reinterpret_cast<const DelayImport*>(base + delay.VirtualAddress + offset);
        if (!descriptor->name && !descriptor->iat) break;
        if (descriptor->attributes == 1) thunks(descriptor->iat); // PE32+ RVA descriptors only.
    }
    return changed;
}
}
