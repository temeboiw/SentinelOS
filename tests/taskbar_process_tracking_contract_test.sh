#!/usr/bin/env bash
# Taskbar process tracking source-contract checks (Agent 4).
# Verifies the taskbar displays real running applications with no fake/static entries:
#   - SYS_WmRestore (19) has a kernel handler wired to wm_restore_pid + compose.
#   - smss launches explorer.exe (the desktop shell that hosts the taskbar).
#   - CreateWindowExA registers visible windows via SYS_WmRegister.
#   - Process exit unregisters the window and recomposes (wm_unregister + compose).
#   - draw_taskbar enumerates live windows via SYS_WmQuery (no fake entries).
set -euo pipefail

ROOT="${1:-.}"
USERH="$ROOT/kernel/user.h"
USER32="$ROOT/user/user32.c"
SMSS="$ROOT/user/smss.c"
TASKBAR="$ROOT/user/taskbar.c"

fail() { echo "FAIL: $*" >&2; exit 1; }
require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}
require_absent() {
  local file="$1" pattern="$2" label="$3"
  if grep -Eq -- "$pattern" "$file"; then
    echo "FAIL: $label ($file unexpectedly matches $pattern)" >&2
    exit 1
  fi
  echo "PASS: $label"
}

# --- 1. SYS_WmRestore (19) handler exists, calls wm_restore_pid + compose ---
require "$USERH" 'case SYS_WmRestore:' "kernel has a SYS_WmRestore case"
require "$USERH" 'wm_restore_pid\(pid\)' "SYS_WmRestore calls wm_restore_pid"
require "$USERH" 'wm_shared_compose\(0, 0, wm_shared_width, wm_shared_height\)' "SYS_WmRestore forces a full recompose"
require "$USERH" 'f->rax = 1;' "SYS_WmRestore returns success without falling through"

# --- 2. smss launches explorer.exe (protected shell; smss is system_app=1) ---
require "$SMSS" 'starting explorer\.exe' "smss starts explorer.exe"
require "$SMSS" 'explorer_pid = sy3\(SYS_Create' "smss launches explorer.exe via NtCreateProcess"

# --- 3. CreateWindowExA registers visible windows via SYS_WmReg (enum alias for 15) ---
require "$USER32" 'SYS_WmReg' "CreateWindowExA registers visible windows"

# --- 4. Process exit unregisters window and recomposes ---
require "$USERH" 'wm_unregister\(dead_pid\)' "process exit unregisters the dead pid's window"
require "$USERH" 'wm_unregister\(pid\)' "exit handler calls wm_unregister"

# --- 5. draw_taskbar enumerates live windows via SYS_WmQuery (no fake entries) ---
require "$USER32" 'SYS_WmQuery' "draw_taskbar enumerates windows via SYS_WmQuery"

# --- 6. explorer refreshes the taskbar after launching an application ---
EXPLORER="$ROOT/user/explorer.c"
require "$EXPLORER" 'NtCreateProcess\(name\)' "explorer launches applications via NtCreateProcess"
require "$EXPLORER" 'if \(pBar\) pBar\(\);' "explorer refreshes taskbar after app launch"

echo "Taskbar process tracking contract checks passed."
