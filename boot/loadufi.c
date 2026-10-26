// loadufi.efi - stage 2. Loads HAL+kernel, higher-half maps, jumps to high kmain.
#include "common.h"
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <IndustryStandard/PeImage.h>
#include "bootinfo.h"
#include "ext4.h"

EFI_GUID gEfiGraphicsOutputProtocolGuid = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
EFI_GUID gEfiLoadedImageProtocolGuid = {0x5B1B31A1,0x9562,0x11d2,{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
EFI_GUID gEfiSimpleFileSystemProtocolGuid = {0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
EFI_GUID gEfiFileInfoGuid = {0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

/* EDID protocols expose the monitor's preferred timing. If EDID is absent or
 * unusable, leave the firmware-selected GOP mode unchanged. */
typedef struct { UINT32 SizeOfEdid; UINT8 *Edid; } SENTINEL_EDID_PROTOCOL;
EFI_GUID gEfiEdidActiveProtocolGuid =
    {0xbd8c1056,0x9f36,0x44ec,{0x92,0xa8,0xa6,0x33,0x7f,0x81,0x79,0x86}};
EFI_GUID gEfiEdidDiscoveredProtocolGuid =
    {0x1c0c34f6,0xd380,0x41fa,{0xa0,0x49,0x8a,0xd0,0x6c,0x1a,0x66,0xaa}};

static int edid_preferred_size(EFI_SYSTEM_TABLE *sys, UINT32 *width, UINT32 *height) {
    EFI_GUID *guids[2] = { &gEfiEdidActiveProtocolGuid, &gEfiEdidDiscoveredProtocolGuid };
    for (UINTN g = 0; g < 2; g++) {
        SENTINEL_EDID_PROTOCOL *edid = NULL;
        if (EFI_ERROR(sys->BootServices->LocateProtocol(guids[g], NULL, (VOID **)&edid)) ||
            !edid || !edid->Edid || edid->SizeOfEdid < 128)
            continue;
        const UINT8 *e = edid->Edid;
        static const UINT8 header[8] = {0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00};
        int valid = 1;
        for (UINTN i = 0; i < 8; i++) if (e[i] != header[i]) valid = 0;
        UINT32 sum = 0;
        for (UINTN i = 0; i < 128; i++) sum += e[i];
        if (!valid || (sum & 0xffu) != 0) continue;
        /* The first base-block detailed timing descriptor is the EDID
         * preferred timing when present. Skip monitor descriptors (clock=0). */
        for (UINTN off = 54; off + 18 <= 126; off += 18) {
            const UINT8 *d = e + off;
            if (!(d[0] | d[1])) continue;
            UINT32 w = (UINT32)d[2] | ((UINT32)(d[4] & 0xf0u) << 4);
            UINT32 h = (UINT32)d[5] | ((UINT32)(d[7] & 0xf0u) << 4);
            if (w >= 320 && h >= 200 && w <= 8192 && h <= 4320) {
                *width = w; *height = h;
                return 1;
            }
        }
    }
    return 0;
}

static void gop_select_display_mode(EFI_SYSTEM_TABLE *sys,
                                    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
    UINT32 want_w = 0, want_h = 0;
    if (!gop || !gop->Mode || !edid_preferred_size(sys, &want_w, &want_h)) {
        say(L"loadufi: EDID preferred mode unavailable; keeping firmware GOP mode\r\n");
        return;
    }
    UINT32 exact = 0xffffffffu, best = 0xffffffffu;
    UINT64 best_area = 0;
    UINTN max_mode = gop->Mode->MaxMode;
    for (UINT32 mode = 0; mode < max_mode; mode++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN info_size = 0;
        EFI_STATUS st_mode = gop->QueryMode(gop, mode, &info_size, &info);
        if (EFI_ERROR(st_mode) || !info) continue;
        UINT32 w = info->HorizontalResolution, h = info->VerticalResolution;
        if (w == want_w && h == want_h) exact = mode;
        /* If exact timing is unavailable, choose the largest mode which does
         * not exceed the preferred dimensions and fits within the preferred dimensions. */
        if (w >= 320 && h >= 200 && w <= want_w && h <= want_h) {
            UINT64 area = (UINT64)w * h;
            if (area > best_area) { best_area = area; best = mode; }
        }
        sys->BootServices->FreePool(info);
    }
    UINT32 chosen = exact != 0xffffffffu ? exact : best;
    if (chosen == 0xffffffffu) {
        say(L"loadufi: EDID preferred mode has no compatible GOP mode; keeping firmware mode\r\n");
        return;
    }
    if (gop->Mode->Mode == chosen) {
        say(L"loadufi: firmware already uses EDID display mode\r\n");
        return;
    }
    EFI_STATUS set_status = gop->SetMode(gop, chosen);
    if (EFI_ERROR(set_status)) {
        say(L"loadufi: could not set EDID display mode; keeping firmware mode\r\n");
        return;
    }
    say(L"loadufi: selected EDID-preferred display mode ");
    /* Avoid printf dependencies in the UEFI loader. */
    CHAR16 digits[12];
    UINT32 vals[2] = { gop->Mode->Info->HorizontalResolution,
                       gop->Mode->Info->VerticalResolution };
    for (UINTN v = 0; v < 2; v++) {
        UINT32 n = vals[v]; UINTN p = 0;
        do { digits[p++] = (CHAR16)(L'0' + n % 10); n /= 10; } while (n && p < 11);
        while (p) { CHAR16 c[2] = { digits[--p], 0 }; say(c); }
        say(v == 0 ? L"x" : L"\r\n");
    }
}

typedef struct { UINT32 OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk; } IMPORT_DESC;
typedef struct {
    UINT32 Characteristics, TimeDateStamp; UINT16 MajorVersion, MinorVersion;
    UINT32 Name, Base, NumberOfFunctions, NumberOfNames, AddressOfFunctions, AddressOfNames, AddressOfNameOrdinals;
} EXPORT_DIR;

typedef struct {
    char name[32];
    UINT8 *base;          /* physical while loading */
    UINT64 phys, size, entry;
    EFI_IMAGE_NT_HEADERS64 *nt;
} Module;

static Module mods[BOOT_MAX_MODULES];
static int nmods;
static EFI_HANDLE boot_image;

static void say_a(const char *s) {
    for (; *s; s++) { CHAR16 b[2] = { (CHAR16)(UINT8)*s, 0 }; say(b); }
}
static void die(const char *a, const char *b, const char *c) {
    say(L"loadufi: "); say_a(a); say_a(b); say_a(c); say(L"\r\n");
    for (;;) {}
}
static CHAR8 lc(CHAR8 c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int streq(const char *a, const char *b, int nocase) {
    for (; *a && *b; a++, b++)
        if ((nocase ? lc(*a) : *a) != (nocase ? lc(*b) : *b)) return 0;
    return *a == *b;
}

/* Installer media stores boot-critical PE images on its FAT ESP. Prefer
 * that self-contained source, then fall back to the installed ext4 volume. */
static EFI_STATUS esp_load_file(const char *const *parts, int depth,
                                VOID **file, UINTN *size) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    EFI_FILE_PROTOCOL *root = NULL, *handle = NULL;
    EFI_STATUS status = bs->HandleProtocol(boot_image,
        &gEfiLoadedImageProtocolGuid, (VOID **)&loaded);
    if (EFI_ERROR(status) || !loaded || !loaded->DeviceHandle) return EFI_NOT_FOUND;
    status = bs->HandleProtocol(loaded->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid, (VOID **)&sfs);
    if (EFI_ERROR(status) || !sfs) return EFI_NOT_FOUND;
    status = sfs->OpenVolume(sfs, &root);
    if (EFI_ERROR(status) || !root) return EFI_NOT_FOUND;

    CHAR16 path[256]; UINTN p = 0;
    path[p++] = L'\\';
    for (int k = 0; k < depth; k++) {
        for (const char *c = parts[k]; *c && p + 2 < sizeof(path)/sizeof(path[0]); c++)
            path[p++] = (CHAR16)(*c == '/' ? '\\' : (UINT8)*c);
        if (k + 1 < depth) path[p++] = L'\\';
    }
    path[p] = 0;
    status = root->Open(root, &handle, path, EFI_FILE_MODE_READ, 0);
    root->Close(root);
    if (EFI_ERROR(status) || !handle) return EFI_NOT_FOUND;

    UINTN info_size = 0;
    status = handle->GetInfo(handle, &gEfiFileInfoGuid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO)) {
        handle->Close(handle); return EFI_LOAD_ERROR;
    }
    EFI_FILE_INFO *info = NULL;
    status = bs->AllocatePool(EfiLoaderData, info_size, (VOID **)&info);
    if (EFI_ERROR(status)) { handle->Close(handle); return status; }
    status = handle->GetInfo(handle, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(status) || info->FileSize > (UINT64)(~(UINTN)0)) {
        bs->FreePool(info); handle->Close(handle); return EFI_LOAD_ERROR;
    }
    UINTN bytes = (UINTN)info->FileSize;
    bs->FreePool(info);
    VOID *buffer = NULL;
    status = bs->AllocatePool(EfiLoaderData, bytes ? bytes : 1, &buffer);
    if (EFI_ERROR(status)) { handle->Close(handle); return status; }
    UINTN got = bytes;
    status = handle->Read(handle, &got, buffer);
    handle->Close(handle);
    if (EFI_ERROR(status) || got != bytes) {
        bs->FreePool(buffer); return EFI_LOAD_ERROR;
    }
    *file = buffer; *size = bytes;
    return EFI_SUCCESS;
}

static EFI_STATUS os_load_file(const char *const *parts, int depth,
                               VOID **file, UINTN *size) {
    EFI_STATUS status = esp_load_file(parts, depth, file, size);
    if (!EFI_ERROR(status)) return status;
    return ext_load_file(parts, depth, file, size);
}

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
    if (EFI_ERROR(s)) {
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
        memcpy((VOID *)(base + sec[i].VirtualAddress),
               (UINT8 *)file + sec[i].PointerToRawData, n);
    }

    INT64 delta = (INT64)(base - want);
    EFI_IMAGE_DATA_DIRECTORY *rd =
        &nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && rd->Size) {
        UINT8 *p = (UINT8 *)base + rd->VirtualAddress, *end = p + rd->Size;
        while (p < end) {
            EFI_IMAGE_BASE_RELOCATION *b = (VOID *)p;
            if (!b->SizeOfBlock) break;
            UINT16 *e = (UINT16 *)(b + 1);
            UINTN n = (b->SizeOfBlock - sizeof *b) / 2;
            for (UINTN i = 0; i < n; i++)
                if ((e[i] >> 12) == EFI_IMAGE_REL_BASED_DIR64)
                    *(UINT64 *)(base + b->VirtualAddress + (e[i] & 0xFFF)) += delta;
            p += b->SizeOfBlock;
        }
    }
    m->base  = (UINT8 *)base;
    m->phys  = (UINT64)base;
    m->size  = nt->OptionalHeader.SizeOfImage;
    m->nt    = (EFI_IMAGE_NT_HEADERS64 *)(m->base + dos->e_lfanew);
    m->entry = nt->OptionalHeader.AddressOfEntryPoint
             ? base + nt->OptionalHeader.AddressOfEntryPoint : 0;
    return EFI_SUCCESS;
}

static Module *load_module(const char *name) {
    const char *path[3] = { "magic", "System32", name };
    VOID *file; UINTN fsize;
    if (EFI_ERROR(os_load_file(path, 3, &file, &fsize)))
        die("cannot read magic/System32/", name, "");
    if (nmods >= BOOT_MAX_MODULES) die("too many modules", "", "");
    Module *m = &mods[nmods];
    int i = 0; for (; name[i] && i < 31; i++) m->name[i] = name[i];
    m->name[i] = 0;
    if (EFI_ERROR(map_image(file, m))) die("not a valid x64 PE image: ", name, "");
    nmods++;
    return m;
}

static UINT64 find_export(Module *m, const char *name, UINT16 ordinal, int by_ordinal) {
    EFI_IMAGE_DATA_DIRECTORY *dd =
        &m->nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd->Size) return 0;
    EXPORT_DIR *exp = (VOID *)(m->base + dd->VirtualAddress);
    UINT32 *funcs = (VOID *)(m->base + exp->AddressOfFunctions);
    if (by_ordinal) {
        if (ordinal < exp->Base || ordinal - exp->Base >= exp->NumberOfFunctions) return 0;
        return (UINT64)(m->base + funcs[ordinal - exp->Base]);
    }
    UINT32 *names = (VOID *)(m->base + exp->AddressOfNames);
    UINT16 *ords  = (VOID *)(m->base + exp->AddressOfNameOrdinals);
    for (UINT32 i = 0; i < exp->NumberOfNames; i++)
        if (streq((const char *)(m->base + names[i]), name, 0))
            return (UINT64)(m->base + funcs[ords[i]]);
    return 0;
}

