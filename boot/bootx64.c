// BOOTX64.EFI - stage 0.  The firmware's fallback loader on the ESP.
// Does one thing: starts \EFI\magic\bootuni.efi from the same volume.
#include "common.h"
#include "devpath.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys) {
    if (!sys) {
        serial_out8(0xEA, 0x42);  /* debug: sys is NULL */
        return EFI_INVALID_PARAMETER;
    }
    st = sys;
    bs = sys->BootServices;
    if (!bs) {
        serial_out8(0xEA, 0x43);  /* debug: bs is NULL */
        return EFI_INVALID_PARAMETER;
    }

    say(L"BOOTX64: entry\r\n");

    /* Silent firmware stage: paint the bootvid gradient, no on-screen text. */
    paint_firmware_background();

    /* Also output to UEFI ConOut in case serial port I/O is not available
     * from EFI applications under QEMU+OVMF. */
    if (st && st->ConOut)
        st->ConOut->OutputString(st->ConOut, L"BOOTX64: entry\r\n");

    EFI_LOADED_IMAGE_PROTOCOL *li;
    EFI_DEVICE_PATH_PROTOCOL *dev;
    if (EFI_ERROR(bs->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (VOID **)&li)) ||
        EFI_ERROR(bs->HandleProtocol(li->DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID **)&dev))) {
        say(L"BOOTX64: cannot find own volume\r\n");
        return EFI_NOT_FOUND;
    }

    say(L"BOOTX64: found own volume\r\n");

    EFI_DEVICE_PATH_PROTOCOL *path = dp_append_file(dev, L"\\EFI\\magic\\bootuni.efi");
    EFI_HANDLE child;
    if (!path || EFI_ERROR(bs->LoadImage(FALSE, image, path, NULL, 0, &child))) {
        say(L"BOOTX64: \\EFI\\magic\\bootuni.efi not found\r\n");
        return EFI_NOT_FOUND;
    }
    say(L"BOOTX64: loaded bootuni, starting\r\n");
    /* Write to QEMU debug-exit port to confirm we reach StartImage. */
    serial_out8(0xEA, 0x41);
    return bs->StartImage(child, NULL, NULL);
}