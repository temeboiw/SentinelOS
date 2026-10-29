# Legacy installer source snapshot (not used by the build)

This directory is a historical duplicate of SentinelOS sources and is **not part of the active installer pipeline**.

The supported build entry point is:

```sh
bash scripts/build.sh
```

The root build compiles the OS once. `installer/build-installer.sh` stages `build/rootfs` and `build/esp` unchanged as the deployable system and target ESP, then creates a separate `staging/installer-esp` for installer-only `smss.exe`, `instlr.exe`, and `screen.exe`. `installer/make-iso.sh` builds the installed partition images only from the main OS staging trees and boots the ISO from the dedicated installer ESP. It does not compile or use this duplicate source snapshot.

Do not add new features here. Active OS changes belong in the repository's root source directories; installer-specific UI, tests, and ISO packaging remain under `installer/`.
