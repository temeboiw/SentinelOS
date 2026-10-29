#!/usr/bin/env bash
# Keep legacy yield and file-close callers aligned with syscall dispatch.
set -euo pipefail

ROOT="${1:-.}"
KUSER="$ROOT/kernel/user.h"
require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}

require "$KUSER" '^#define SYS_NtYield[[:space:]]+21$' "kernel preserves legacy yield ID 21"
require "$KUSER" '^#define SYS_NtCloseFile[[:space:]]+22$' "kernel assigns file close ID 22"
require "$KUSER" '^#define SYS_WmGetDirty[[:space:]]+53$' "kernel assigns window dirty query ID 53"
require "$KUSER" 'case SYS_NtYield:' "yield dispatch uses its named contract"
require "$KUSER" 'case SYS_NtCloseFile:' "file close dispatch uses its named contract"
require "$KUSER" 'case SYS_WmGetDirty:' "window dirty dispatch uses its named contract"

require "$ROOT/user/ntdll_user.h" 'SYS_Yield[[:space:]]*=[[:space:]]*21' "ntdll yield wrapper uses ID 21"
require "$ROOT/user/smss.c" 'SYS_Yield[[:space:]]*=[[:space:]]*21' "SMSS yield caller uses ID 21"
require "$ROOT/user/bootvid.c" 'sy3\(SYS_Yield,' "boot video yields through the shared ID"
require "$ROOT/user/sentlogon.c" 'sy3\(SYS_Yield,' "logon yields through the shared ID"
require "$ROOT/user/kernel32.c" 'sy3\(21[[:space:]]*,' "kernel32 yield calls use ID 21"
require "$ROOT/user/kernel32.c" 'SYS_CloseFile[[:space:]]*=[[:space:]]*22' "kernel32 close caller uses ID 22"
require "$ROOT/user/msiexec.c" 'SYS_NtYield[[:space:]]*=[[:space:]]*21,[[:space:]]*SYS_CloseFile[[:space:]]*=[[:space:]]*22' "msiexec yield and close IDs match dispatch"
require "$ROOT/user/fileexplorer.c" 'SYS_CloseFile[[:space:]]*=[[:space:]]*22' "File Explorer close caller uses ID 22"
require "$ROOT/user/font_ttf.h" 'sy3\(22[[:space:]]*,[[:space:]]*h,' "TrueType loader closes its file with ID 22"
require "$ROOT/user/user32.c" 'SYS_WmGetDirty[[:space:]]*=[[:space:]]*53' "USER32 dirty query caller uses ID 53"
require "$ROOT/UserTestApps/maze.cpp" 'SYS_YIELD[[:space:]]*=[[:space:]]*21' "maze compatibility app retains yield ID 21"

echo "SentinelOS syscall ABI source-contract checks passed."
