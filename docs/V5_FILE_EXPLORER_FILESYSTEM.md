# SentinelOS V5 TERROR — File Explorer filesystem contract

This is the Batch 1 baseline for the File Explorer milestone. It records the
current kernel/VFS contract so later UI work cannot accidentally treat an
unsupported or failed operation as success.

## Current capability matrix

| Capability | ext4 root volume | NTFS root volume |
|---|---|---|
| Directory enumeration | Supported; current VFS directory cache is capped at 256 entries | Read-only NTFS reader; directory lookup/enumeration is limited by the current NTFS implementation |
| Open/read regular files | Supported for files resolved by the VFS | Supported for the NTFS subset handled by the read-only driver |
| File metadata | Supported for ext4 file handles; creation time is not available from the legacy inode format | Not currently exposed through the same metadata syscall |
| Volume metadata | Supported for ext4 | Not currently exposed through the same volume metadata syscall |
| Create empty/named file | Limited ext4 implementation; output-file syscall only creates a single basename in the current directory | Unsupported |
| Append/write file data | Limited ext4 subset; append-only, extent and filesystem-feature constraints apply | Unsupported |
| Create directory, delete, rename/move, copy | Not yet exposed as complete supported Win32/VFS operations | Unsupported |

The table describes the implementation found during Batch 1 inspection. It is
not a promise that every ext4 volume is writable. The ext4 write path rejects
unsupported layouts and features. Its metadata changes are not journaled, so
it must not be described as crash-atomic. NTFS is explicitly read-only.

## Safety invariants

1. An operation is successful only when its underlying VFS/filesystem function reports success. A UI message or exported Win32 symbol is not proof.
2. Unsupported mutating Win32 APIs must fail with ERROR_NOT_SUPPORTED (50) until their complete implementation and behavioral tests land together.
3. The existing output-file syscall accepts a basename, not an arbitrary path. It must reject slash and backslash characters and must not be used as a shortcut for general path mutation.
4. Ordinary file operations must not be able to mutate protected OS files or directories, including the packaged System32 tree. UI checks alone are not a security boundary; enforcement belongs in the VFS/kernel operation path.
5. A failed filesystem operation must not leave Explorer's current path/history in a false state, report success, or freeze input/cursor processing.
6. Never switch a mounted filesystem to another device after an I/O failure. If the ext4 writer fences a volume after an unrecoverable write/rollback failure, later mutations must remain disabled.

## Known limitations to resolve in later batches

- The VFS keeps a single current-directory entry cache and has a 256-entry cap.
- Current directory navigation is name-based and mostly relative to that cache; it is not yet a canonical, arbitrary-path resolver suitable for an editable address bar.
- CreateDirectoryA, RemoveDirectoryA, DeleteFileA, MoveFileA and CopyFileA currently return error 50 for valid-looking arguments.
- The file metadata syscall is ext4-specific; timestamps are limited by the metadata available from the ext4 inode.
- The ext4 writer supports only a constrained filesystem layout and does not provide journaled, power-loss-atomic transactions.
- The NTFS kernel driver is read-only and supports only a subset of NTFS files.

## Batch 1 validation

Run:

```sh
bash tests/fileexplorer_filesystem_contract_test.sh
```

This is a source-contract regression check, not a replacement for runtime
filesystem tests. Runtime create/copy/move/delete tests belong with the batches
that implement those operations.
