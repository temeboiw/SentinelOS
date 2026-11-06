#!/usr/bin/env bash
# Cursor rendering source-contract checks (Agent 3: Window Manager, Cursors, Start Menu).
# Verifies cursor dimensions, save-under buffers, pixel format, and single-ownership
# of cursor drawing (cursor_repaint_transition owns it; compose() must not draw it).
set -euo pipefail

ROOT="${1:-.}"
USER32="$ROOT/user/user32.c"

fail() { echo "FAIL: $*" >&2; exit 1; }
require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}
refute() {
  local file="$1" pattern="$2" label="$3"
  if grep -Eq -- "$pattern" "$file"; then
    echo "FAIL: $label ($file unexpectedly matches $pattern)" >&2
    exit 1
  fi
  echo "PASS: $label"
}

# --- Cursor dimensions: 16x16 shape table ---
require "$USER32" 'u32_cursor_shape\[16\]\[17\]' "cursor shape table is 16x16"

# --- Save-under buffers for fast cursor motion ---
require "$USER32" 'static u32 cur_under\[16\*16\]' "cur_under save-under buffer exists"
require "$USER32" 'static u32 cur_paint\[16\*16\]' "cur_paint save-under buffer exists"

# --- Cursor pixel format: opaque '#'/'w' and transparent '.' ---
require "$USER32" "if \(p=='#'\) val=0xFF000000u;" "cursor '#' is opaque black"
require "$USER32" "else if \(p=='w'\) val=COL_WHITE;" "cursor 'w' is opaque white"
# Transparent pixels ('.') are skipped: 'if (!val) continue;'
require "$USER32" 'if (\(!val\)) continue;' "cursor transparent pixels are skipped"

# --- Cursor hotspot: painted at (x,y) with shape origin at top-left ---
require "$USER32" 'cursor_save_draw\(int x, int y\)' "cursor drawn at explicit x,y position"

# --- Exactly one component owns cursor drawing: cursor_repaint_transition ---
# cursor_save_draw must exist.
require "$USER32" 'static void cursor_save_draw\(int x, int y\)' "cursor_save_draw exists"
# cursor_save_draw must be called from cursor_repaint_transition (the input path).
require "$USER32" 'cursor_repaint_transition.*cursor_save_draw|cursor_save_draw\(x,y\)' "cursor_repaint_transition calls cursor_save_draw"
# compose() must NOT directly draw the cursor (caused duplicate save-under cycles/trails).
if sed -n '/^static void compose(void) {/,/^static void compose_region/p' "$USER32" | grep -Eq 'cursor_save_draw\(g_mx'; then
  echo "FAIL: compose() still draws the cursor directly (double draw with cursor_repaint_transition)" >&2
  exit 1
fi
echo "PASS: compose() no longer draws the cursor directly"

echo "Cursor draw checks passed."
