// bootuni.efi - stage 1.  Lives at \EFI\magic\bootuni.efi on the FAT ESP.
//  1. registers itself as a UEFI boot entry in NVRAM ("magic OS")
//  2. once that is verified, deletes the fallback \EFI\BOOT\BOOTX64.EFI  (build with -DKEEP_FALLBACK to skip)
//  3. starts \EFI\magic\loadufi.efi from the same ESP
#include "common.h"
#include "devpath.h"
#include "nvram.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys) {
    st = sys; bs = sys->BootServices;
    say(L"bootuni: started\r\n");

    EFI_LOADED_IMAGE_PROTOCOL *li;
    if (EFI_ERROR(bs->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (VOID **)&li))) {
        say(L"bootuni: no loaded-image info\r\n");
        for (;;) {}
    }

    // --- NVRAM: only if we really were started from \...\bootuni.efi ---
    if (li->FilePath && dp_file_is(li->FilePath, L"bootuni.efi")) {
        if (!EFI_ERROR(nvram_register(li->FilePath))) {
            say(L"bootuni: boot entry 'magic OS' is in NVRAM\r\n");
#ifndef KEEP_FALLBACK
            if (!EFI_ERROR(remove_file(li->DeviceHandle, L"\\EFI\\BOOT\\BOOTX64.EFI")))
                say(L"bootuni: removed EFI\\BOOT\\BOOTX64.EFI\r\n");
#endif
        } else {
            say(L"bootuni: NVRAM write failed, keeping BOOTX64.EFI\r\n");
        }
    }

    // --- stage 2: loadufi.efi lives next to us on the ESP ---
    say(L"bootuni: loading \\EFI\\magic\\loadufi.efi\r\n");
    EFI_DEVICE_PATH_PROTOCOL *dev, *path;
    EFI_HANDLE child;
    if (EFI_ERROR(bs->HandleProtocol(li->DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID **)&dev)) ||
        !(path = dp_append_file(dev, L"\\EFI\\magic\\loadufi.efi")) ||
        EFI_ERROR(bs->LoadImage(FALSE, image, path, NULL, 0, &child))) {
        say(L"bootuni: \\EFI\\magic\\loadufi.efi not found on the ESP\r\n");
        for (;;) {}
    }
    EFI_STATUS s = bs->StartImage(child, NULL, NULL);
    say(L"bootuni: loadufi returned\r\n");
    return s;
}