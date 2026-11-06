---
name: regedit-architecture-decision
description: Architecture decisions for the V11 regedit.exe + persistent registry task
metadata:
  type: project
---

## Context
Task: implement a complete native SentinelOS Registry Editor (regedit.exe) with a real persistent hierarchical registry (V11 SOVEREIGN). See pasted task requirements (regedit requirements 1-13).

## Existing state (verified by inspection)
- `user/advapi32.c`: existing in-memory registry (ADVAPI_KEY[256], ADVAPI_VALUE[64] inline data[4096]). Only exports: RegCloseKey, RegOpenKeyExW, RegGetValueW, RegQueryValueExW (+GetUserNameW/security/crypto). Non-persistent (lost on reboot). Per-process DLL state. Missing most required APIs (RegEnumKeyEx, RegDeleteKey, RegDeleteTree, RegCopyTree, RegFlushKey, RegLoadKey, RegSaveKey, RegConnectRegistry, RegNotifyChangeKeyValue, ANSI variants, RegDeleteKeyEx).
- `kernel/obmgr.h`: object manager with `ObTypeKey` type, but Ob* functions are NOT reachable from user mode (SYS_NtGetProcAddress only resolves PE DLL exports, not kernel exports). Kernel exports ObCreateObject/ObReferenceObjectByHandle/etc. but no user-mode syscall to call them. So registry cannot use the kernel object manager from user mode without new kernel syscalls.
- `kernel/ntfsk.h`: NTFS reader + writer (`ntfsk_write_file_at`) exists; `SYS_NtWriteFile`/`SYS_NtReadFile`/`SYS_NtOpenFile`/`SYS_NtCloseFile` syscalls exist. **NTFS writes cannot grow a file** (only overwrite within existing size); no runtime file-create syscall. ext4 supports grow.
- `user/kernel32.c`: CreateFileA (SYS_OpenFile), ReadFile (SYS_ReadFile), WriteFile (SYS_NtWriteOutputFile=52 → NTFS write), CloseHandle (SYS_CloseFile), GetFileSize, SetFilePointer, FlushFileBuffers (stub, error 50). All resolvable via GetProcAddress("kernel32.dll", ...).
- `user/user32.c`: single-window compositor. main window hwnd=1, children via u32_child_from_hwnd. Message loop GetMessage/TranslateMessage/DispatchMessage. WM_PAINT(0xF)/WM_CLOSE(0x10)/WM_SIZE(0x18)/WM_CHAR(0x102)/mouse 0x200-0x203. Menus: CreateMenu/AppendMenuA/TrackPopupMenu/DestroyMenu. GDI: TextOutA/W, Rectangle, FillRect, CreateFontA, SetTextColor, GetTextExtentPoint32A. SetWindowDecorA. No rich control toolkit — regedit UI must be custom-drawn in WM_PAINT.
- `scripts/build.ps1`: `Build-SimpleExe "name" "source.c" base entry -subsystem windows` → `$sys32\$name.exe` (=build/rootfs/magic/System32). Disk image NTFS volume rooted at \magic mirrors build/rootfs/magic. No file-create at runtime → hive file must be pre-created at build time.

## Decisions
1. **Registry backend lives in user-mode `user/advapi32.c`** (reworked). Do NOT add kernel registry subsystem — user mode cannot reach Ob* functions, and kernel NTFS cannot grow files. Preserves kernel architecture; avoids destabilizing kernel.
2. **Persistence:** disk-backed hive file `magic/System32/config/registry.hive`, pre-created at build time (fixed-size zeroed file, e.g. 4–8 MB) since NTFS cannot grow files and no runtime create syscall. advapi32 loads hive at DLL init, saves (overwrites within preallocated size) on every mutation + RegFlushKey. Changes survive reboot (writes go to VHDX block device).
3. **System-wide behavior:** hive file is the source of truth; advapi32 loads it at process start, writes mutations to disk immediately, so later-starting processes see updates. Each process has its own in-memory cache (DLL is per-process), but disk is authoritative and rewritten on mutation.
4. **Access control:** implemented in advapi32 (validate handle = index+1 bounds/used check; check granted access mask on open; ERROR_ACCESS_DENIED on unauthorized write). Honest mechanism; broad default grant since no real user security DB is populated. Does not call Ob* (unreachable from user mode).
5. **regedit.exe:** custom-drawn GUI in `user/regedit.c`, entry `mainCRTStartup`, subsystem windows, links user32/gdi32/advapi32/kernel32 via runtime GetProcAddress (fileexplorer/settings pattern). Defines own memcpy/memset (freestanding /nodefaultlib). Features: menu bar, toolbar, address bar, tree pane (expand/collapse, lazy load), value list, edit dialogs (string/DWORD/QWORD/binary/multi_sz), create/delete/rename key, copy/paste, import/export .reg, search (Find Next), status bar.
6. **Build wiring:** add `Build-SimpleExe "regedit" "regedit.c" 0x40800000 mainCRTStartup -subsystem windows` to build.ps1; add regedit.exe to requiredSystem32; precreate registry.hive; export all regedit-needed advapi32 APIs.

## Status tracking
Use statuses NOT IMPLEMENTED / STUBBED / PARTIALLY IMPLEMENTED / IMPLEMENTED / TESTED / VERIFIED honestly. Do not misrepresent stubs as implementations.
