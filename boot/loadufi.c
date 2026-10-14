// loadufi.efi - stage 2. Loads HAL+kernel, higher-half maps, jumps to high kmain.
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

typedef struct {
    char name[32];
    UINT8 *base;          /* physical while loading */
    UINT64 phys, size, entry;
    EFI_IMAGE_NT_HEADERS64 *nt;
} Module;

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
    for (; *a && *b; a++, b++)
        if ((nocase ? lc(*a) : *a) != (nocase ? lc(*b) : *b)) return 0;
    return *a == *b;
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
    if (EFI_ERROR(ext_load_file(path, 3, &file, &fsize)))
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
    st = sys; bs = sys->BootServices;
    say(L"loadufi: loading hal.dll and krnl.exe\r\n");

    load_module("hal.dll");
    Module *krnl = load_module("krnl.exe");

    {
        static const char *users[] = { "smss.exe", "explorer.exe", "fileexplorer.exe", "taskbar.dll", "kernel32.dll", "user32.dll", "hello.exe" };
        int nusers = 7;
        for (int u = 0; u < nusers; u++) {
            const char *path[3] = { "magic", "System32", users[u] };
            VOID *file; UINTN fsize;
            EFI_STATUS st2 = ext_load_file(path, 3, &file, &fsize);
            if (EFI_ERROR(st2)) {
                say(L"loadufi: missing user image ");
                say_a(users[u]);
                say(L"\r\n");
                continue;
            }
            if (nmods >= BOOT_MAX_MODULES) break;
            Module *m = &mods[nmods];
            int ii = 0;
            for (; users[u][ii] && ii < 31; ii++) m->name[ii] = users[u][ii];
            m->name[ii] = 0;
            st2 = map_image(file, m);
            if (EFI_ERROR(st2)) {
                say(L"loadufi: bad PE ");
                say_a(users[u]);
                say(L"\r\n");
                continue;
            }
            nmods++;
            say(L"loadufi: loaded user image ");
            say_a(users[u]);
            say(L"\r\n");
        }
    }

    for (int i = 0; i < nmods; i++) {
        if (!streq(mods[i].name, "hal.dll", 1) && !streq(mods[i].name, "krnl.exe", 1)) continue;
        resolve_imports(&mods[i]);
    }
    if (!krnl->entry) die("krnl.exe has no entry point", "", "");

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

    UINTN msz = 0, key, dsz; UINT32 dver; EFI_MEMORY_DESCRIPTOR *map;
    bs->GetMemoryMap(&msz, NULL, &key, &dsz, &dver);
    msz += 8 * dsz;
    bs->AllocatePool(EfiLoaderData, msz, (VOID **)&map);
    if (EFI_ERROR(bs->GetMemoryMap(&msz, map, &key, &dsz, &dver))) for (;;) {}
    bi->mmap = (UINT64)(UINTN)map; bi->mmap_size = msz; bi->desc_size = dsz;

    if (EFI_ERROR(bs->ExitBootServices(image, key))) for (;;) {}

    __asm__ __volatile__("mov %0, %%cr3" :: "r"((UINT64)(UINTN)hh_pml4) : "memory");
    ((void (*)(BootInfo *))(UINTN)krnl->entry)(bi);
    for (;;) {}
}
