#!/usr/bin/env bash
# Master a UEFI-bootable hybrid ISO from built installer staging data.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
STAGE="$HERE/staging"
OUT="$HERE/artifacts"
ISO_ROOT="$HERE/iso-root"
ISO="$OUT/SentinelOS-V4-BETA.iso"
ESP_IMG="$ISO_ROOT/boot/esp.img"
ESP_SIZE_MIB="${ESP_SIZE_MIB:-128}"

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: required tool not found: $1" >&2; exit 2; }; }
need xorriso
need dd
need mkfs.vfat
need mcopy
need mmd
need sha256sum

[[ -s "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" ]] || { echo "error: staged EFI/BOOT/BOOTX64.EFI missing; run build-installer.sh first" >&2; exit 3; }
[[ -s "$OUT/esp.spf" && -s "$OUT/system.spf" ]] || { echo "error: SPF packages missing; run build-installer.sh first" >&2; exit 3; }
[[ "$ESP_SIZE_MIB" =~ ^[0-9]+$ ]] && (( ESP_SIZE_MIB >= 32 && ESP_SIZE_MIB <= 1024 )) || { echo "error: ESP_SIZE_MIB must be 32..1024" >&2; exit 2; }

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

# Fail early if the fallback loader did not make it into the FAT image.
TMP_CHECK="$(mktemp)"
trap 'rm -f "$TMP_CHECK"' EXIT
mcopy -i "$ESP_IMG" ::/EFI/BOOT/BOOTX64.EFI "$TMP_CHECK"
[[ -s "$TMP_CHECK" ]] || { echo "error: EFI image does not contain BOOTX64.EFI" >&2; exit 4; }
rm -f "$TMP_CHECK"
trap - EXIT

cp "$OUT/esp.spf" "$OUT/system.spf" "$ISO_ROOT/packages/"
cat > "$ISO_ROOT/README.txt" <<'EOF'
SentinelOS V.4 [Beta] installer media

This ISO contains a UEFI El Torito boot image and Sentinel Package File
payloads under /packages. SPF files are package archives, not disk images.

WARNING: Beta software. Back up important data before installing.
The current installer UI does not yet partition or format disks. Booting this
media does not by itself perform a complete OS installation.
EOF
(
  cd "$ISO_ROOT"
  sha256sum boot/esp.img packages/esp.spf packages/system.spf README.txt > SHA256SUMS
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
