---
name: regedit-progress
description: Implementation progress for the V11 regedit.exe + persistent registry task
metadata:
  type: project
---

## Status
- Architecture decision recorded (memory/regedit-architecture-decision.md): user-mode advapi32 persistent hive (precreated 4MB registry.hive at build time, NTFS overwrite-within-size via raw syscalls), regedit.exe custom-drawn GUI.
- Next: write new user/advapi32.c registry backend (all APIs A+W, persistence, access checks, value types).
- Then: write user/regedit.c GUI, wire scripts/build.ps1, create tests, build + honest status report.

## Files to create/modify
- user/advapi32.c — rework registry section (replace lines 1-304), keep security/crypto (306-685) verbatim, modify constructor to load hive.
- user/regedit.c — new GUI (to create).
- scripts/build.ps1 — add regedit.exe Build-SimpleExe, requiredSystem32, precreate registry.hive.
- tests/ — automated tests (to create).

## Key syscalls verified (kernel/user.h)
- SYS_OpenFile=5, SYS_NtReadFile=6, SYS_NtWriteOutputFile=52 (overwrite within size, pos-based), SYS_NtCloseFile=22, SYS_NtQueryFileSize=25, SYS_NtSetFilePosition=26 (rdx=0 = FILE_BEGIN), SYS_NtGetProcAddress=9 (returns export addr in rax).
- vfs_open_user_file opens NTFS writable; resolves "magic/" + path prefixes. Path must be narrow char string (not WCHAR).
- No runtime file-create syscall → hive file must be precreated at build time.
