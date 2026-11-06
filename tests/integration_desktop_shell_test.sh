#!/usr/bin/env bash
# Cross-agent integration check: the desktop shell (explorer.c, Agent 4) must
# resolve USER32 window/drawing functions through the kernel's GetProcAddress
# syscall, launch applications through NtCreateProcess, and the kernel must
# expose those syscalls.
#
# Source-contract gate only; does not run the desktop shell.
set -euo pipefail

ROOT="${1:-.}"
EXPLORER="$ROOT/user/explorer.c"
USER32="$ROOT/user/user32.c"
KUSER="$ROOT/kernel/user.h"
NTDLL="$ROOT/user/ntdll_user.h"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }
require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}

# --- Kernel exposes the syscalls explorer depends on -----------------------
require "$KUSER" '^#define SYS_NtCreateProcess[[:space:]]+4$' \
    "kernel assigns create-process syscall ID 4"
require "$KUSER" '^#define SYS_NtGetProcAddress[[:space:]]+9$' \
    "kernel assigns get-proc-address syscall ID 9"
require "$KUSER" '^#define SYS_NtWriteFile[[:space:]]+1$' \
    "kernel assigns write syscall ID 1"
require "$KUSER" 'case SYS_NtCreateProcess:' \
    "kernel dispatches create-process syscall"
require "$KUSER" 'case SYS_NtGetProcAddress:' \
    "kernel dispatches get-proc-address syscall"

# ntdll_user.h mirrors the same syscall IDs for usermode callers.
require "$NTDLL" 'SYS_CreateProcess[[:space:]]*=[[:space:]]*4' \
    "ntdll create-process ID is 4"
require "$NTDLL" 'SYS_GetProcAddress[[:space:]]*=[[:space:]]*9' \
    "ntdll get-proc-address ID is 9"
require "$NTDLL" 'SYS_Write[[:space:]]*=[[:space:]]*1' \
    "ntdll write ID is 1"

# --- explorer.c resolves USER32 functions via NtGetProcAddress -------------
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"CreateWindowExA"\)' \
    "explorer resolves CreateWindowExA from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"ShowWindow"\)' \
    "explorer resolves ShowWindow from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"GetMessageA"\)' \
    "explorer resolves GetMessageA from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"TranslateMessage"\)' \
    "explorer resolves TranslateMessage from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"DispatchMessageA"\)' \
    "explorer resolves DispatchMessageA from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"TextOutA"\)' \
    "explorer resolves TextOutA from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"DrawTaskbar"\)' \
    "explorer resolves DrawTaskbar from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"SetTimer"\)' \
    "explorer resolves SetTimer from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"UpdateWindow"\)' \
    "explorer resolves UpdateWindow from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"BeginRedrawBatch"\)' \
    "explorer resolves BeginRedrawBatch from user32.dll"
require "$EXPLORER" 'NtGetProcAddress\(u32n,\s*"EndRedrawBatch"\)' \
    "explorer resolves EndRedrawBatch from user32.dll"

# explorer launches apps via NtCreateProcess.
require "$EXPLORER" 'NtCreateProcess\(name\)' \
    "explorer launches applications via NtCreateProcess"

# --- user32.c exports every function explorer resolves ---------------------
# These dllexport declarations are what make NtGetProcAddress succeed at runtime.
# Some attributes are on the same line as the declaration, some on the line
# before -- check both patterns.
require "$USER32" '__attribute__\(\(dllexport\)\)' \
    "user32.c uses __attribute__((dllexport))"
for fn in CreateWindowExA ShowWindow GetMessageA TranslateMessage DispatchMessageA \
          TextOutA DrawTaskbar SetTimer UpdateWindow BeginRedrawBatch EndRedrawBatch; do
  require "$USER32" "$fn[[:space:]]*\\(" \
      "user32.c defines $fn for the desktop shell"
done

echo "Cross-agent desktop shell integration checks passed."
