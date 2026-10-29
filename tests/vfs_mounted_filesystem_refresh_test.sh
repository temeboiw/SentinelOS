#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXT4="$ROOT/kernel/ext4k.h"
USER="$ROOT/kernel/user.h"

if ! sed -n '/static int vfs_listdir(uint32_t dir_ino)/,/^}/p' "$EXT4" |
    grep -Fq 'if (!g_ext4_ok || !g_ext4.blksz || g_ext4.blksz > sizeof(g_ext4.blk))'; then
    echo "FAIL: ext4 directory listing must reject an unmounted or invalid block size" >&2
    exit 1
fi

if ! sed -n '/static int vfs_refresh_current_directory(void)/,/^}/p' "$USER" |
    grep -Fq 'if (g_ntfsk_ok) return vfs_ntfsk_refresh();'; then
    echo "FAIL: directory refresh must use the mounted NTFS cache when NTFS is active" >&2
    exit 1
fi

if ! sed -n '/static int vfs_refresh_current_directory(void)/,/^}/p' "$USER" |
    grep -Fq 'if (g_ext4_ok && g_ext4.blksz) return vfs_listdir(vfs_cwd_ino);'; then
    echo "FAIL: directory refresh must use ext4 only when it is mounted" >&2
    exit 1
fi

echo "Mounted-filesystem directory refresh checks: PASS"
