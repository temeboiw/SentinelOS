#!/usr/bin/env bash
# Master a UEFI-bootable hybrid ISO from built installer staging data.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
STAGE="$HERE/staging"
OUT="$HERE/artifacts"
ISO_ROOT="$HERE/iso-root"
ISO="$OUT/SentinelOS-V4-BETA.iso"
ESP_IMG="$ISO_ROOT/boot/esp.img"
ESP_SIZE_MIB="${ESP_SIZE_MIB:-auto}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: required tool not found: $1" >&2; exit 2; }; }
need xorriso
need dd
need mkfs.vfat
need mcopy
need mmd
need sha256sum

[[ -s "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" ]] || { echo "error: staged EFI/BOOT/BOOTX64.EFI missing; run build-installer.sh first" >&2; exit 3; }
[[ -s "$OUT/esp.spf" && -s "$OUT/system.spf" ]] || { echo "error: SPF packages missing; run build-installer.sh first" >&2; exit 3; }
# Build the actual installable partition images on the build host. The native
# installer only writes these verified raw images; it does not need zlib/tar
# support or a host shell at install time.
need truncate
need mkfs.ext4
need mcopy
need mmd
TARGET_ESP_IMG="$OUT/esp.img"
TARGET_SYSTEM_IMG="$OUT/system.img"
INSTALL_SUMS="$OUT/install.sha256"
ESP_IMAGE_SIZE_MIB="${INSTALL_ESP_SIZE_MIB:-128}"
SYSTEM_IMAGE_SIZE_MIB="${INSTALL_SYSTEM_SIZE_MIB:-512}"
[[ "$ESP_IMAGE_SIZE_MIB" =~ ^[0-9]+$ ]] && (( ESP_IMAGE_SIZE_MIB >= 128 && ESP_IMAGE_SIZE_MIB <= 512 )) || { echo "error: INSTALL_ESP_SIZE_MIB must be 128..512" >&2; exit 2; }
[[ "$SYSTEM_IMAGE_SIZE_MIB" =~ ^[0-9]+$ ]] && (( SYSTEM_IMAGE_SIZE_MIB >= 512 && SYSTEM_IMAGE_SIZE_MIB <= 1024 )) || { echo "error: INSTALL_SYSTEM_SIZE_MIB must be 512..1024" >&2; exit 2; }
rm -f "$TARGET_ESP_IMG" "$TARGET_SYSTEM_IMG"
dd if=/dev/zero of="$TARGET_ESP_IMG" bs=1M count="$ESP_IMAGE_SIZE_MIB" status=none
mkfs.vfat -F 32 -n SENTINEL "$TARGET_ESP_IMG" >/dev/null
mmd -i "$TARGET_ESP_IMG" ::/EFI
shopt -s nullglob dotglob
for entry in "$STAGE/esp/EFI"/*; do mcopy -s -i "$TARGET_ESP_IMG" "$entry" ::/EFI/; done
for entry in "$STAGE/esp"/*; do
  [[ "${entry##*/}" == "EFI" ]] && continue
  mcopy -s -i "$TARGET_ESP_IMG" "$entry" ::/
done
shopt -u nullglob dotglob
truncate -s "$((SYSTEM_IMAGE_SIZE_MIB * 1024 * 1024))" "$TARGET_SYSTEM_IMG"
mkfs.ext4 -F -q -L SENTINELOS -d "$STAGE/system" "$TARGET_SYSTEM_IMG"
# Verify critical files directly from the image before it can be embedded.
for f in magic/System32/hal.dll magic/System32/krnl.exe magic/System32/smss.exe magic/System32/instlr.exe; do
  debugfs -R "stat /$f" "$TARGET_SYSTEM_IMG" 2>/dev/null | grep -q 'Type:' || { echo "error: system image is missing /$f" >&2; exit 4; }
done
printf '%s\\n%s\\n' "$(sha256sum "$TARGET_ESP_IMG" | cut -d ' ' -f1)" "$(sha256sum "$TARGET_SYSTEM_IMG" | cut -d ' ' -f1)" > "$INSTALL_SUMS"
# Store package hashes separately so the kernel can verify exact staged bytes before formatting any disk.
printf '%s\n%s\n' "$(sha256sum "$OUT/esp.spf" | cut -d ' ' -f1)" "$(sha256sum "$OUT/system.spf" | cut -d ' ' -f1)" > "$OUT/packages.sha256"
if [[ "$ESP_SIZE_MIB" == "auto" ]]; then
  # Packages are also embedded on the FAT ESP so UEFI can preload them.
  # Size for every staged file plus both packages, with 25% headroom and
  # 16 MiB for FAT metadata/cluster slack. Do not silently truncate at 128 MiB.
  required_bytes="$(du -sb "$STAGE/esp" | awk '{print $1}')"
  required_bytes=$((required_bytes + $(stat -c %s "$TARGET_ESP_IMG") + $(stat -c %s "$TARGET_SYSTEM_IMG") + $(stat -c %s "$INSTALL_SUMS")))
  ESP_SIZE_MIB=$(((required_bytes * 125 / 100 + 16 * 1024 * 1024 + 1024 * 1024 - 1) / (1024 * 1024)))
  (( ESP_SIZE_MIB < 128 )) && ESP_SIZE_MIB=128
  echo "Auto-sized installer ESP: ${ESP_SIZE_MIB} MiB (staged files and both SPF packages)"
