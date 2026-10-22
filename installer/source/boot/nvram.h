#pragma once
// UEFI NVRAM: register a Boot#### entry for this image, and remove the fallback loader.
// Include AFTER common.h and devpath.h.

static EFI_GUID gGlobalVar = {0x8BE4DF61,0x93CA,0x11d2,{0xAA,0x0D,0x00,0xE0,0x98,0x03,0x2B,0x8C}};
#define NV_BS_RT 0x7                                    // NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS
static const CHAR16 kDesc[] = L"magic OS";

static void boot_name(CHAR16 *out, UINT16 id) {         // "Boot0001"
    static const CHAR16 hex[] = L"0123456789ABCDEF";
    memcpy(out, L"Boot", 8);
    for (int i = 0; i < 4; i++) out[4 + i] = hex[(id >> (12 - 4 * i)) & 0xF];
    out[8] = 0;
}

static BOOLEAN var_exists(CHAR16 *name) {
    UINTN sz = 0;
    return st->RuntimeServices->GetVariable(name, &gGlobalVar, NULL, &sz, NULL) == EFI_BUFFER_TOO_SMALL;
}

// Make "magic OS" -> `fp` the first entry in BootOrder. Returns SUCCESS only after reading it back.
static EFI_STATUS nvram_register(EFI_DEVICE_PATH_PROTOCOL *fp) {
    EFI_RUNTIME_SERVICES *rt = st->RuntimeServices;
    UINTN fplen = dp_len(fp);
    UINT8 want[512];
    if (6 + sizeof kDesc + fplen > sizeof want) return EFI_BUFFER_TOO_SMALL;

    // EFI_LOAD_OPTION: u32 attributes | u16 filepath-list length | description | filepath list
    *(UINT32 *)want = 1;                                // LOAD_OPTION_ACTIVE
    *(UINT16 *)(want + 4) = (UINT16)fplen;
    memcpy(want + 6, kDesc, sizeof kDesc);
    memcpy(want + 6 + sizeof kDesc, fp, fplen);
    UINTN wsz = 6 + sizeof kDesc + fplen;

    UINT16 order[128]; UINTN osz = sizeof order;
    if (EFI_ERROR(rt->GetVariable(L"BootOrder", &gGlobalVar, NULL, &osz, order))) osz = 0;
    UINTN n = osz / 2;

    // reuse our entry if one with our description already exists
    int id = -1;
    for (UINTN i = 0; i < n && id < 0; i++) {
        CHAR16 nm[9]; UINT8 cur[512]; UINTN csz = sizeof cur;
        boot_name(nm, order[i]);
        if (EFI_ERROR(rt->GetVariable(nm, &gGlobalVar, NULL, &csz, cur))) continue;
        if (csz >= 6 + sizeof kDesc && same(cur + 6, kDesc, sizeof kDesc)) id = order[i];
    }
    // otherwise take the first free Boot#### number
    for (UINT32 c = 0; id < 0 && c < 0x10000; c++) {
        CHAR16 nm[9]; boot_name(nm, (UINT16)c);
        if (!var_exists(nm)) id = (int)c;
    }
    if (id < 0) return EFI_OUT_OF_RESOURCES;

    CHAR16 nm[9]; boot_name(nm, (UINT16)id);
    UINT8 cur[512]; UINTN csz = sizeof cur;
    BOOLEAN identical = !EFI_ERROR(rt->GetVariable(nm, &gGlobalVar, NULL, &csz, cur)) &&
                        csz == wsz && same(cur, want, wsz);
    if (!identical) {
        EFI_STATUS s = rt->SetVariable(nm, &gGlobalVar, NV_BS_RT, wsz, want);
        if (EFI_ERROR(s)) return s;
    }

    if (!(n && order[0] == id)) {                       // put ours first, keep the rest
        UINT16 neworder[128]; UINTN m = 0;
        neworder[m++] = (UINT16)id;
        for (UINTN i = 0; i < n && m < 128; i++) if (order[i] != id) neworder[m++] = order[i];
        EFI_STATUS s = rt->SetVariable(L"BootOrder", &gGlobalVar, NV_BS_RT, m * 2, neworder);
        if (EFI_ERROR(s)) return s;
    }

    // verify by reading both variables back
    UINT16 chk[128]; UINTN ksz = sizeof chk;
    if (EFI_ERROR(rt->GetVariable(L"BootOrder", &gGlobalVar, NULL, &ksz, chk)) || ksz < 2 || chk[0] != id)
        return EFI_ABORTED;
    csz = sizeof cur;
    if (EFI_ERROR(rt->GetVariable(nm, &gGlobalVar, NULL, &csz, cur)) || csz != wsz || !same(cur, want, wsz))
        return EFI_ABORTED;
    return EFI_SUCCESS;
}

// delete a file on the volume `dev`
static EFI_STATUS remove_file(EFI_HANDLE dev, CHAR16 *path) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs; EFI_FILE_PROTOCOL *root, *f;
    EFI_STATUS s = bs->HandleProtocol(dev, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs);
    if (EFI_ERROR(s)) return s;
    s = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(s)) return s;
    s = root->Open(root, &f, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (!EFI_ERROR(s)) s = f->Delete(f);                // Delete also closes the handle
    root->Close(root);
    return s;
}