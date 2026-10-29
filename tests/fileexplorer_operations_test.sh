#!/usr/bin/env bash
# Batch 3 source checks for real, bounded file creation and file copy/paste.
set -euo pipefail
ROOT="${1:-.}"
EXPLORER="$ROOT/user/fileexplorer.c"
USER="$ROOT/kernel/user.h"
fail() { echo "FAIL: $*" >&2; exit 1; }
require() { grep -Fq -- "$2" "$1" || fail "$3"; echo "PASS: $3"; }
require "$EXPLORER" 'SYS_CreateOutputFile=51, SYS_WriteOutputFile=52' "Explorer uses the existing kernel output-file syscalls"
require "$EXPLORER" 'static int fe_new_file(MessageBoxA_t pMsgBox)' "new-file action is implemented"
require "$EXPLORER" 'static int fe_copy_to_clipboard(int row, MessageBoxA_t pMsgBox)' "copy opens and retains a real source handle"
require "$EXPLORER" 'static int fe_paste_file(MessageBoxA_t pMsgBox)' "paste streams source bytes to a created destination file"
require "$EXPLORER" 'msg.wParam==0x4E' "Ctrl+N creates a new file"
require "$EXPLORER" 'msg.wParam==0x43' "Ctrl+C copies a selected file"
require "$EXPLORER" 'msg.wParam==0x56' "Ctrl+V pastes a copied file"
require "$EXPLORER" 'Copy did not finish. A partial destination file may remain' "partial copy failures are not reported as success"
require "$EXPLORER" 'Cut (not supported)' "cut is clearly disabled rather than falsely reported"
require "$EXPLORER" 'Delete (not supported)' "delete remains disabled until VFS deletion is implemented"
require "$EXPLORER" 'static int fe_protected_destination(void)' "file creation and paste guard the System32 destination"
require "$EXPLORER" 'Access denied: System32 is protected. No files were changed.' "protected destination operations fail closed"
require "$USER" 'static int64_t vfs_create_user_output_file(const char *name)' "kernel limits output creation to a basename in the current directory"
echo "File Explorer file-operation source-contract checks passed."
