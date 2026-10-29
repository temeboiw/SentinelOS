#!/usr/bin/env bash
# Master a UEFI-bootable hybrid ISO from built installer staging data.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
STAGE="$HERE/staging"
INSTALLER_ESP="$STAGE/installer-esp"
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

if [[ "${SECURE_BOOT:-0}" == "1" ]]; then
  : "${SECURE_BOOT_CERT:?set SECURE_BOOT_CERT to the enrolled signing certificate when making Secure Boot media}"
  need sbverify
  for f in "$INSTALLER_ESP/EFI/BOOT/BOOTX64.EFI" \
           "$INSTALLER_ESP/EFI/magic/bootuni.efi" \
           "$INSTALLER_ESP/EFI/magic/loadufi.efi"; do
    sbverify --cert "$SECURE_BOOT_CERT" "$f" >/dev/null || { echo "error: unsigned or invalid EFI stage: $f" >&2; exit 4; }
  done
fi

[[ -s "$STAGE/esp/EFI/BOOT/BOOTX64.EFI" ]] || { echo "error: staged EFI/BOOT/BOOTX64.EFI missing; run build-installer.sh first" >&2; exit 3; }
[[ -s "$INSTALLER_ESP/magic/System32/instlr.exe" && -s "$INSTALLER_ESP/magic/System32/smss.exe" ]] || { echo "error: installer startup files missing from dedicated installer ESP" >&2; exit 3; }
[[ -s "$OUT/esp.spf" && -s "$OUT/system.spf" ]] || { echo "error: SPF packages missing; run build-installer.sh first" >&2; exit 3; }
if [[ "$(realpath -e "$STAGE/system")" == "$(realpath -e "$INSTALLER_ESP")" || -e "$STAGE/system/magic/System32/instlr.exe" || -e "$STAGE/system/magic/System32/screen.exe" ]]; then
  echo "error: installer staging cannot be used as the deployable OS payload" >&2
  exit 4
fi
if find "$STAGE/system" -type f \( -iname instlr.exe -o -iname screen.exe \) -print -quit | grep -q .; then
  echo "error: installer-only executable found in the deployable OS tree" >&2
  exit 4
fi
# Build the actual installable partition images on the build host. The native
# installer only writes these verified raw images; it does not need zlib/tar
# support or a host shell at install time.
need truncate
need mkfs.ext4
need debugfs
need mcopy
need mmd
TARGET_ESP_IMG="$OUT/esp.img"
TARGET_SYSTEM_IMG="$OUT/system.img"
PACKED_ESP_IMG="$OUT/esp.img.zblk"
PACKED_SYSTEM_IMG="$OUT/system.img.zblk"
INSTALL_SUMS="$OUT/install.sha256"
ESP_IMAGE_SIZE_MIB="${INSTALL_ESP_SIZE_MIB:-128}"
SYSTEM_IMAGE_SIZE_MIB="${INSTALL_SYSTEM_SIZE_MIB:-512}"
[[ "$ESP_IMAGE_SIZE_MIB" == "128" ]] || { echo "error: INSTALL_ESP_SIZE_MIB must be exactly 128 to match the native installer GPT layout" >&2; exit 2; }
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
mkfs.ext4 -F -q -L SENTINELOS -O extents,^64bit,^metadata_csum,^dir_index,^has_journal -E lazy_itable_init=0 -d "$STAGE/system" "$TARGET_SYSTEM_IMG"
# Verify critical files directly from the image before it can be embedded.
for f in magic/System32/hal.dll magic/System32/krnl.exe magic/System32/smss.exe magic/System32/bootvid.exe magic/System32/sentlogon.exe; do
  debugfs -R "stat /$f" "$TARGET_SYSTEM_IMG" 2>/dev/null | grep -q 'Type:' || { echo "error: system image is missing /$f" >&2; exit 4; }
done
if debugfs -R 'stat /magic/System32/instlr.exe' "$TARGET_SYSTEM_IMG" 2>/dev/null | grep -q 'Type:'; then
  echo "error: installer executable is present in the installed system image" >&2
  exit 4
fi
if debugfs -R 'stat /magic/System32/screen.exe' "$TARGET_SYSTEM_IMG" 2>/dev/null | grep -q 'Type:'; then
  echo "error: installer-only screen executable is present in the installed system image" >&2
  exit 4
fi
debugfs -R "dump /magic/System32/smss.exe $OUT/installed-smss.check" "$TARGET_SYSTEM_IMG" >/dev/null 2>&1
if ! cmp -s "$OUT/installed-smss.check" "$ROOT/build/rootfs/magic/System32/smss.exe"; then
  rm -f "$OUT/installed-smss.check"
  echo "error: installed system image does not contain the main OS session manager" >&2
  exit 4
