#!/usr/bin/env bash
# Automated tests for the regedit.exe + persistent registry implementation (V11 SOVEREIGN, task c373 Section 12).
# Source-contract checks: verifies the build wiring and source contracts required by the regedit task.
# These run on the host without needing to boot SentinelOS.
set -euo pipefail

ROOT="${1:-.}"
BUILD="$ROOT/scripts/build.ps1"
ADVAPI="$ROOT/user/advapi32.c"
REGEDIT="$ROOT/user/regedit.c"

require() {
  local file="$1" pattern="$2" label="$3"
  if ! grep -Eq -- "$pattern" "$file"; then
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  fi
  echo "PASS: $label"
}

require_file() {
  local file="$1" label="$2"
  if [[ ! -f "$file" ]]; then
    echo "FAIL: $label ($file missing)" >&2
    exit 1
  fi
  echo "PASS: $label"
}

# --- regedit.exe source exists and implements the required surface ---
require_file "$REGEDIT" "user/regedit.c exists"
require "$REGEDIT" 'void mainCRTStartup\(void\)' "regedit defines mainCRTStartup entry point"
require "$REGEDIT" 'gpa\("advapi32.dll"' "regedit resolves advapi32 via runtime gpa"
require "$REGEDIT" 'pRegOpenKeyExA' "regedit uses RegOpenKeyExA"
require "$REGEDIT" 'pRegCreateKeyExA' "regedit uses RegCreateKeyExA"
require "$REGEDIT" 'pRegDeleteKeyA' "regedit uses RegDeleteKeyA"
require "$REGEDIT" 'pRegDeleteTreeA' "regedit uses RegDeleteTreeA"
require "$REGEDIT" 'pRegDeleteValueA' "regedit uses RegDeleteValueA"
require "$REGEDIT" 'pRegSetValueExA' "regedit uses RegSetValueExA"
require "$REGEDIT" 'pRegEnumKeyExA' "regedit uses RegEnumKeyExA"
require "$REGEDIT" 'pRegEnumValueA' "regedit uses RegEnumValueA"
require "$REGEDIT" 'pRegQueryValueExA' "regedit uses RegQueryValueExA"
require "$REGEDIT" 'draw_screen' "regedit has draw_screen"
require "$REGEDIT" 'handle_main_key' "regedit has handle_main_key"
require "$REGEDIT" 'build_tree' "regedit has build_tree"
require "$REGEDIT" 'refresh_values' "regedit has refresh_values"

# --- build.ps1 wires regedit.exe into the system image ---
require_file "$BUILD" "scripts/build.ps1 exists"
require "$BUILD" 'Build-SimpleExe "regedit" "regedit.c"' "build.ps1 builds regedit.exe"
require "$BUILD" '"regedit\.exe"' "build.ps1 lists regedit.exe in requiredSystem32"
require "$BUILD" 'registry\.hive' "build.ps1 references registry.hive"
require "$BUILD" '4194304' "build.ps1 precreates a 4 MB registry.hive"

# --- build.ps1 exports every regedit-needed advapi32 A-variant ---
for api in RegOpenKeyExA RegCreateKeyExA RegDeleteKeyA RegDeleteTreeA RegDeleteValueA RegCloseKey RegFlushKey RegEnumKeyExA RegEnumValueA RegQueryValueExA RegSetValueExA RegQueryInfoKeyA RegCopyTreeA RegConnectRegistryA RegNotifyChangeKeyValue RegGetValueA; do
  require "$BUILD" "/export:$api" "build.ps1 exports advapi32.$api"
done

# --- advapi32.c implements (dllexport) each regedit-needed A-variant ---
for api in RegOpenKeyExA RegCreateKeyExA RegDeleteKeyA RegDeleteTreeA RegDeleteValueA RegCloseKey RegFlushKey RegEnumKeyExA RegEnumValueA RegQueryValueExA RegSetValueExA RegQueryInfoKeyA RegCopyTreeA RegConnectRegistryA RegNotifyChangeKeyValue RegGetValueA; do
  require "$ADVAPI" "__attribute__\(\(dllexport\)\) LONG $api\(" "advapi32.c exports $api"
done

echo "regedit tests passed."
