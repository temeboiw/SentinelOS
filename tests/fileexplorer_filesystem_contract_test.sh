#!/usr/bin/env bash
# Batch 1 source-contract checks for File Explorer filesystem safety.
# These assertions protect the existing fail-closed baseline while the VFS
# mutation APIs and path resolver are designed and implemented in later batches.
set -euo pipefail

ROOT="${1:-.}"
KUSER="$ROOT/kernel/user.h"
EXT4="$ROOT/kernel/ext4k.h"
NTFS="$ROOT/kernel/ntfsk.h"
K32="$ROOT/user/kernel32.c"
EXPLORER="$ROOT/user/fileexplorer.c"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }
require() {
  local file="$1" needle="$2" label="$3"
  grep -Fq -- "$needle" "$file" || fail "$label (missing: $needle)"
  pass "$label"
}
function_body() {
  local file="$1" function="$2"
  awk -v fn="$function" '
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
  ' "$file"
}

require "$NTFS" "does not implement writes" "NTFS driver declares read-only contract"
require "$EXT4" "static int e4_write_supported(Ext4k *fs)" "ext4 write capability gate exists"
require "$EXT4" "fs->blksz != 4096" "ext4 writer rejects unsupported block size"
require "$EXT4" "fs->feature_compat & (0x0004u | 0x0020u)" "ext4 writer rejects journal and directory-index layouts"
require "$EXT4" "fs->feature_ro_compat & (0x0010u | 0x0200u | 0x0400u)" "ext4 writer rejects unsupported read-only-compatible features"
require "$EXT4" "if (name[nl] == '/' || name[nl] == '\\\\') return -1;" "ext4 create rejects path separators in a basename"

create_body="$(sed -n '/^static int vfs_create_user_output_file(/,/^}/p' "$KUSER")"
[[ -n "$create_body" ]] || fail "VFS output-file creation function is missing"
grep -Fq 'if (!g_ext4_ok || !name || !name[0]) return -1;' <<<"$create_body" || fail "output-file creation must require ext4 and a nonempty name"
grep -Fq 'e4_create_empty_file(&g_ext4, vfs_cwd_ino, name, &ino)' <<<"$create_body" || fail "output-file creation must remain relative to the current directory"
pass "output-file syscall is limited to a basename in the current directory"

for function in CreateDirectoryA RemoveDirectoryA DeleteFileA MoveFileA CopyFileA; do
  body="$(function_body "$K32" "$function")"
  [[ -n "$body" ]] || fail "$function export is missing"
  grep -Fq 'k32_last_error=50' <<<"$body" || fail "$function must fail closed with ERROR_NOT_SUPPORTED until implemented"
  pass "$function fails closed until its filesystem implementation exists"
done

require "$KUSER" "if(!g_ext4_ok||!g_ext4.blksz||length<sizeof(UserVolumeMetadata)" "volume metadata syscall does not claim unsupported NTFS metadata"
require "$KUSER" "if(!g_ext4_ok||e4_inode(&g_ext4,vh->ino,inode)<0)" "file metadata syscall does not claim unsupported NTFS metadata"
require "$EXPLORER" "Access denied: System32 is protected." "Explorer retains its existing System32 protection warning"
require "$EXPLORER" "This filesystem is currently read-only." "Explorer reports unavailable mutations instead of claiming success"

echo "File Explorer filesystem source-contract checks passed."
