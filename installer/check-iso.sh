#!/usr/bin/env bash
# Smoke-check a mastered SentinelOS installer ISO and its embedded payloads.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ISO="${1:-$HERE/artifacts/SentinelOS-V4-BETA.iso}"
ISO="$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")"
SUM="${ISO}.sha256"
need() { command -v "$1" >/dev/null 2>&1 || { echo "error: required tool not found: $1" >&2; exit 2; }; }
need xorriso
need sha256sum
need mcopy
need python3
[[ -s "$ISO" ]] || { echo "error: ISO not found or empty: $ISO" >&2; exit 3; }
[[ -s "$SUM" ]] || { echo "error: ISO checksum file missing: $SUM" >&2; exit 3; }

echo "==> Verify ISO SHA-256"
(cd "$(dirname "$ISO")" && sha256sum -c "$(basename "$SUM")")

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "==> Inspect El Torito boot catalog"
if ! xorriso -indev "$ISO" -report_el_torito plain >"$TMP/boot-report.txt" 2>&1; then
  cat "$TMP/boot-report.txt" >&2
  exit 4
fi
cat "$TMP/boot-report.txt"
grep -Eiq 'UEFI|0xEF|EFI' "$TMP/boot-report.txt" || {
  echo "error: no UEFI El Torito boot entry detected" >&2
  exit 4
}

echo "==> Extract boot ESP, deployable partition images, and SPF validation packages"
if ! xorriso -osirrox on -indev "$ISO" \
    -extract /boot/esp.img "$TMP/boot-esp.img" \
    -extract /packages/esp.img "$TMP/target-esp.img" \
    -extract /packages/system.img "$TMP/target-system.img" \
    -extract /packages/install.sha256 "$TMP/install.sha256" \
    -extract /packages/esp.spf "$TMP/esp.spf" \
    -extract /packages/system.spf "$TMP/system.spf" >"$TMP/extract.log" 2>&1; then
  cat "$TMP/extract.log" >&2
  exit 5
fi
[[ -s "$TMP/boot-esp.img" && -s "$TMP/target-esp.img" && -s "$TMP/target-system.img" && -s "$TMP/install.sha256" ]] || {
  echo "error: ISO is missing an installer image or checksum manifest" >&2
  exit 5
}
mcopy -i "$TMP/boot-esp.img" ::/EFI/BOOT/BOOTX64.EFI "$TMP/BOOTX64.EFI"
[[ -s "$TMP/BOOTX64.EFI" ]] || { echo "error: boot image has no BOOTX64.EFI" >&2; exit 6; }
mcopy -i "$TMP/boot-esp.img" ::/esp.img "$TMP/embedded-esp.img"
cmp -s "$TMP/target-esp.img" "$TMP/embedded-esp.img" || { echo "error: embedded EFI image mismatch" >&2; exit 6; }
mcopy -i "$TMP/boot-esp.img" ::/system.img "$TMP/embedded-system.img"
cmp -s "$TMP/target-system.img" "$TMP/embedded-system.img" || { echo "error: embedded system image mismatch" >&2; exit 6; }
mcopy -i "$TMP/boot-esp.img" ::/install.sha256 "$TMP/embedded-install.sha256"
cmp -s "$TMP/install.sha256" "$TMP/embedded-install.sha256" || { echo "error: embedded install checksum manifest mismatch" >&2; exit 6; }
[[ "$(sed -n "1p" "$TMP/install.sha256")" == "$(sha256sum "$TMP/target-esp.img" | cut -d " " -f1)" ]] || { echo "error: esp.img SHA-256 mismatch" >&2; exit 6; }
[[ "$(sed -n "2p" "$TMP/install.sha256")" == "$(sha256sum "$TMP/target-system.img" | cut -d " " -f1)" ]] || { echo "error: system.img SHA-256 mismatch" >&2; exit 6; }
python3 "$HERE/make_spf.py" verify "$TMP/esp.spf" --kind esp --require-boot-layout
python3 "$HERE/make_spf.py" verify "$TMP/system.spf" --kind system --require-boot-layout
echo "PASS: ISO checksum, UEFI boot entry, installer images, embedded image hashes, and SPF validation packages verified."
echo "Note: this smoke test does not replace booting the ISO in a UEFI virtual machine."
