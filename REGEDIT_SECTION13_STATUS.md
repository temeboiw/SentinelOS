# regedit.exe + Persistent Registry — Section 13 Status Report

**Scope:** SentinelOS V11 SOVEREIGN native Registry Editor (regedit.exe), task c373.
**Basis:** honest self-assessment of the current working tree; nothing here has been end-to-end booted or run in QEMU.
**Status scale:** NOT IMPLEMENTED / STUBBED / PARTIALLY IMPLEMENTED / IMPLEMENTED / TESTED / VERIFIED.
**Nothing is VERIFIED** — no runtime behavior has been observed in the OS.

---

## Status summary

| Area | Status | Notes |
|------|--------|-------|
| Persistent registry backend (user-mode advapi32 + disk hive) | IMPLEMENTED / TESTED | `user/advapi32.c` reworked: all A-variants, handle + access-mask validation, per-mutation disk write. 4 MB `registry.hive` precreated at build time (NTFS cannot grow files; no runtime create syscall). |
| Reg* API set (A + W) | IMPLEMENTED / TESTED | All A-variants dllexported and used by regedit. W-variants `RegOpenKeyExW`, `RegGetValueW`, `RegQueryValueExW` are dllexported in `scripts/build.ps1` and implemented in `user/advapi32.c`; `advapi32.dll` links as a valid PE (27136 bytes). regedit uses A-variants exclusively. |
| Value types (SZ / DWORD / QWORD / Binary / MultiSz) | IMPLEMENTED | Display/formatting for all five types implemented in `format_value_data`. Value create/edit is wired in `dlg_commit` (kinds 2/3) calling `do_new_value`/`do_edit_value`; value delete works. Source compiles clean and builds to a valid PE. Not runtime-tested in QEMU. |
| regedit native GUI | PARTIALLY IMPLEMENTED | Menu bar, address bar, tree pane (expand/collapse), values pane, status bar, hints, new-key/delete/rename dialogs, find dialog. New-value, edit-value, rename and find dialogs all commit to the hive. Mouse, copy/paste, favorites not implemented. Not runtime-tested in QEMU. |
| Security / access control | PARTIALLY IMPLEMENTED | User-mode advapi32 validates handle bounds and granted access mask, returns `ERROR_ACCESS_DENIED` on unauthorized write. Not the kernel Object Manager (unreachable from user mode — no new kernel syscalls added, preserving kernel architecture). Broad default grant; no real security DB populated. |
| Build wiring (scripts/build.ps1) | IMPLEMENTED / TESTED | `Build-SimpleExe "regedit" ...`, `regedit.exe` in `requiredSystem32`, all regedit-needed advapi32 A-variants dllexported, 4 MB hive precreation. `user/regedit.c` compiles clean (`clang -ffreestanding -Wall -c` → 0 errors) and links to a valid PE. Source-contract tests pass. |
| Automated tests (Section 12) | IMPLEMENTED / TESTED | `tests/regedit_test.sh`: 43 source-contract checks (regedit surface, build wiring, advapi32 exports), all pass. |
| `.reg` import / export | IMPLEMENTED | `cmd_import` reads `import.reg` via `SYS_ReadFile` (syscall 6) and parses `[Key]` sections plus `"name"=value` (strings, `dword:`, `hex:`, `hex(b):`), creating keys and setting values. `cmd_export` walks the selected subtree recursively and writes `export.reg` via `SYS_CreateOutputFile`/`SYS_WriteOutputFile` (syscalls 51/52). Basic only: no `hex(7)` import, no escaped quotes, 64 KB import cap, 8 KB export buffer. Not runtime-tested in QEMU. |
| Key rename | IMPLEMENTED | `do_rename_key` opens the parent, copies the subtree with `RegCopyTreeA` to the new name, then deletes the old with `RegDeleteTreeA` (copy+delete, since advapi32 has no rename API). Not atomic; not runtime-tested in QEMU. |
| Value create / edit | IMPLEMENTED | `dlg_commit` kind 2 parses `name|type|data`, converts per `REG_*` type, calls `do_new_value` (`RegSetValueExA`). Kind 3 converts new string data per the value's type and calls `do_edit_value`. Both `cmd_refresh` on success. Buildable and source-tested; not runtime-tested in QEMU. |
| Find / search | IMPLEMENTED | Basic case-insensitive substring search (`strstri`) wired: Find dialog (kind 8) searches tree paths/names, then value names/formatted data; reports "Found: ..." or "Not found". Not runtime-tested (no QEMU boot). |
| Copy / paste, favorites, mouse | STUBBED | Not wired; no mouse handling. |
| End-to-end OS verification | NOT IMPLEMENTED | regedit.exe has not been booted or executed inside SentinelOS/QEMU. QEMU is available but the disk image is not bootable here (OVMF finds no bootable option). Do NOT mark stronger unless actually booted. |

