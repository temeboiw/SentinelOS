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
echo "Start-menu Settings launch source-contract check passed."
