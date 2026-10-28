#!/usr/bin/env bash
# Batch 2 navigation regression checks. These guard source-level behavior;
# runtime interaction and path traversal are still verified by the OS boot test.
set -euo pipefail
ROOT="${1:-.}"
EXPLORER="$ROOT/user/fileexplorer.c"
fail() { echo "FAIL: $*" >&2; exit 1; }
require() { grep -Fq -- "$2" "$1" || fail "$3"; echo "PASS: $3"; }
require "$EXPLORER" 'static char g_history[FE_HISTORY_MAX][260];' "bounded Back/Forward history exists"
require "$EXPLORER" 'static int fe_walk_path_raw(SetCurrentDirectoryA_t pChdir, const char *target)' "absolute path resolver exists"
require "$EXPLORER" 'if(!fe_set_path(pChdir,g_address_edit)) return 0;' "address bar rejects paths that cannot be resolved"
require "$EXPLORER" 'if(!fe_address_commit(pChdir) && pMsgBox)' "invalid address path reports an error"
require "$EXPLORER" 'msg.wParam==0x74' "F5 refresh shortcut exists"
require "$EXPLORER" 'else if(ch==12)' "Ctrl+L activates the address editor"
require "$EXPLORER" 'msg.wParam==0x25 && (msg.lParam & (1LL<<29))' "Alt+Left history navigation exists"
require "$EXPLORER" 'msg.wParam==0x27 && (msg.lParam & (1LL<<29))' "Alt+Right history navigation exists"
require "$EXPLORER" 'if(relx>=240)' "address bar can be activated from the toolbar"
require "$EXPLORER" 'fe_history_visit(g_path);' "directory changes are added to navigation history"
echo "File Explorer navigation source-contract checks passed."
