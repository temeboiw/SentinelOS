# UEFI Secure Boot builds

`scripts/build.sh --secure-boot` signs the three EFI applications in the boot chain (`BOOTX64.EFI`, `bootuni.efi`, and `loadufi.efi`) with a private key and certificate supplied by the build operator. The normal developer build remains unsigned. Firmware must trust the matching certificate in its Secure Boot `db` variable before it will start this media.

Install `sbsigntool` and OpenSSL. Create a signing key and certificate locally (keep the key private):

```sh
openssl req -new -x509 -newkey rsa:2048 -keyout sentinel-uefi.key \
  -out sentinel-uefi.crt -days 3650 -nodes -subj '/CN=SentinelOS UEFI/'
```

Build the EFI chain:

```sh
SECURE_BOOT_KEY="$PWD/sentinel-uefi.key" \
SECURE_BOOT_CERT="$PWD/sentinel-uefi.crt" \
bash scripts/build.sh --secure-boot
```

To make and validate installer media, keep the certificate setting and enable Secure Boot checks for both packaging steps:

```sh
export SECURE_BOOT=1 SECURE_BOOT_CERT="$PWD/sentinel-uefi.crt"
bash installer/build-installer.sh
bash installer/make-iso.sh
bash installer/check-iso.sh
```

Enroll the certificate in the target machine's UEFI Secure Boot `db` trust list using its firmware key-management UI or a trusted UEFI key-enrollment utility. Firmware interfaces vary; some require the certificate as DER rather than PEM, which can be produced with `openssl x509 -in sentinel-uefi.crt -outform DER -out sentinel-uefi.der`. Enable Secure Boot after enrollment. MOK enrollment alone is not sufficient unless the firmware's shim path is actually used; SentinelOS does not bundle shim. Keep the private key offline and use a disposable VM with Secure Boot enabled to verify the boot path.

The firmware-authenticated chain currently ends at `loadufi.efi`: the kernel and HAL are loaded by SentinelOS's own loader and are not independently Authenticode-signed or verified by a kernel trust policy. This provides Secure Boot validation of EFI stages, not a complete measured or verified boot of every OS component.
