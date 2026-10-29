#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BODY="$(sed -n '/case SYS_WmSetRect:/,/case SYS_WmGetDirty:/p' "$ROOT/kernel/user.h")"
if ! grep -Fq 'wm_set_rect(pid, rect[0], rect[1], rect[2], rect[3]);' <<<"$BODY"; then
    echo "FAIL: WM set-rectangle syscall no longer updates the window manager" >&2
    exit 1
fi
if ! grep -Fq 'f->rax = 1;' <<<"$BODY" || ! grep -Fq 'return f;' <<<"$BODY"; then
    echo "FAIL: WM set-rectangle syscall must return without falling through" >&2
    exit 1
fi
echo "WM set-rectangle syscall return check: PASS"
