// BOOTX64.EFI - stage 0.  The firmware's fallback loader on the ESP.
// Does one thing: starts \EFI\magic\bootuni.efi from the same volume.
#include "common.h"
#include "devpath.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys) {
    st = sys; bs = sys->BootServices;

    /* Firmware-stage splash: simple UEFI text graphics, no extra assets. */
    if (st->ConOut) {
        st->ConOut->ClearScreen(st->ConOut);
        st->ConOut->SetAttribute(st->ConOut, EFI_LIGHTCYAN | EFI_BACKGROUND_BLUE);
        say(L"\r\n");
        say(L"       _____             __  _             __\r\n");
        say(L"      / ___/__  ___  ___ / /_(_)___  ___  / /\r\n");
        say(L"      \\__ \\/ _ \\/ _ \\/ __/ / __ \\/ _ \\/ /\r\n");
        say(L"     ___/ /  __/  __/ /_/ / / / /  __/ /\r\n");
        say(L"    /____/\\___/\\___/\\__/_/_/ /_/\\___/_/\r\n\r\n");
        say(L"                 SENTINELOS  |  BOOTING\r\n");
        say(L"       Initializing session environment...\r\n\r\n");
        st->ConOut->SetAttribute(st->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
    }

    EFI_LOADED_IMAGE_PROTOCOL *li;
    EFI_DEVICE_PATH_PROTOCOL *dev;
    if (EFI_ERROR(bs->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (VOID **)&li)) ||
        EFI_ERROR(bs->HandleProtocol(li->DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID **)&dev))) {
        say(L"BOOTX64: cannot find own volume\r\n");
        return EFI_NOT_FOUND;
    }

    EFI_DEVICE_PATH_PROTOCOL *path = dp_append_file(dev, L"\\EFI\\magic\\bootuni.efi");
    EFI_HANDLE child;
    if (!path || EFI_ERROR(bs->LoadImage(FALSE, image, path, NULL, 0, &child))) {
        say(L"BOOTX64: \\EFI\\magic\\bootuni.efi not found\r\n");
        return EFI_NOT_FOUND;
    }
    return bs->StartImage(child, NULL, NULL);
}