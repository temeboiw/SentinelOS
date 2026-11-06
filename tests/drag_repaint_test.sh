#!/usr/bin/env bash
# Drag-repaint source-contract checks (Agent 3: Window Manager, Cursors, Start Menu).
# Verifies window-drag repainting: invalidation, damage rects, old-position
# clearing, and compositor redraw after move.
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

# --- Drag motion repaints the union of old and new window bounds ---
# compose_region(x0,y0,x1,y1) with the union rect computed from old_x/old_y and nx/ny.
require "$USER32" 'compose_region\(x0,y0,x1,y1\)' "drag motion repaints union of old+new bounds"

# --- Old position clearing: wallpaper is restored into the desktop layer
# --- on the GPU scanout path so dragged-away positions show the background ---
require "$USER32" 'u32_desktop_layer\[\(u64\)y\*g_fw\+\(u64\)x\] =' "wallpaper restored into desktop layer clip region"
require "$USER32" 'u32_aero_bg_cache\[\(u64\)y\*g_fw\+\(u64\)x\]' "wallpaper source is the cached aero background"

# --- Drag state is reset on button-down and button-up ---
require "$USER32" 'g_drag = 0' "drag state is cleared"

# --- Compositor redraw after move: SYS_WmRect is called with new geometry ---
require "$USER32" 'SYS_WmRect' "window geometry registered with kernel on move"

echo "Drag repaint checks passed."