---

## What is actually implemented (non-stub)

- Builds cleanly as freestanding PE32+ (`mainCRTStartup`, `/subsystem:windows`, `/nodefaultlib`), resolving `user32`/`advapi32`/`kernel32` exports at runtime via `sy3(9, ...)` GetProcAddress — same pattern as fileexplorer/settings.
- Tree view of the hive (expand/collapse, lazy subkey loading), value list, address bar, menu bar, status bar, keyboard navigation (arrows/WASD/vim), Enter/BS/Tab/Delete, refresh.
- Real (non-stub) operations: create key, delete key (recursive tree), delete value, rename key (copy+delete), create/edit value, .reg import/export (basic).
- Value rendering: REG_SZ, REG_EXPAND_SZ, REG_DWORD, REG_QWORD, REG_BINARY, REG_MULTI_SZ.
- Value create/edit dialogs wired (kinds 2/3 in `dlg_commit`), rename dialog wired, Find/search (basic substring) wired — all buildable and source-tested; not runtime-tested in QEMU.

## What is stubbed / not implemented (honest)

- Copy/paste, favorites, mouse.
- End-to-end execution in SentinelOS (QEMU boot does not succeed in this environment).

## Build verification performed

- `clang -ffreestanding -Wall -c user/regedit.c` → **0 errors** (only pre-existing unused-symbol warnings).
- `clang -ffreestanding -Wall -c user/advapi32.c` → **0 errors, 0 warnings**.
- `lld-link /subsystem:windows /entry:mainCRTStartup /nodefaultlib` → valid PE32+ `regedit.exe` (33280 bytes, MZ magic), rebuilt from current source.
- `lld-link /dll /subsystem:native ...` → valid PE32+ `advapi32.dll` (27136 bytes, MZ magic), includes `RegGetSecurityInfo`/`RegSetSecurityInfo` and all A+W variants.
- registry.hive precreation → 4 MB zeroed file (4194304 bytes) at `build/rootfs/magic/System32/config/registry.hive`.
- `bash tests/regedit_test.sh` → 43/43 PASS.

## End-to-end QEMU attempt

- `qemu-system-x86_64` IS available (`/c/Program Files/qemu/qemu-system-x86_64`, v11.1.0) and the disk image (`build/magic-disk.vhdx`, 239 MB) + OVMF (`build/ovmf.fd`) + bootloader (`build/esp/EFI/BOOT/BOOTX64.EFI`) all exist.
- Two boot attempts (default drive interface; and `virtio-blk-pci` + `bootindex=1` + `-boot order=c`, TCG accel, no display, stdio serial): OVMF firmware loads, but both report **"No bootable option or device was found. Not Found."** The VHDX image is not bootable in QEMU here.
- Conclusion: `regedit.exe` cannot be booted or executed in QEMU in this environment. End-to-end OS verification remains NOT IMPLEMENTED.

## Full build.ps1

- `scripts/build.ps1` has **no `--no-disk-image` switch** (params: `-Clean`, `-Run`, `-KeepFallback`, `-ResetNvram`). It requires Administrator (DiskPart), EDK2 headers, and OVMF, and creates a GPT VHDX. This host is **not** running as Administrator, so the full build cannot be executed here. All build outputs above are from a previous successful run.

## Files touched

- `user/regedit.c` — new (the regedit GUI); compiles clean (`clang -ffreestanding -Wall -c` → 0 errors) and rebuilds from source.
- `user/advapi32.c` — previously reworked (registry backend; all needed A-variants present; W-variants implemented).
- `scripts/build.ps1` — expanded advapi32 dllexports, added `regedit.exe` to `requiredSystem32`, added regedit `Build-SimpleExe`, added 4 MB `registry.hive` precreation.
- `tests/regedit_test.sh` — new (Section 12 automated tests).
- `REGEDIT_SECTION13_STATUS.md` — this report (post-implementation honest status).
