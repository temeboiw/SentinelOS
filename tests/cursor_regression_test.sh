#!/usr/bin/env bash
# Cursor regression tests (Agent 3: Window Manager, Cursors, Start Menu).
# Verifies the cursor shape is not corrupted and exactly one component owns
# cursor drawing. Source-contract checks against user/user32.c.
set -euo pipefail
ROOT="${1:-.}"
USER32="$ROOT/user/user32.c"
fail() { echo "FAIL: $*" >&2; exit 1; }

# --- Cursor shape is not corrupted ---
# Row 15 of the 16x16 cursor shape must not contain a stray "####" block
# disconnected from the arrow body. The correct row is "##..............".
if grep -Fq '##....####......' "$USER32"; then
  fail "cursor shape row 15 contains corrupted stray block (##....####......)"
fi
grep -Fq '##..............' "$USER32" ||
  fail "cursor shape row 15 is not the clean '##..............' row"

# --- Exactly one component owns cursor drawing ---
# cursor_save_draw exists (owned by cursor_repaint_transition).
grep -Eq 'cursor_save_draw' "$USER32" ||
  fail "cursor_save_draw is missing"
# cursor_save_draw must NOT be called from within compose() — that caused
# duplicate save-under cycles and drag trails.
if sed -n '/^static void compose(void) {/,/^static void compose_region/p' "$USER32" | grep -Eq 'cursor_save_draw\(g_mx'; then
  fail "compose() still draws the cursor directly (double draw with cursor_repaint_transition)"
fi
# cursor_repaint_transition exists and owns cursor repaint on input.
grep -Eq 'cursor_repaint_transition' "$USER32" ||
  fail "cursor_repaint_transition is missing"
# cursor_repaint_transition calls cursor_save_draw (single owner).
grep -Eq 'cursor_repaint_transition.*cursor_save_draw|cursor_save_draw\(x,y\)' "$USER32" ||
  fail "cursor_repaint_transition does not call cursor_save_draw"

echo "Cursor regression source-contract checks passed."
