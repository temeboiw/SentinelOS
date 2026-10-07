// BOOTX64.EFI - stage 0.  The firmware's fallback loader on the ESP.
// Does one thing: starts \EFI\magic\bootuni.efi from the same volume.
#include "common.h"
#include "devpath.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys) {
    st = sys; bs = sys->BootServices;

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