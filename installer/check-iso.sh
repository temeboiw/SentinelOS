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

echo "==> Extract boot image and package payloads for validation"
if ! xorriso -osirrox on -indev "$ISO" \
    -extract /boot/esp.img "$TMP/esp.img" \
    -extract /packages/esp.spf "$TMP/esp.spf" \
    -extract /packages/system.spf "$TMP/system.spf" \
    -extract /packages/packages.sha256 "$TMP/packages.sha256" >"$TMP/extract.log" 2>&1; then
  cat "$TMP/extract.log" >&2
  exit 5
fi
[[ -s "$TMP/esp.img" && -s "$TMP/esp.spf" && -s "$TMP/system.spf" ]] || {
  echo "error: ISO is missing the EFI image or one of the SPF packages" >&2
  exit 5
}
mcopy -i "$TMP/esp.img" ::/EFI/BOOT/BOOTX64.EFI "$TMP/BOOTX64.EFI"
[[ -s "$TMP/BOOTX64.EFI" ]] || { echo "error: boot image has no BOOTX64.EFI" >&2; exit 6; }
mcopy -i "$TMP/esp.img" ::/packages.sha256 "$TMP/esp-packages.sha256"
cmp -s "$TMP/packages.sha256" "$TMP/esp-packages.sha256" || { echo "error: ESP and ISO package checksum manifests differ" >&2; exit 6; }
[[ "$(sed -n "1p" "$TMP/packages.sha256")" == "$(sha256sum "$TMP/esp.spf" | cut -d " " -f1)" ]] || { echo "error: esp.spf SHA-256 mismatch" >&2; exit 6; }
[[ "$(sed -n "2p" "$TMP/packages.sha256")" == "$(sha256sum "$TMP/system.spf" | cut -d " " -f1)" ]] || { echo "error: system.spf SHA-256 mismatch" >&2; exit 6; }
python3 "$HERE/make_spf.py" verify "$TMP/esp.spf"
python3 "$HERE/make_spf.py" verify "$TMP/system.spf"
echo "PASS: ISO checksum, UEFI boot entry, fallback loader, both SPF packages, and their embedded SHA-256 manifest verified."
echo "Note: this smoke test does not replace booting the ISO in a UEFI virtual machine."
