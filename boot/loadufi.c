// loadufi.efi - stage 2 (the winload equivalent).  Lives at \EFI\magic\loadufi.efi on the ESP.
// Loads hal.dll and krnl.exe from magic/System32 on the ext4 volume, binds their imports
// against each other's export tables, exits boot services and jumps into the kernel.
#include "common.h"
#include <Protocol/GraphicsOutput.h>
#include <IndustryStandard/PeImage.h>
#include "bootinfo.h"
#include "ext4.h"

EFI_GUID gEfiGraphicsOutputProtocolGuid = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

typedef struct { UINT32 OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk; } IMPORT_DESC;
typedef struct {
    UINT32 Characteristics, TimeDateStamp; UINT16 MajorVersion, MinorVersion;
    UINT32 Name, Base, NumberOfFunctions, NumberOfNames, AddressOfFunctions, AddressOfNames, AddressOfNameOrdinals;
} EXPORT_DIR;

typedef struct { char name[32]; UINT8 *base; UINT64 size, entry; EFI_IMAGE_NT_HEADERS64 *nt; } Module;
static Module mods[BOOT_MAX_MODULES];
static int nmods;

static void say_a(const char *s) {
    for (; *s; s++) { CHAR16 b[2] = { (CHAR16)(UINT8)*s, 0 }; say(b); }
}
static void die(const char *a, const char *b, const char *c) {
    say(L"loadufi: "); say_a(a); say_a(b); say_a(c); say(L"\r\n");
    for (;;) {}
}
static CHAR8 lc(CHAR8 c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int streq(const char *a, const char *b, int nocase) {
    for (; *a && *b; a++, b++) if ((nocase ? lc(*a) : *a) != (nocase ? lc(*b) : *b)) return 0;
    return *a == *b;
}

// map one PE image (EXE or DLL): headers + sections, then apply base relocations
static EFI_STATUS map_image(VOID *file, Module *m) {
    EFI_IMAGE_DOS_HEADER *dos = file;
    if (dos->e_magic != EFI_IMAGE_DOS_SIGNATURE) return EFI_LOAD_ERROR;
    EFI_IMAGE_NT_HEADERS64 *nt = (VOID *)((UINT8 *)file + dos->e_lfanew);
    if (nt->Signature != EFI_IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.Machine != EFI_IMAGE_MACHINE_X64) return EFI_UNSUPPORTED;

    UINT64 want  = nt->OptionalHeader.ImageBase;
    UINTN  pages = EFI_SIZE_TO_PAGES(nt->OptionalHeader.SizeOfImage);
    EFI_PHYSICAL_ADDRESS base = want;
    EFI_STATUS s = bs->AllocatePages(AllocateAddress, EfiLoaderData, pages, &base);
    if (EFI_ERROR(s)) {                             // preferred address busy: load anywhere, relocate
        s = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &base);
        if (EFI_ERROR(s)) return s;
    }
    memset((VOID *)base, 0, pages * 4096);
    memcpy((VOID *)base, file, nt->OptionalHeader.SizeOfHeaders);

    EFI_IMAGE_SECTION_HEADER *sec =
        (VOID *)((UINT8 *)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);
    for (UINT16 i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        UINTN n = sec[i].SizeOfRawData;
        if (sec[i].Misc.VirtualSize && sec[i].Misc.VirtualSize < n) n = sec[i].Misc.VirtualSize;
        memcpy((VOID *)(base + sec[i].VirtualAddress), (UINT8 *)file + sec[i].PointerToRawData, n);
    }

    INT64 delta = (INT64)(base - want);
    EFI_IMAGE_DATA_DIRECTORY *rd = &nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && rd->Size) {
        UINT8 *p = (UINT8 *)base + rd->VirtualAddress, *end = p + rd->Size;
        while (p < end) {
            EFI_IMAGE_BASE_RELOCATION *b = (VOID *)p;
            if (!b->SizeOfBlock) break;
            UINT16 *e = (UINT16 *)(b + 1);
            UINTN   n = (b->SizeOfBlock - sizeof *b) / 2;
            for (UINTN i = 0; i < n; i++)
                if ((e[i] >> 12) == EFI_IMAGE_REL_BASED_DIR64)
                    *(UINT64 *)(base + b->VirtualAddress + (e[i] & 0xFFF)) += delta;
            p += b->SizeOfBlock;
        }
    }
    m->base  = (UINT8 *)base;
    m->size  = nt->OptionalHeader.SizeOfImage;
    m->nt    = (EFI_IMAGE_NT_HEADERS64 *)(m->base + dos->e_lfanew);   // the mapped copy of the headers
    m->entry = nt->OptionalHeader.AddressOfEntryPoint ? base + nt->OptionalHeader.AddressOfEntryPoint : 0;
    return EFI_SUCCESS;
}

static Module *load_module(const char *name) {
    const char *path[3] = { "magic", "System32", name };
    VOID *file; UINTN fsize;
    if (EFI_ERROR(ext_load_file(path, 3, &file, &fsize))) die("cannot read magic/System32/", name, "");
    if (nmods >= BOOT_MAX_MODULES) die("too many modules", "", "");
    Module *m = &mods[nmods];
    int i = 0; for (; name[i] && i < 31; i++) m->name[i] = name[i];
    m->name[i] = 0;
    if (EFI_ERROR(map_image(file, m))) die("not a valid x64 PE image: ", name, "");
    nmods++;
    return m;
}

