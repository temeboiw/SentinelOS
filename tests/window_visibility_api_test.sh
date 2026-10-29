#!/usr/bin/env bash
# CloseWindow/OpenIcon must use USER32's normal minimize/restore path so the
# kernel window manager, compositor, and local USER32 state stay in sync.
set -euo pipefail
ROOT="${1:-.}"
USER32="$ROOT/user/user32.c"
fail() { echo "FAIL: $*" >&2; exit 1; }
close_body="$(sed -n '/BOOL CloseWindow(void \*hwnd)/,/^$/p' "$USER32")"
open_body="$(sed -n '/BOOL OpenIcon(void \*hwnd)/,/^$/p' "$USER32")"
grep -Fq 'ShowWindow(hwnd,6)' <<<"$close_body" ||
  fail "CloseWindow does not use the kernel-synchronized minimize path"
grep -Fq 'ShowWindow(hwnd,9)' <<<"$open_body" ||
  fail "OpenIcon does not use the kernel-synchronized restore path"
if grep -Fq 'g_wnd.minimized=' <<<"$close_body$open_body"; then
  fail "visibility APIs still mutate local state without the window manager"
fi
echo "USER32 minimize/restore synchronization contract checks passed."
