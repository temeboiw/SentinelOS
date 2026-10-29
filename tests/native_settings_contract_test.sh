#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-.}"
APP="$ROOT/user/settings.c"
BUILD="$ROOT/scripts/build.sh"
IMAGE="$ROOT/build/rootfs/magic/System32/settings.exe"

fail() { echo "FAIL: $*" >&2; exit 1; }
require() { grep -Fq -- "$2" "$1" || fail "$3"; echo "PASS: $3"; }

for category in '"System"' '"Display"' '"Personalization"' '"Mouse & Input"' \
                '"Performance"' '"Storage"' '"Network & Internet"' \
                '"Applications"' '"Accounts & Users"' '"About SentinelOS"'; do
  require "$APP" "$category" "Settings includes $category category"
done
require "$APP" 'SYS_NtNetworkInfo=49' "network page uses the kernel network-info syscall"
require "$APP" 'NtQueryProcessList(g_processes,32)' "performance page queries the real process snapshot"
require "$APP" 'NtMapFramebuffer(&g_fb)' "display page queries the active framebuffer mode"
require "$APP" 'dynamic resolution changes are unsupported' "display page discloses unsupported mode changes"
require "$APP" 'Memory capacity and uptime APIs are not exposed' "system page does not invent memory or uptime values"
require "$APP" 'No authenticated user identity is exposed' "accounts page reports unavailable identity services"
require "$APP" 'msg.message==0x0010u' "Settings exits on normal window close"
require "$APP" '(cy-32)/32' "mouse hit testing maps the first sidebar row to the System page"
require "$BUILD" '"$ROOT/user/settings.c"' "build compiles the native Settings source"
require "$BUILD" '"$SYS32/settings.exe"' "build stages Settings in System32"
require "$ROOT/user/user32.c" 'static char settings_name[]="settings.exe"' "Start menu launches Settings through the shell process path"
[[ -s "$IMAGE" ]] || fail "settings.exe is missing from the generated OS root filesystem"
FORMAT="$(file -b "$IMAGE")"
[[ "$FORMAT" == *"PE32+"* && "$FORMAT" == *"x86-64"* ]] || fail "packaged Settings is not an x86-64 PE32+ executable: $FORMAT"
echo "PASS: native Settings executable is packaged as PE32+ x86-64"