static void resolve_imports(Module *m) {
    EFI_IMAGE_DATA_DIRECTORY *dd =
        &m->nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd->Size) return;
    for (IMPORT_DESC *d = (IMPORT_DESC *)(m->base + dd->VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(m->base + d->Name);
        Module *src = NULL;
        for (int i = 0; i < nmods; i++) if (streq(mods[i].name, dll, 1)) src = &mods[i];
        if (!src) die(m->name, " imports from unloaded: ", dll);
        UINT64 *look = (UINT64 *)(m->base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        UINT64 *iat  = (UINT64 *)(m->base + d->FirstThunk);
        for (; *look; look++, iat++) {
            UINT64 addr; const char *sym = "(ordinal)";
            if (*look >> 63)
                addr = find_export(src, NULL, (UINT16)*look, 1);
            else {
                sym  = (const char *)(m->base + (UINT32)*look + 2);
                addr = find_export(src, sym, 0, 0);
            }
            if (!addr) die(m->name, " unresolved ", sym);
            *iat = addr;
        }
    }
}

/* ---- higher-half page tables ---- */
#define HH_PTE_P    (1ULL << 0)
#define HH_PTE_RW   (1ULL << 1)
#define HH_PTE_PS   (1ULL << 7)
#define HH_PTE_ADDR 0x000FFFFFFFFFF000ULL
#define HH_LARGE    (2ULL << 20)
#define HH_HHDM     0xFFFF800000000000ULL

static UINT64 *hh_alloc_page(void) {
    EFI_PHYSICAL_ADDRESS pa = 0;
    if (EFI_ERROR(bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pa)))
        die("hh: out of PT pages", "", "");
    memset((VOID *)pa, 0, 4096);
    return (UINT64 *)(UINTN)pa;
}

