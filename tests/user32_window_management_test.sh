#!/usr/bin/env bash
# User32 window management source-contract checks (Agent 4: Desktop/Explorer/System Apps).
# Verifies window closing, welcome card suppression, and child control input routing.
set -euo pipefail

ROOT="${1:-.}"
USER32="$ROOT/user/user32.c"
FILEEXPLORER="$ROOT/user/fileexplorer.c"

fail() { echo "FAIL: $*" >&2; exit 1; }
require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}

# --- WM_CLOSE handling: X button and child control both route to u32_close_window_now ---
require "$USER32" 'u32_close_window_now' "user32 has close window routine"
require "$USER32" 'SendMessageA\(\(void \*\)1, 0x0010u' "WM_CLOSE sent on close"

# --- Welcome card suppression: u32_suppress_desktop_card checked in compose() ---
require "$USER32" '!u32_suppress_desktop_card && u32_damage_intersects' "welcome card suppressed when flag is set"
require "$USER32" 'u32_suppress_desktop_card = 1' "suppression flag set in DestroyWindow"

# --- Child controls only receive events for their windows: parent filter in on_click ---
require "$USER32" 'cc->parent!=\(void \*)1' "child controls filtered by parent HWND"

# --- WM_COMMAND routed to parent with child ID and handle ---
require "$USER32" '0x0111u,clicked_child->id' "WM_COMMAND sent with child control ID"

# --- fileexplorer.c has no unused variable warning sources ---
! grep -Eq 'int line=\(msg\.y-\(g_win_y\+44\)\)/16' "$FILEEXPLORER" || {
  echo "FAIL: fileexplorer.c still has unused 'line' variable" >&2
  exit 1
}
echo "PASS: fileexplorer.c has no unused line variable"

# --- Cursor: exactly one component owns cursor drawing ---
# cursor_save_draw must exist (owned by cursor_repaint_transition).
require "$USER32" 'cursor_save_draw' "cursor_save_draw exists"
# cursor_save_draw must NOT be called from within compose() (the compose
# function body ends where compose_region begins). Direct drawing from compose
# caused duplicate save-under cycles and drag trails.
if sed -n '/^static void compose(void) {/,/^static void compose_region/p' "$USER32" | grep -Eq 'cursor_save_draw\(g_mx'; then
  echo "FAIL: compose() still draws the cursor directly (double draw with cursor_repaint_transition)" >&2
  exit 1
fi
echo "PASS: compose() no longer draws the cursor directly"
# cursor_repaint_transition must exist and own cursor repaint on input.
require "$USER32" 'cursor_repaint_transition' "cursor_repaint_transition owns cursor repaint"
# cursor_save_draw must be called from cursor_repaint_transition (single owner).
require "$USER32" 'cursor_repaint_transition.*cursor_save_draw|cursor_save_draw\(x,y\)' "cursor_repaint_transition calls cursor_save_draw"

# --- Drag: old+new window bounds are repainted on drag motion ---
# The drag handler repaints the union of old and new window bounds.
require "$USER32" 'compose_region\(x0,y0,x1,y1\)' "drag motion repaints union of old+new bounds"
# Drag state is reset on button-up and button-down.
require "$USER32" 'g_drag = 0' "drag state is cleared"

# --- Start menu: open/close toggles and redraws ---
# The Start button toggles g_menu.
require "$USER32" 'g_menu=!g_menu' "Start button toggles g_menu"
# Menu close paths repaint the damaged region (no stale menu left on screen).
require "$USER32" 'compose_region\(0,my,\(int\)g_fw,\(int\)g_fh\)' "menu close repaints menu region"
# g_menu is reset to 0 in menu close fallbacks.
require "$USER32" 'g_menu=0;g_search_focus=0;g_start_hover=-1;u32_taskbar_dirty=1;' "menu state flags are reset on close"

echo "User32 window management checks passed."
