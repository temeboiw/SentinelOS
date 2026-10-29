#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-.}"
VMM="$ROOT/kernel/vmm.h"
fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }
require() {
  grep -Fq -- "$2" "$1" || fail "$3"
  pass "$3"
}

require "$VMM" 'if (i4 == 0 && i3 == 0) continue;' \
  "teardown skips the borrowed system identity-map subtree"
require "$VMM" 'va < USER_CODE_BASE ||' \
  "user allocation rejects the shared low identity-map range"
require "$VMM" 'MmDeleteAddressSpace(test_proc);' \
  "VMM self-test destroys a process address space"
require "$VMM" 'teardown_ok = pmm_is_used(shared_pd_pa) &&' \
  "VMM self-test checks the borrowed page-table frame remains allocated"
require "$VMM" 'MmVirtualPagePresent(PspSystemProcess, shared_va) &&' \
  "VMM self-test checks the system identity mapping remains present"

echo "VMM address-space teardown source checks passed."
