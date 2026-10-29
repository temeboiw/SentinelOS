#!/usr/bin/env bash
# Batch 5 keyboard navigation and activation source-contract checks.
set -euo pipefail
ROOT="${1:-.}"
EXPLORER="$ROOT/user/fileexplorer.c"
fail() { echo "FAIL: $*" >&2; exit 1; }
require() { grep -Fq -- "$2" "$1" || fail "$3"; echo "PASS: $3"; }
require "$EXPLORER" '*selection+=direction*step;' "arrow and Page keys move the selected entry"
require "$EXPLORER" 'if(*selection<g_scroll)g_scroll=*selection;' "keyboard selection scrolls into view"
require "$EXPLORER" 'g_hover_row=*selection;' "keyboard selection uses the existing row highlight"
require "$EXPLORER" 'if(selected>=0 && selected<g_count && g_isdir[selected])' "Enter can open the selected directory"
require "$EXPLORER" 'fe_history_visit(g_path);g_filter_len=0;g_filter[0]=0;g_home=0;' "keyboard directory activation updates history and clears stale search"
require "$EXPLORER" 'int step=(msg.wParam==0x21 || msg.wParam==0x22)?VISIBLE_ROWS:1;' "Page Up and Page Down move by a visible page"
require "$EXPLORER" 'int count=g_home?HOME_ITEM_COUNT:g_count;' "arrow and Page keys navigate Home pins as well as directory entries"
require "$EXPLORER" 'if (g_home) {' "Enter activates the selected Home pin"
require "$EXPLORER" 'fe_activate_home_selection(pChdir)' "keyboard Home activation uses the same real pin resolver"
require "$EXPLORER" 'i == g_home_selected' "Home keyboard selection is visible in the row highlight"
echo "File Explorer keyboard-navigation source-contract checks passed."
