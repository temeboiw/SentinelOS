#!/usr/bin/env bash
# Batch 4 source-contract checks for directory capacity, sorting and filtering.
set -euo pipefail
ROOT="${1:-.}"
EXPLORER="$ROOT/user/fileexplorer.c"
fail() { echo "FAIL: $*" >&2; exit 1; }
require() { grep -Fq -- "$2" "$1" || fail "$3"; echo "PASS: $3"; }
require "$EXPLORER" '#define MAX_ENT 1024' "directory entry storage is expanded beyond the old 256-entry cap"
require "$EXPLORER" 'for (DWORD i = 0; i < MAX_ENT - 1 && g_count < MAX_ENT; i++)' "directory enumeration uses the expanded bounded capacity"
require "$EXPLORER" 'static int name_contains_ci(const char *name, const char *needle)' "search matching is case-insensitive"
require "$EXPLORER" 'if(g_filter_len&&!name_contains_ci(name,g_filter))continue;' "directory filtering is applied while enumerating"
require "$EXPLORER" 'g_sort_mode=(g_sort_mode+1)%3' "S cycles native, folders-first, and alphabetical sorting"
require "$EXPLORER" '" | Sort: Native"' "current sort mode is visible in the toolbar"
require "$EXPLORER" '" | Sort: Folders"' "folders-first sort is labelled in the toolbar"
require "$EXPLORER" '" | Sort: A-Z"' "alphabetical sort is labelled in the toolbar"
require "$EXPLORER" 'else if(ch==27){g_search_mode=0;g_filter_len=0;g_filter[0]=0;reload(pQuery,pText,pInv);}' "Escape clears the active search filter"
require "$EXPLORER" 'if(ch==8&&g_filter_len>0)' "Backspace edits the active search filter"
echo "File Explorer views/search source-contract checks passed."
