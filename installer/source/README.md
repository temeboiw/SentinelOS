# Legacy installer source snapshot (not used by the build)

This directory is a historical duplicate of SentinelOS sources and is **not part of the active installer pipeline**.

The supported build entry point is:

```sh
bash scripts/build.sh
```

The root build compiles the OS once. The installer then stages the artifacts from `build/esp` and `build/rootfs`, adds its installer-only session applications to a staging copy, and creates `installer/artifacts/SentinelOS-V4-BETA.iso`. It does not compile or use this duplicate source snapshot.

Do not add new features here. Active OS changes belong in the repository's root source directories; installer-specific UI, tests, and ISO packaging remain under `installer/`.
