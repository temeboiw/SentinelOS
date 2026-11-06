#!/usr/bin/env bash
# The Settings entry must use the same real process-launch path as other
# Start-menu applications instead of printing a placeholder status message.
set -euo pipefail
ROOT="${1:-.}"
USER32="$ROOT/user/user32.c"
fail() { echo "FAIL: $*" >&2; exit 1; }
grep -Fq 'static char settings_name[]="settings.exe";' "$USER32" ||
  fail "Start-menu Settings entry does not name the packaged Settings app"
grep -Fq 'u32_menu_launch(settings_name);return;' "$USER32" ||
  fail "Start-menu Settings entry does not use the real application launcher"
if grep -Fq 'SentinelOS Settings: environment and display controls are being added.' "$USER32"; then
  fail "Start-menu Settings entry still only prints a placeholder"
fi

# --- Start menu repeated open/close toggle ---
# The Start button must toggle g_menu on every click, not just the first.
grep -Eq 'g_menu=!g_menu' "$USER32" ||
  fail "Start button does not toggle g_menu"
# Every menu close path resets g_menu and repaints so the menu can reopen.
grep -Eq 'g_menu=0;g_search_focus=0;g_start_hover=-1;u32_taskbar_dirty=1;' "$USER32" ||
  fail "Menu close does not reset g_menu and state flags"
# The Start button click repaints the taskbar region so the menu appears/disappears.
grep -Eq 'compose_region\(0,damage_y,\(int\)g_fw,\(int\)g_fh\)' "$USER32" ||
  fail "Start button click does not compose_region over taskbar range"
# Menu hover changes also repaint so hover state is visible on repeated opens.
grep -Eq 'compose_region\(0,sl\.y,\(int\)g_fw,\(int\)g_fh\)' "$USER32" ||
  fail "Menu hover does not repaint the menu region"

echo "Start-menu Settings launch source-contract check passed."
