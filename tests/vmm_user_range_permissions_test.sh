#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-.}"
VMM="$ROOT/kernel/vmm.h"
fail() { echo "FAIL: $*" >&2; exit 1; }

body="$(sed -n '/^static uint64_t MmLookupUserPte(/,/^}/p' "$VMM")"
[[ -n "$body" ]] || fail "user page-table lookup is missing"

# x86 applies writable permissions through every paging level. Keep the
# syscall range validator from treating a leaf as writable when PML4 denies it.
grep -Fq 'if (!(e & PTE_P) || !(e & PTE_U) || (write && !(e & PTE_RW))) return 0;' <<<"$body" ||
  fail "PML4 write permission is not checked"

echo "VMM user-range permission checks: PASS"