// look a symbol up in a loaded module's export table (by name or ordinal)
static UINT64 find_export(Module *m, const char *name, UINT16 ordinal, int by_ordinal) {
    EFI_IMAGE_DATA_DIRECTORY *dd = &m->nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd->Size) return 0;
    EXPORT_DIR *e = (EXPORT_DIR *)(m->base + dd->VirtualAddress);
    UINT32 *fn = (UINT32 *)(m->base + e->AddressOfFunctions);
    UINT32 *nm = (UINT32 *)(m->base + e->AddressOfNames);
    UINT16 *od = (UINT16 *)(m->base + e->AddressOfNameOrdinals);
    UINT32 idx = 0;
    if (by_ordinal) {
        if (ordinal < e->Base) return 0;
        idx = ordinal - e->Base;
    } else {
        int found = 0;
        for (UINT32 i = 0; i < e->NumberOfNames; i++)
            if (streq((const char *)(m->base + nm[i]), name, 0)) { idx = od[i]; found = 1; break; }
        if (!found) return 0;
    }
    if (idx >= e->NumberOfFunctions) return 0;
    UINT32 rva = fn[idx];
    if (rva >= dd->VirtualAddress && rva < dd->VirtualAddress + dd->Size) return 0;   // forwarders: not supported yet
    return (UINT64)(m->base + rva);
}

// fill in the import address table of `m` from the other loaded modules
static void resolve_imports(Module *m) {
    EFI_IMAGE_DATA_DIRECTORY *dd = &m->nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd->Size) return;
    for (IMPORT_DESC *d = (IMPORT_DESC *)(m->base + dd->VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(m->base + d->Name);
        Module *src = NULL;
        for (int i = 0; i < nmods; i++) if (streq(mods[i].name, dll, 1)) src = &mods[i];
        if (!src) die(m->name, " imports from a module that is not loaded: ", dll);

        UINT64 *look = (UINT64 *)(m->base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        UINT64 *iat  = (UINT64 *)(m->base + d->FirstThunk);
        for (; *look; look++, iat++) {
            UINT64 addr; const char *sym = "(ordinal)";
            if (*look >> 63) {
                addr = find_export(src, NULL, (UINT16)*look, 1);
            } else {
                sym  = (const char *)(m->base + (UINT32)*look + 2);        // skip the 2-byte hint
                addr = find_export(src, sym, 0, 0);
            }
            if (!addr) { say(L"loadufi: unresolved import: "); say_a(m->name); say_a(" -> "); say_a(dll); say_a("!"); say_a(sym); say(L"\r\n"); for (;;) {} }
            *iat = addr;
        }
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys) {
    st = sys; bs = sys->BootServices;
    say(L"loadufi: loading hal.dll and krnl.exe from magic/System32\r\n");

    // 1. read + map every module, 2. then bind imports (so modules may import from each other)
    load_module("hal.dll");
    Module *krnl = load_module("krnl.exe");
    for (int i = 0; i < nmods; i++) resolve_imports(&mods[i]);
    if (!krnl->entry) die("krnl.exe has no entry point", "", "");

    // 3. boot info: framebuffer + module list
    BootInfo *bi;
    bs->AllocatePool(EfiLoaderData, sizeof *bi, (VOID **)&bi);
    memset(bi, 0, sizeof *bi);
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    if (!EFI_ERROR(bs->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gop))) {
        bi->fb_base = gop->Mode->FrameBufferBase;
        bi->width   = gop->Mode->Info->HorizontalResolution;
        bi->height  = gop->Mode->Info->VerticalResolution;
        bi->pitch   = gop->Mode->Info->PixelsPerScanLine * 4;
    }
    bi->module_count = (UINT32)nmods;
    for (int i = 0; i < nmods; i++) {
        for (int k = 0; k < 32; k++) bi->modules[i].name[k] = mods[i].name[k];
        bi->modules[i].base = (UINT64)mods[i].base;
        bi->modules[i].size = mods[i].size;
        bi->modules[i].entry = mods[i].entry;
    }

    // 4. memory map + ExitBootServices (slack: the allocation itself changes the map)
    UINTN msz = 0, key, dsz; UINT32 dver; EFI_MEMORY_DESCRIPTOR *map;
    bs->GetMemoryMap(&msz, NULL, &key, &dsz, &dver);
    msz += 4 * dsz;
    bs->AllocatePool(EfiLoaderData, msz, (VOID **)&map);
    bs->GetMemoryMap(&msz, map, &key, &dsz, &dver);
    bi->mmap = (UINT64)map; bi->mmap_size = msz; bi->desc_size = dsz;

    if (EFI_ERROR(bs->ExitBootServices(image, key))) { for (;;) {} }

    // 5. jump: both sides use the Win64 ABI, so BootInfo* arrives in RCX
    ((void (*)(BootInfo *))krnl->entry)(bi);
    for (;;) {}
}