static void hh_map_2m(UINT64 *pml4, UINT64 virt, UINT64 phys, UINT64 attr) {
    UINTN i4 = (virt >> 39) & 511, i3 = (virt >> 30) & 511, i2 = (virt >> 21) & 511;
    if (!(pml4[i4] & HH_PTE_P)) {
        UINT64 *x = hh_alloc_page();
        pml4[i4] = (UINT64)(UINTN)x | HH_PTE_P | HH_PTE_RW;
    }
    UINT64 *pdpt = (UINT64 *)(UINTN)(pml4[i4] & HH_PTE_ADDR);
    if (!(pdpt[i3] & HH_PTE_P)) {
        UINT64 *x = hh_alloc_page();
        pdpt[i3] = (UINT64)(UINTN)x | HH_PTE_P | HH_PTE_RW;
    }
    UINT64 *pd = (UINT64 *)(UINTN)(pdpt[i3] & HH_PTE_ADDR);
    if (!(pd[i2] & HH_PTE_P))
        pd[i2] = (phys & ~(HH_LARGE - 1)) | HH_PTE_P | HH_PTE_RW | HH_PTE_PS | attr;
}

static void hh_map_4k(UINT64 *pml4, UINT64 virt, UINT64 phys, UINT64 attr) {
    UINTN i4 = (virt >> 39) & 511, i3 = (virt >> 30) & 511, i2 = (virt >> 21) & 511, i1 = (virt >> 12) & 511;
    if (!(pml4[i4] & HH_PTE_P)) {
        UINT64 *x = hh_alloc_page();
        pml4[i4] = (UINT64)(UINTN)x | HH_PTE_P | HH_PTE_RW;
    }
    UINT64 *pdpt = (UINT64 *)(UINTN)(pml4[i4] & HH_PTE_ADDR);
    if (!(pdpt[i3] & HH_PTE_P)) {
        UINT64 *x = hh_alloc_page();
        pdpt[i3] = (UINT64)(UINTN)x | HH_PTE_P | HH_PTE_RW;
    }
    UINT64 *pd = (UINT64 *)(UINTN)(pdpt[i3] & HH_PTE_ADDR);
    UINT64 *pt;
    if (pd[i2] & HH_PTE_P) {
        if (pd[i2] & HH_PTE_PS) die("hh: 2M clash", "", "");
        pt = (UINT64 *)(UINTN)(pd[i2] & HH_PTE_ADDR);
    } else {
        pt = hh_alloc_page();
        pd[i2] = (UINT64)(UINTN)pt | HH_PTE_P | HH_PTE_RW;
    }
    pt[i1] = (phys & HH_PTE_ADDR) | HH_PTE_P | HH_PTE_RW | attr;
}