fi
[[ "$ESP_SIZE_MIB" =~ ^[0-9]+$ ]] && (( ESP_SIZE_MIB >= 32 && ESP_SIZE_MIB <= 1024 )) || { echo "error: required ESP exceeds supported 32..1024 MiB range; set ESP_SIZE_MIB explicitly or reduce the payload" >&2; exit 2; }

rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/boot" "$ISO_ROOT/packages" "$OUT"
dd if=/dev/zero of="$ESP_IMG" bs=1M count="$ESP_SIZE_MIB" status=none
mkfs.vfat -F 32 -n SENTINEL "$ESP_IMG" >/dev/null
mmd -i "$ESP_IMG" ::/EFI

# Copy every staged EFI vendor directory and any additional ESP-root files.
shopt -s nullglob dotglob
for entry in "$STAGE/esp/EFI"/*; do
  mcopy -s -i "$ESP_IMG" "$entry" ::/EFI/
done
for entry in "$STAGE/esp"/*; do
  [[ "${entry##*/}" == "EFI" ]] && continue
  mcopy -s -i "$ESP_IMG" "$entry" ::/
done
shopt -u nullglob dotglob

# The UEFI loader can only read files on its own FAT ESP. Embed both raw
# package containers there so it can preserve them as boot modules for the
# trusted installer session. The ISO-level copies remain for host-side tools.
mcopy -i "$ESP_IMG" "$TARGET_ESP_IMG" ::/esp.img
mcopy -i "$ESP_IMG" "$TARGET_SYSTEM_IMG" ::/system.img
mcopy -i "$ESP_IMG" "$INSTALL_SUMS" ::/install.sha256

# Fail early if the fallback loader and both payloads made it into the FAT image.
TMP_CHECK="$(mktemp)"
trap 'rm -f "$TMP_CHECK"' EXIT
mcopy -i "$ESP_IMG" ::/EFI/BOOT/BOOTX64.EFI "$TMP_CHECK"
[[ -s "$TMP_CHECK" ]] || { echo "error: EFI image does not contain BOOTX64.EFI" >&2; exit 4; }
mcopy -i "$ESP_IMG" ::/esp.img "$TMP_CHECK"
cmp -s "$TMP_CHECK" "$TARGET_ESP_IMG" || { echo "error: EFI image payload mismatch" >&2; exit 4; }
mcopy -i "$ESP_IMG" ::/system.img "$TMP_CHECK"
cmp -s "$TMP_CHECK" "$TARGET_SYSTEM_IMG" || { echo "error: system image payload mismatch" >&2; exit 4; }
mcopy -i "$ESP_IMG" ::/install.sha256 "$TMP_CHECK"
cmp -s "$TMP_CHECK" "$INSTALL_SUMS" || { echo "error: install checksum manifest mismatch" >&2; exit 4; }
rm -f "$TMP_CHECK"
trap - EXIT

cp "$OUT/esp.spf" "$OUT/system.spf" "$TARGET_ESP_IMG" "$TARGET_SYSTEM_IMG" "$INSTALL_SUMS" "$ISO_ROOT/packages/"
cat > "$ISO_ROOT/README.txt" <<'EOF'
SentinelOS V.4 [Beta] installer media

This ISO contains a UEFI El Torito boot image and Sentinel Package File
payloads under /packages. SPF files are package archives, not disk images.

WARNING: Beta software. Back up important data before installing.
The installer can create GPT partitions and format the EFI partition as FAT32
and the system partition with a minimal ext4 filesystem. OS file deployment,
installed boot configuration, and end-to-end installed-system boot are not yet
implemented. Booting this media does not by itself perform a complete OS installation.
EOF
(
  cd "$ISO_ROOT"
  sha256sum boot/esp.img packages/esp.spf packages/system.spf packages/esp.img packages/system.img packages/install.sha256 README.txt > SHA256SUMS
)
xorriso -as mkisofs \
  -iso-level 3 -full-iso9660-filenames -R -J -V SENTINELOS_V4_BETA \
  -o "$ISO" \
  -eltorito-alt-boot -e boot/esp.img -no-emul-boot -isohybrid-gpt-basdat \
  "$ISO_ROOT"
xorriso -indev "$ISO" -report_el_torito plain
# Write a portable checksum containing only the ISO's filename, not this
# machine's absolute build path.
(
  cd "$OUT"
  sha256sum "$(basename "$ISO")" > "$(basename "$ISO").sha256"
)
echo "Created $ISO"
echo "ISO SHA-256: $(cut -d ' ' -f 1 "$ISO.sha256")"
