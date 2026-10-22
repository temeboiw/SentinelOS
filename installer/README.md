# SentinelOS V.4 [Beta] installer build

This directory is an isolated installer build area. All original source files are
copied under `installer/source/`; installer work must stay inside `installer/`.

## Build the isolated OS and package its payload

From the repository root:

```sh
bash installer/build-installer.sh
```

Pass supported options through to the copied source build, for example:

```sh
bash installer/build-installer.sh --clean
```

The build uses `installer/source/scripts/build.sh`, whose output is
`installer/source/build/`. It never edits or builds from root-level source
files. After the isolated OS build succeeds, it stages the real EFI files and
system rootfs, excludes `game.exe`, `maze.exe`, and game/test directories,
then creates and verifies:

- `installer/artifacts/esp.spf`
- `installer/artifacts/system.spf`

SPF is Sentinel Package File, a project-specific compressed container; it is
not Microsoft WIM-compatible. The packaging script refuses to emit packages if
required boot/kernel artifacts are absent.

## Current status and limitations

This script builds the isolated OS and verified package payloads. It does not
yet make a bootable installer ISO. The installer-specific boot entry, `instlr.exe`
and `screen.exe` runtime integration, disk enumeration, partition table writes,
FAT32/ext4 formatting, safe SPF extraction to selected partitions, and ISO
mastering still require implementation and hardware/VM testing.

No disk is formatted by this script. Formatting belongs in the installer runtime
and must require a selected target plus an explicit destructive-action confirmation.
