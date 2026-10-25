#!/usr/bin/env bash
# Regression gate for the compatibility DLLs currently required by the
# Geometry Dash dependency trace. This verifies package layout and PE exports;
# it does not claim that every Win32 API or subsystem is implemented.
set -euo pipefail

ROOT="${1:-build/rootfs}"
READOBJ="${LLVM_READOBJ:-llvm-readobj}"

if ! command -v "$READOBJ" >/dev/null 2>&1; then
  echo "error: llvm-readobj is required to inspect PE exports" >&2
  exit 2
fi

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

require_file() {
  [[ -f "$1" ]] || fail "missing packaged file: $1"
}

require_export() {
  local file="$1" symbol="$2" dump
  dump="$("$READOBJ" --coff-exports "$file" 2>&1)" ||
    fail "cannot read PE exports from $file"
  grep -Fq "Name: $symbol" <<<"$dump" ||
    fail "$file does not export $symbol"
  pass "$(basename "$file") exports $symbol"
}

# WinMM is intentionally only a small implemented subset today. Keep the
# existing Geometry Dash timeGetTime dependency from silently regressing.
for file in "$ROOT/winmm.dll" "$ROOT/Windows/System32/winmm.dll" "$ROOT/magic/System32/winmm.dll"; do
  require_file "$file"
  require_export "$file" timeGetTime
done

# Ensure the shared CRT implementation and the VCRUNTIME compatibility module
# remain packaged under both names searched by the current loader.
for file in \
  "$ROOT/ucrtbase.dll" \
  "$ROOT/api-ms-win-crt-runtime-l1-1-0.dll" \
  "$ROOT/Windows/System32/ucrtbase.dll" \
  "$ROOT/Windows/System32/api-ms-win-crt-runtime-l1-1-0.dll" \
  "$ROOT/Windows/System32/vcruntime140.dll" \
  "$ROOT/vcruntime140.dll"; do
  require_file "$file"
done

for symbol in memcpy memmove memset memcmp strlen strcmp malloc free _beginthreadex strtol strtoul strtoll strtoull wcstombs mbstowcs; do
  require_export "$ROOT/ucrtbase.dll" "$symbol"
done
for symbol in memcpy memmove memset memcmp _purecall __std_terminate _invoke_watson; do
  require_export "$ROOT/vcruntime140.dll" "$symbol"
done

# These three kernel32 APIs were added to address concrete imports in the
# observed game dependency trace. Export checks are a regression gate, not a
# substitute for behavioral tests of their Windows semantics.
for symbol in SetStdHandle LoadLibraryExW FindFirstFileExW; do
  require_export "$ROOT/magic/System32/kernel32.dll" "$symbol"
done


# OpenGL is a real bounded framebuffer renderer, not a full OpenGL driver.
for file in "$ROOT/opengl32.dll" "$ROOT/Windows/System32/opengl32.dll" "$ROOT/magic/System32/opengl32.dll"; do
  require_file "$file"
  for symbol in glClearColor glClear glViewport glDrawArrays glDrawElements glGetString glGetError glGenTextures glBindTexture glDeleteTextures glTexImage2D glTexParameteri glTexCoordPointer glEnableClientState glBlendFunc glPixelStorei glColor4f glColor4ub wglCreateContext wglMakeCurrent wglGetCurrentDC wglGetCurrentContext wglGetProcAddress SwapBuffers; do
    require_export "$file" "$symbol"
  done
done

# The WGL proc-address resolver must return the real implementations for
# supported names and leave unknown extension names unavailable.
OGL_SOURCE="$(dirname "$0")/../user/opengl32.c"
require_file "$OGL_SOURCE"
for symbol in glClearColor glClear glViewport glScissor glEnable glDisable glIsEnabled glGetError glGetString glDrawArrays glDrawElements glVertexPointer glColorPointer glTexCoordPointer glEnableClientState glDisableClientState glLineWidth glBlendFunc glPixelStorei glTexParameteri glTexImage2D glBindTexture glGenTextures glDeleteTextures glGetIntegerv glGetFloatv glGetBooleanv glReadPixels glFinish glFlush glTexSubImage2D glColor4f glColor4ub; do
  grep -Fq "ogl_streq(name,\"$symbol\")" "$OGL_SOURCE" || fail "wglGetProcAddress does not resolve implemented symbol $symbol"
done
grep -Fq 'return 0;' "$OGL_SOURCE" || fail "wglGetProcAddress must return NULL for unsupported symbols"
[[ "$(grep -Fc 'static int ogl_streq(' "$OGL_SOURCE")" -eq 1 ]] || fail "duplicate or missing ogl_streq helper in OpenGL runtime"
pass "WGL proc-address source coverage and helper uniqueness"

echo "Compatibility DLL export/package regression checks passed."
