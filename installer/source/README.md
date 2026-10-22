# Isolated SentinelOS installer build tree

This folder is a source snapshot of the tracked SentinelOS repository as of the
base revision used to create the installer branch. The original repository
files at the repository root are not modified by work in this folder.

All tracked source folders are preserved under this directory, including boot,
HAL, kernel, shared headers, user-mode components, tests, build scripts, and
UserTestApps. The copied `scripts/build.sh` resolves its root relative to its
own location, so running it here uses `installer/source/build/` and does not
write into the normal top-level `build/` directory.

This is the isolated baseline, not yet the completed installer build. Installer
changes belong here: add `instlr.exe` and `screen.exe`, use a separate
installer boot path, omit game/test-app payloads from the install image, build
`esp.spf` and `system.spf` from actual staged files, and master
`SentinelOS-V4-BETA.iso`. Keep the normal root-level source and build path
unchanged.

The installer must reuse the SentinelOS kernel, HAL, Win32-compatible runtime,
Vulkan implementation and font code from this copied tree. It must not be
replaced by a Linux installer environment.