static void hh_map_range_2m(UINT64 *pml4, UINT64 voff, UINT64 s, UINT64 e) {
    s &= ~(HH_LARGE - 1);
    e = (e + HH_LARGE - 1) & ~(HH_LARGE - 1);
    for (UINT64 p = s; p < e; p += HH_LARGE)
        hh_map_2m(pml4, voff + p, p, 0);
}

static void hh_apply_relocs(UINT8 *base, INT64 delta) {
    EFI_IMAGE_DOS_HEADER *dos = (VOID *)base;
    EFI_IMAGE_NT_HEADERS64 *nt = (VOID *)(base + dos->e_lfanew);
    EFI_IMAGE_DATA_DIRECTORY *rd =
        &nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!delta || !rd->Size) return;
    UINT8 *p = base + rd->VirtualAddress, *end = p + rd->Size;
    while (p < end) {
        EFI_IMAGE_BASE_RELOCATION *b = (VOID *)p;
        if (!b->SizeOfBlock) break;
        UINT16 *e = (UINT16 *)(b + 1);
        UINTN n = (b->SizeOfBlock - sizeof *b) / 2;
        for (UINTN i = 0; i < n; i++)
            if ((e[i] >> 12) == EFI_IMAGE_REL_BASED_DIR64)
                *(UINT64 *)(base + b->VirtualAddress + (e[i] & 0xFFF)) += delta;
        p += b->SizeOfBlock;
    }
}