fi
rm -f "$OUT/installed-smss.check"
printf '%s\n%s\n' "$(sha256sum "$TARGET_ESP_IMG" | cut -d ' ' -f1)" "$(sha256sum "$TARGET_SYSTEM_IMG" | cut -d ' ' -f1)" > "$INSTALL_SUMS"
python3 "$HERE/pack_image.py" pack "$TARGET_ESP_IMG" "$PACKED_ESP_IMG"
python3 "$HERE/pack_image.py" pack "$TARGET_SYSTEM_IMG" "$PACKED_SYSTEM_IMG"
# Store package hashes separately so the kernel can verify exact staged bytes before formatting any disk.
printf '%s\n%s\n' "$(sha256sum "$OUT/esp.spf" | cut -d ' ' -f1)" "$(sha256sum "$OUT/system.spf" | cut -d ' ' -f1)" > "$OUT/packages.sha256"
if [[ "$ESP_SIZE_MIB" == "auto" ]]; then
  # Zero-block packed install images are embedded on the FAT ESP and expanded
  # by the UEFI loader. Leave space for these plus staged files and metadata.
  # 16 MiB for FAT metadata/cluster slack. Do not silently truncate the image.
  required_bytes="$(du -sb "$INSTALLER_ESP" | awk '{print $1}')"
  required_bytes=$((required_bytes + $(stat -c %s "$PACKED_ESP_IMG") + $(stat -c %s "$PACKED_SYSTEM_IMG") + $(stat -c %s "$INSTALL_SUMS")))
  ESP_SIZE_MIB=$(((required_bytes * 125 / 100 + 16 * 1024 * 1024 + 1024 * 1024 - 1) / (1024 * 1024)))
  (( ESP_SIZE_MIB < 128 )) && ESP_SIZE_MIB=128
  echo "Auto-sized installer ESP: ${ESP_SIZE_MIB} MiB (staged files and both filesystem images)"
fi
[[ "$ESP_SIZE_MIB" =~ ^[0-9]+$ ]] && (( ESP_SIZE_MIB >= 32 && ESP_SIZE_MIB <= 1024 )) || { echo "error: required ESP exceeds supported 32..1024 MiB range; set ESP_SIZE_MIB explicitly or reduce the payload" >&2; exit 2; }

rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/boot" "$ISO_ROOT/packages" "$OUT"
dd if=/dev/zero of="$ESP_IMG" bs=1M count="$ESP_SIZE_MIB" status=none
mkfs.vfat -F 32 -n SENTINEL "$ESP_IMG" >/dev/null
mmd -i "$ESP_IMG" ::/EFI

# Copy every staged EFI vendor directory and any additional ESP-root files.
shopt -s nullglob dotglob
for entry in "$INSTALLER_ESP/EFI"/*; do
  mcopy -s -i "$ESP_IMG" "$entry" ::/EFI/
done
for entry in "$INSTALLER_ESP"/*; do
  [[ "${entry##*/}" == "EFI" ]] && continue
  mcopy -s -i "$ESP_IMG" "$entry" ::/
done
shopt -u nullglob dotglob

# The UEFI loader can only read files on its own FAT ESP. Embed both packed
# filesystem images there for fast boot-time expansion into installer modules.
# The ISO-level SPF copies remain for build validation and diagnostics.
mcopy -i "$ESP_IMG" "$PACKED_ESP_IMG" ::/esp.img.zblk
mcopy -i "$ESP_IMG" "$PACKED_SYSTEM_IMG" ::/system.img.zblk
mcopy -i "$ESP_IMG" "$INSTALL_SUMS" ::/install.sha256

# Fail early if the fallback loader and both install images made it into the FAT image.
TMP_CHECK="$(mktemp)"
trap 'rm -f "$TMP_CHECK"' EXIT
mcopy -i "$ESP_IMG" ::/EFI/BOOT/BOOTX64.EFI "$TMP_CHECK"
[[ -s "$TMP_CHECK" ]] || { echo "error: EFI image does not contain BOOTX64.EFI" >&2; exit 4; }
mcopy -i "$ESP_IMG" ::/esp.img.zblk "$TMP_CHECK"
cmp -s "$TMP_CHECK" "$PACKED_ESP_IMG" || { echo "error: packed EFI image payload mismatch" >&2; exit 4; }
mcopy -i "$ESP_IMG" ::/system.img.zblk "$TMP_CHECK"
cmp -s "$TMP_CHECK" "$PACKED_SYSTEM_IMG" || { echo "error: packed system image payload mismatch" >&2; exit 4; }
mcopy -i "$ESP_IMG" ::/install.sha256 "$TMP_CHECK"
cmp -s "$TMP_CHECK" "$INSTALL_SUMS" || { echo "error: install checksum manifest mismatch" >&2; exit 4; }
rm -f "$TMP_CHECK"
trap - EXIT

cp "$OUT/esp.spf" "$OUT/system.spf" "$TARGET_ESP_IMG" "$TARGET_SYSTEM_IMG" "$INSTALL_SUMS" "$ISO_ROOT/packages/"
cat > "$ISO_ROOT/README.txt" <<'EOF'
SentinelOS V.4 [Beta] installer media

This ISO boots the graphical SentinelOS Setup app. It carries prebuilt FAT32
and ext4 filesystem images plus SPF build/validation packages under /packages.

WARNING: Beta software. Back up important data before installing. The graphical
installer creates GPT and copies checksum-verified FAT32 EFI and ext4 system
images to the selected VirtIO disk. The system image is 512 MiB by default; the
target disk must be at least about 640 MiB. The installed ext4 filesystem initially
retains that size even if the partition is larger. The selected
disk is erased. A real UEFI VM install and reboot test is still required.
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
