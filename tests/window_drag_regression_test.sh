#!/usr/bin/env bash
# Window drag regression tests (Agent 3: Window Manager, Cursors, Start Menu).
# Verifies that dragging windows repaints old+new bounds and that transparent
# window-layer pixels clear to background instead of leaving trails.
# Source-contract checks against user/user32.c.
set -euo pipefail
ROOT="${1:-.}"
USER32="$ROOT/user/user32.c"
fail() { echo "FAIL: $*" >&2; exit 1; }

# --- Drag repaints union of old and new window bounds ---
# The drag motion handler calls compose_region with x0,y0,x1,y1 covering both positions.
grep -Eq 'compose_region\(x0,y0,x1,y1\)' "$USER32" ||
  fail "drag motion does not repaint union of old+new window bounds"
# Drag state is reset on button-up and button-down.
grep -Eq 'g_drag = 0' "$USER32" ||
  fail "drag state is not cleared on button events"

# --- Composite layers clears transparent window pixels to background ---
# Non-desktop composite path: where window_layer alpha == 0, restore background
# from aero cache instead of leaving stale scanout pixels (drag trails).
grep -Eq 'u32 bg = \(u32_aero_bg_w==g_fw&&u32_aero_bg_h==g_fh\)' "$USER32" ||
  fail "composite_layers non-desktop path does not restore background from aero cache"
grep -Eq 'if \(\(w >> 24\) == 0\) c = bg;' "$USER32" ||
  fail "composite_layers does not clear transparent window pixels to background"

# --- Start button hit test matches draw_taskbar taskbar position ---
# on_click uses ty = h - 48, matching draw_taskbar's taskbar top at h - 48.
grep -Eq 'int ty = h - 48;' "$USER32" ||
  fail "on_click ty does not match draw_taskbar taskbar position (h - 48)"

echo "Window drag regression source-contract checks passed."