static UINT64 bi_prefer(Module *m) {
    UINT64 prefer = m->nt->OptionalHeader.ImageBase;
    if (prefer < 0xFFFF800000000000ULL) prefer = HH_HHDM + m->phys;
    return prefer;
}

static UINT64 *hh_build_tables(void) {
    UINT64 *pml4 = hh_alloc_page();
    hh_map_range_2m(pml4, 0, 0, 4ULL << 30);
    hh_map_range_2m(pml4, HH_HHDM, 0, 4ULL << 30);
    hh_map_range_2m(pml4, 0, 0x80000000ULL, 0x90000000ULL);
    hh_map_range_2m(pml4, HH_HHDM, 0x80000000ULL, 0x90000000ULL);
    hh_map_range_2m(pml4, 0, 0xFEC00000ULL, 0x100000000ULL);
    hh_map_range_2m(pml4, HH_HHDM, 0xFEC00000ULL, 0x100000000ULL);
    return pml4;
}

static void resolve_imports_hh(int mi) {
    Module *m = &mods[mi];
    UINT8 *phys = (UINT8 *)(UINTN)m->phys;
    EFI_IMAGE_DATA_DIRECTORY *dd =
        &m->nt->OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd->Size) return;
    for (IMPORT_DESC *d = (IMPORT_DESC *)(phys + dd->VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(phys + d->Name);
        Module *src = NULL;
        for (int j = 0; j < nmods; j++)
            if (streq(mods[j].name, dll, 1)) { src = &mods[j]; break; }
        if (!src) die(m->name, " imports missing ", dll);
        UINT64 *look = (UINT64 *)(phys + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        UINT64 *iat  = (UINT64 *)(phys + d->FirstThunk);
        for (; *look; look++, iat++) {
            UINT64 abs_phys;
            if (*look >> 63)
                abs_phys = find_export(src, NULL, (UINT16)*look, 1);
            else {
                const char *sym = (const char *)(phys + (UINT32)*look + 2);
                abs_phys = find_export(src, sym, 0, 0);
            }
            if (!abs_phys) die(m->name, " unresolved import", "");
            *iat = bi_prefer(src) + (abs_phys - src->phys);
        }
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys) {
    boot_image = image;
    st = sys; bs = sys->BootServices;
    say(L"loadufi: loading hal.dll and krnl.exe\r\n");

    load_module("hal.dll");
    Module *krnl = load_module("krnl.exe");

    /* User images are loaded by the kernel after mounting ext4. */

    for (int i = 0; i < nmods; i++) {
        if (!streq(mods[i].name, "hal.dll", 1) &&
            !streq(mods[i].name, "krnl.exe", 1)) continue;

        say(L"loadufi: resolving ");
        say_a(mods[i].name);
        say(L"\r\n");
        resolve_imports_hh(i);
    }

    say(L"loadufi: imports resolved\r\n");

    if (!krnl->entry) die("krnl.exe has no entry point", "", "");

    BootInfo *bi;
    bs->AllocatePool(EfiLoaderData, sizeof *bi, (VOID **)&bi);
    memset(bi, 0, sizeof *bi);
    /* SIPI vectors address a 4 KiB page below 1 MiB. Reserve it before the
       final memory-map snapshot so the kernel can copy its AP trampoline there. */
    EFI_PHYSICAL_ADDRESS ap_trampoline = 0x7FFFF; /* stay below EBDA and VGA memory */
    EFI_STATUS ap_page_status = bs->AllocatePages(
        AllocateMaxAddress, EfiLoaderData, 1, &ap_trampoline);
    if (!EFI_ERROR(ap_page_status) && ap_trampoline < 0x100000ULL) {
        bi->ap_trampoline_phys = ap_trampoline;
        memset((VOID *)(UINTN)ap_trampoline, 0, 4096);
        say(L"loadufi: reserved low-memory AP SIPI page\r\n");
    } else {
        bi->ap_trampoline_phys = 0;
        say(L"loadufi: AP SIPI page unavailable; booting BSP-only\r\n");
    }
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    if (!EFI_ERROR(bs->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gop))) {
        gop_select_display_mode(sys, gop);
        /* Restore the silent bootvid-matching gradient after any GOP mode switch. */
        paint_firmware_background();
        bi->fb_base = gop->Mode->FrameBufferBase;
        bi->width   = gop->Mode->Info->HorizontalResolution;
        bi->height  = gop->Mode->Info->VerticalResolution;
        bi->pitch   = gop->Mode->Info->PixelsPerScanLine * 4;
    }

    UINT64 *hh_pml4 = hh_build_tables();
    
    for (int i = 0; i < nmods; i++) {
        UINT64 phys = mods[i].phys;
        UINT64 size = mods[i].size;
        int is_user = !streq(mods[i].name, "hal.dll", 1) && !streq(mods[i].name, "krnl.exe", 1);
        UINT64 prefer = mods[i].nt->OptionalHeader.ImageBase;

        if (is_user) {
            if (!prefer) prefer = 0x40000000ULL;
            say(L"loadufi: user image ");
            say_a(mods[i].name);
            say(L"\r\n");
            UINT64 rva = mods[i].entry - phys;
            bi->modules[i].base = prefer;
            bi->modules[i].phys = phys;
            bi->modules[i].entry = prefer + rva;
            bi->modules[i].size = size;
        } else {
            if (prefer < 0xFFFF800000000000ULL)
                prefer = HH_HHDM + phys;
            say(L"loadufi: higher-half ");
            say_a(mods[i].name);
            say(L"\r\n");
            for (UINT64 off = 0; off < size; off += 4096)
                hh_map_4k(hh_pml4, prefer + off, phys + off, 0);
            hh_apply_relocs((UINT8 *)(UINTN)phys, (INT64)(prefer - phys));
            mods[i].entry = prefer + (mods[i].entry - phys);
            bi->modules[i].base = prefer;
            bi->modules[i].phys = phys;
            bi->modules[i].entry = mods[i].entry;
            bi->modules[i].size = size;
        }
        for (int k = 0; k < 32; k++) bi->modules[i].name[k] = mods[i].name[k];
    }

    bi->module_count = (UINT32)nmods;

    for (int i = 0; i < nmods; i++) {
        if (!streq(mods[i].name, "hal.dll", 1) && !streq(mods[i].name, "krnl.exe", 1)) continue;
        resolve_imports_hh(i);
    }

    
    say(L"loadufi: preparing memory map\r\n");

    UINTN msz = 0, key = 0, dsz = 0;
    UINT32 dver = 0;
    EFI_MEMORY_DESCRIPTOR *map = NULL;

    EFI_STATUS status = bs->GetMemoryMap(
        &msz, NULL, &key, &dsz, &dver
    );

    if (status != EFI_BUFFER_TOO_SMALL || dsz == 0)
        for (;;) __asm__ volatile("hlt");

    UINTN map_capacity = msz + 16 * dsz;

    status = bs->AllocatePool(
        EfiLoaderData, map_capacity, (VOID **)&map
    );

    if (EFI_ERROR(status))
        for (;;) __asm__ volatile("hlt");

    int attempt = 0;

    while (attempt < 4) {
        msz = map_capacity;

        status = bs->GetMemoryMap(
            &msz, map, &key, &dsz, &dver
        );

        if (status == EFI_BUFFER_TOO_SMALL) {
            bs->FreePool(map);
            map = NULL;

            map_capacity = msz + 16 * dsz;

            status = bs->AllocatePool(
                EfiLoaderData, map_capacity, (VOID **)&map
            );

            if (EFI_ERROR(status))
                for (;;) __asm__ volatile("hlt");

            continue;
        }

        if (EFI_ERROR(status))
            for (;;) __asm__ volatile("hlt");

        bi->mmap = (UINT64)(UINTN)map;
        bi->mmap_size = msz;
        bi->desc_size = dsz;

        /* Do not call console or allocation services between
           GetMemoryMap and ExitBootServices. */
        status = bs->ExitBootServices(image, key);

        if (!EFI_ERROR(status))
            break;

        attempt++;
    }

    if (EFI_ERROR(status))
        for (;;) __asm__ volatile("hlt");

    __asm__ __volatile__("mov %0, %%cr3" :: "r"((UINT64)(UINTN)hh_pml4) : "memory");
    ((void (*)(BootInfo *))(UINTN)krnl->entry)(bi);
    for (;;) {}
}
