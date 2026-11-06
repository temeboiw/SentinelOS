#!/usr/bin/env bash
# Single system pointer source-contract checks.
# Regression: every USER32 process painted its own CPU cursor with a private
# save-under buffer, so each window left a ghost mouse / drag trail behind.
# The kernel compositor now owns the one pointer; USER32 only reports shape
# and hidden state.
set -euo pipefail

ROOT="${1:-.}"
KUSER="$ROOT/kernel/user.h"
USER32="$ROOT/user/user32.c"

require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}

# --- Kernel owns the pointer ---
require "$KUSER" '^#define SYS_WmCursor[[:space:]]+54' "kernel assigns pointer syscall ID 54"
require "$KUSER" 'case SYS_WmCursor:' "kernel dispatches the pointer syscall"
require "$KUSER" 'static void wm_cursor_overlay\(' "kernel has a pointer overlay"
require "$KUSER" 'static void wm_cursor_track\(' "kernel tracks pointer motion itself"
require "$KUSER" 'wm_cursor_track\(vi_ok \? vi_x : mouse_x' "input syscall moves the pointer regardless of which process polls"

# The overlay must be blended after the taskbar and before the flush, so every
# recompose of a rectangle is pointer-correct and nothing needs restoring.
BODY="$(sed -n '/^static void wm_shared_compose(uint32_t x,uint32_t y,uint32_t w,uint32_t h) {/,/^#define SYS_NtSocketCreate/p' "$KUSER")"
echo "$BODY" | grep -Eq 'wm_cursor_overlay\(out,pitch,x,y,w,h,width,height\);' || {
  echo "FAIL: wm_shared_compose does not blend the pointer" >&2; exit 1; }
echo "$BODY" | tr -d '\n' | grep -Eq 'wm_cursor_overlay\(out,pitch,x,y,w,h,width,height\);[[:space:]]*int flush_ok=gfx_flush' || {
  echo "FAIL: pointer must be blended right before gfx_flush" >&2; exit 1; }
echo "PASS: pointer is the topmost layer, blended just before the flush"

# --- USER32 never paints a private pointer once the kernel owns it ---
require "$USER32" 'SYS_WmCursor=54' "USER32 uses pointer syscall ID 54"
require "$USER32" 'static int u32_kernel_cursor_sync\(void\)' "USER32 has the kernel pointer sync"
for fn in cursor_save_draw cursor_repaint_transition cursor_clear_footprint; do
  BODY="$(tr -d '\r' < "$USER32" | sed -n "/^static void $fn(/,/^}/p")"
  echo "$BODY" | grep -Eq 'u32_kernel_cursor_sync\(\)' || {
    echo "FAIL: $fn does not defer to the kernel-owned pointer" >&2; exit 1; }
  echo "PASS: $fn defers to the kernel-owned pointer"
done

# --- The shutdown screen draws straight to scanout, so the pointer must stop ---
require "$USER32" 'sy3\(SYS_WmCursor, 3, 0, 0\)' "shutdown screen suspends the kernel pointer"

echo "Single system pointer checks passed."
