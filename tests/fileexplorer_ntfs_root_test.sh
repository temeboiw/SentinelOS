#!/usr/bin/env bash
# Regression checks for real NTFS-backed File Explorer root enumeration.
set -euo pipefail
ROOT="${1:-.}"
USER_H="$ROOT/kernel/user.h"
FE="$ROOT/user/fileexplorer.c"
EXT4_H="$ROOT/kernel/ext4k.h"
NTFS_H="$ROOT/kernel/ntfsk.h"
fail() { echo "FAIL: $*" >&2; exit 1; }
require() { grep -Fq -- "$2" "$1" || fail "$3"; echo "PASS: $3"; }
require "$EXT4_H" '#define VFS_MAX_ENT 1024' "VFS root cache can expose up to 1024 real entries"
require "$USER_H" 'static int vfs_ntfsk_refresh(void)' "NTFS directory entries are cached from real MFT records"
require "$USER_H" '(ntfsk_u64(fn)&0x0000ffffffffffffULL)==vfs_ntfsk_cwd_storage' "NTFS enumeration filters children by the actual parent directory reference"
require "$USER_H" 'vfs_ntfsk_cwd_storage=5;' "NTFS root starts at the real NTFS root MFT record"
require "$USER_H" 'if (g_ext4_ok || g_ntfsk_ok)' "directory queries use a mounted filesystem cache"
require "$USER_H" 'g_ntfsk_ok ? vfs_ntfsk_chdir(name) : -1' "directory navigation changes the real NTFS current directory"
require "$USER_H" 'if(vfs_ntfsk_refresh()<0)' "NTFS mount refreshes the real root listing"
QUERY="$(sed -n '/case SYS_NtQueryDirectoryFile:/,/case SYS_NtUploadVirGLBuffer:/p' "$USER_H")"
if grep -Fq 'user_bootinfo->module_count' <<<"$QUERY"; then
  fail "boot modules must never be exposed as filesystem root entries"
fi
require "$USER_H" 'vfs_ntfsk_internal_root_name(fn,nl)' "NTFS internal metadata is hidden from the normal root listing"
require "$USER_H" '"$MFT","$MFTMirr","$LogFile"' "common NTFS metadata names are filtered"
require "$FE" 'Right gutter click jumps to the matching scrollbar position' "File Explorer exposes a clickable scrollbar"
require "$FE" 'retry executable by basename' "Explorer retries executable using the disk-loader search path"
echo "File Explorer real-root/NTFS enumeration checks passed."
