#!/usr/bin/env bash
# Priority APIs must not claim changes unless the scheduler applies them.
set -euo pipefail
ROOT="${1:-.}"
K32="$ROOT/user/kernel32.c"
fail() { echo "FAIL: $*" >&2; exit 1; }
require() { grep -Fq -- "$2" "$1" || fail "$3"; echo "PASS: $3"; }
body() {
  local fn="$1"
  awk -v fn="$fn" '
    !found && index($0, fn "(") { found=1 }
    found {
      print
      line=$0
      for (i=1; i<=length(line); i++) {
        c=substr(line,i,1)
        if (c=="{") opens++
        if (c=="}") closes++
      }
      if (opens > 0 && opens == closes) exit
    }
  ' "$K32"
}

set_class="$(body SetPriorityClass)"
set_thread="$(body SetThreadPriority)"
get_class="$(body GetPriorityClass)"
get_thread="$(body GetThreadPriority)"
[[ -n "$set_class" && -n "$set_thread" && -n "$get_class" && -n "$get_thread" ]] || fail "priority API implementation is missing"
grep -Fq 'k32_last_error=50;return 0;' <<<"$set_class" || fail "SetPriorityClass must report ERROR_NOT_SUPPORTED without scheduler support"
grep -Fq 'k32_last_error=50;return 0;' <<<"$set_thread" || fail "SetThreadPriority must report ERROR_NOT_SUPPORTED without scheduler support"
grep -Fq 'return 0x20u;' <<<"$get_class" || fail "GetPriorityClass must report the unchanged normal class"
grep -Fq 'return 0;' <<<"$get_thread" || fail "GetThreadPriority must report the unchanged normal priority"
if grep -Fq 'k32_priority_class' "$K32" || grep -Fq 'k32_thread_priority' "$K32"; then
  fail "priority setters must not retain a pretend process-local change"
fi
echo "kernel32 priority API fail-closed contract checks passed."
