#!/usr/bin/env bash
# Cross-agent integration check: kernel graphics syscall IDs (Agent 1) must be
# consistently referenced by the graphics/userspace agents (Agent 2 vulkan.c,
# Agent 3 gdi32.c) and the desktop/window manager (user32.c).
#
# This is a source-contract gate: it verifies that every agent agrees on the
# syscall numbers used to cross the kernel/userspace graphics boundary. It does
# not exercise the kernel at runtime.
set -euo pipefail

ROOT="${1:-.}"
KUSER="$ROOT/kernel/user.h"
NTDLL="$ROOT/user/ntdll_user.h"
USER32="$ROOT/user/user32.c"
GDI32="$ROOT/user/gdi32.c"
VULKAN="$ROOT/user/vulkan.c"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }
require() {
  local file="$1" pattern="$2" label="$3"
  grep -Eq -- "$pattern" "$file" || {
    echo "FAIL: $label ($file does not match $pattern)" >&2
    exit 1
  }
  echo "PASS: $label"
}

# --- Kernel syscall definitions (Agent 1) -----------------------------------
require "$KUSER" '^#define SYS_NtMapFramebuffer[[:space:]]+8$' \
    "kernel assigns framebuffer map syscall ID 8"
require "$KUSER" '^#define SYS_NtInitializeGraphics[[:space:]]+32$' \
    "kernel assigns initialize-graphics syscall ID 32"
require "$KUSER" '^#define SYS_NtFlushGraphics[[:space:]]+33$' \
    "kernel assigns flush-graphics syscall ID 33"
require "$KUSER" '^#define SYS_NtSubmit3D[[:space:]]+34$' \
    "kernel assigns submit-3D syscall ID 34"
require "$KUSER" '^#define SYS_NtPresent3D[[:space:]]+35$' \
    "kernel assigns present-3D syscall ID 35"
require "$KUSER" '^#define SYS_NtUploadVirGLBuffer[[:space:]]+36$' \
    "kernel assigns upload-VirGL-buffer syscall ID 36"
require "$KUSER" '^#define SYS_NtPresentLayer[[:space:]]+37$' \
    "kernel assigns present-layer syscall ID 37"
require "$KUSER" '^#define SYS_NtPresentWindowSurface[[:space:]]+40$' \
    "kernel assigns present-window-surface syscall ID 40"
require "$KUSER" 'case SYS_NtPresentLayer:' \
    "kernel dispatches the present-layer syscall"
require "$KUSER" 'case SYS_NtPresentWindowSurface:' \
    "kernel dispatches the present-window-surface syscall"
require "$KUSER" 'case SYS_NtSubmit3D:' \
    "kernel dispatches the submit-3D syscall"
require "$KUSER" 'case SYS_NtPresent3D:' \
    "kernel dispatches the present-3D syscall"
require "$KUSER" 'case SYS_NtUploadVirGLBuffer:' \
    "kernel dispatches the upload-VirGL-buffer syscall"

# --- ntdll_user.h usermode syscall enum (Agent 1 / shared contract) ---------
require "$NTDLL" 'SYS_InitializeGraphics[[:space:]]*=[[:space:]]*32' \
    "ntdll initialize-graphics ID is 32"
require "$NTDLL" 'SYS_FlushGraphics[[:space:]]*=[[:space:]]*33' \
    "ntdll flush-graphics ID is 33"
require "$NTDLL" 'SYS_PresentLayer[[:space:]]*=[[:space:]]*37' \
    "ntdll present-layer ID is 37"

# --- user32.c (desktop/window manager) uses kernel graphics syscalls -------
require "$USER32" 'SYS_MapFb[[:space:]]*=[[:space:]]*8' \
    "user32 maps framebuffer via syscall 8"
require "$USER32" 'SYS_InitializeGraphics[[:space:]]*=[[:space:]]*32' \
    "user32 initializes graphics via syscall 32"
require "$USER32" 'SYS_FlushGraphics[[:space:]]*=[[:space:]]*33' \
    "user32 flushes graphics via syscall 33"
require "$USER32" 'SYS_PresentLayer[[:space:]]*=[[:space:]]*37' \
    "user32 presents layers via syscall 37"
require "$USER32" 'SYS_PresentWindowSurface[[:space:]]*=[[:space:]]*40' \
    "user32 presents window surfaces via syscall 40"

# --- gdi32.c (Agent 3) uses kernel graphics syscalls -----------------------
require "$GDI32" 'SYS_MapFb[[:space:]]*=[[:space:]]*8' \
    "gdi32 maps framebuffer via syscall 8"
require "$GDI32" 'SYS_FlushGraphics[[:space:]]*=[[:space:]]*33' \
    "gdi32 flushes graphics via syscall 33"

# --- vulkan.c (Agent 2) uses kernel graphics syscalls -----------------------
require "$VULKAN" 'SENTINEL_SYS_NT_INITIALIZE_GRAPHICS[[:space:]]*[0-9]*u' \
    "vulkan.c defines initialize-graphics syscall constant"
require "$VULKAN" 'SENTINEL_SYS_NT_SUBMIT_3D[[:space:]]*[0-9]*u' \
    "vulkan.c defines submit-3D syscall constant"
require "$VULKAN" 'SENTINEL_SYS_NT_PRESENT_3D[[:space:]]*[0-9]*u' \
    "vulkan.c defines present-3D syscall constant"
require "$VULKAN" 'SENTINEL_SYS_NT_UPLOAD_VIRGL_BUFFER[[:space:]]*[0-9]*u' \
    "vulkan.c defines upload-VirGL-buffer syscall constant"

# Verify the numeric values match the kernel dispatch table.
require "$VULKAN" '^#define SENTINEL_SYS_NT_INITIALIZE_GRAPHICS[[:space:]]+32u$' \
    "vulkan.c initialize-graphics syscall constant is 32u"
require "$VULKAN" '^#define SENTINEL_SYS_NT_SUBMIT_3D[[:space:]]+34u$' \
    "vulkan.c submit-3D syscall constant is 34u"
require "$VULKAN" '^#define SENTINEL_SYS_NT_PRESENT_3D[[:space:]]+35u$' \
    "vulkan.c present-3D syscall constant is 35u"
require "$VULKAN" '^#define SENTINEL_SYS_NT_UPLOAD_VIRGL_BUFFER[[:space:]]+36u$' \
    "vulkan.c upload-VirGL-buffer syscall constant is 36u"
pass "vulkan.c graphics syscall constants match kernel/user.h (32/34/35/36)"

# Verify user32.c and gdi32.c agree on framebuffer map ID 8.
fb32="$(grep -Eo 'SYS_MapFb[[:space:]]*=[[:space:]]*[0-9]+' "$USER32" | grep -Eo '[0-9]+')"
fbg="$(grep -Eo 'SYS_MapFb[[:space:]]*=[[:space:]]*[0-9]+' "$GDI32" | grep -Eo '[0-9]+')"
[[ "$fb32" == "8" ]] || fail "user32.c SYS_MapFb is $fb32, expected 8"
[[ "$fbg" == "8" ]] || fail "gdi32.c SYS_MapFb is $fbg, expected 8"
pass "user32.c and gdi32.c agree on SYS_MapFb=8 (kernel SYS_NtMapFramebuffer=8)"

echo "Cross-agent graphics syscall integration checks passed."
