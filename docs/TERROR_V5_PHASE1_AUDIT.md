# SentinelOS V5: TERROR — Phase 1 Architecture Audit

**Status:** User reports the telemetry build booted through logon and passed desktop checks, including File Explorer launch and window dragging. Full release qualification and independent build verification remain pending.
**Branch:** `terror-v5-phase1`
**Baseline:** `main` at the time this branch was created.
**Purpose:** Establish the actual implementation boundary before making foundational TERROR changes.

## Findings confirmed in source

### Kernel and process model
- `kernel/kernel.c` initializes CPU tables, physical and virtual memory, heap, HAL, networking, audio, SMP discovery/startup, and self-tests before entering user mode.
- `kernel/user.h` contains the user-process table and system-call IDs for memory management, process/thread operations, window registration, and graphics.
- The current process table is bounded by `UP_MAX = 8`. This is a concrete scalability constraint for the current process model, not a statement about the eventual V5 target.

### Window management
- `kernel/wm.h` contains a kernel-side window table bounded by `WM_MAX = 8`, z-order activation, minimize/restore, unregister, hit testing, and z-order enumeration.
- Hit testing uses widened arithmetic to avoid rectangle-edge overflow.
- The table is a small static implementation; it is not yet evidence of a complete general-purpose desktop window manager.
- Preserve existing Explorer folder behavior and the currently fixed mouse cursor during the V5 work. Changes to window lifecycle must be isolated and regression-tested.

### Graphics
- `kernel/gfx.h` identifies a VirtIO-GPU 2D backend, PCI display-adapter inventory, GPU resource/scanout operations, and VirGL-related 3D command paths.
- The source defines graphics capability flags and 3D-related system-call interfaces. These declarations alone do not prove complete 3D acceleration, driver coverage, or Vulkan conformance.
- `user/soft3d.c` contains a CPU-side software 3D rasterizer and can obtain either the VirtIO-GPU surface or the firmware framebuffer. This is a real architectural distinction that must be accounted for before claiming GPU-only rendering.
- `user/opengl32.c` explicitly describes itself as a bounded software OpenGL 1.1 compatibility implementation over a mapped framebuffer. It is not full hardware OpenGL.
- End-to-end trace: syscall 32 initializes graphics and returns backend/surface metadata; syscall 33 flushes a rectangle; syscalls 34–36 submit/present VirGL work and upload the shared buffer; window-layer and window-surface presentation go through syscalls 37 and 40. The shared compositor in `kernel/user.h` copies pixels on the CPU into the mapped VirtIO-GPU backing and then calls `gfx_flush`, which issues a VirtIO-GPU transfer and flush. This is GPU-backed scanout, but not GPU-only composition or proof that the GPU rasterized those pixels.
- `gfx.h` now counts successful 2D flushes and cumulative transferred pixel area, with sparse serial telemetry on the first four flushes and every 4096th flush. This is instrumentation for measuring the real path, not a performance fix. The telemetry change has not yet been built or runtime-tested.
- The source audit has not found evidence of a complete Vulkan implementation or an integrated, working DXVK runtime. Those are major implementation projects, not simple API-export additions.

### Build and repository health
- `scripts/build.sh` is the repository's Linux build entry point and supports `--run`, `--clean`, and other options. It attempts to use all online CPUs by default, with `JOBS` available to limit parallelism.
- `readme.md` still describes the project as an early kernel that paints the framebuffer and halts. That description is stale compared with the current user-mode and desktop source tree and should be updated in a separate documentation task.
- The user reports that `bash scripts/build.sh` succeeded on the Phase 1 branch before the telemetry change. That is a user-reported baseline, not independent verification of the latest commit. No QEMU boot, frame-time benchmark, or runtime regression test has been confirmed.

## TERROR architecture decisions

1. **Keep one graphics ownership path.** Applications request surfaces, drawing, and presentation through defined OS APIs. The OS graphics subsystem owns device selection, resource lifetime, synchronization, composition, and presentation.
2. **Make capability explicit.** Report the active backend and supported capabilities separately from discovered PCI devices. Never equate adapter detection with a working driver.
3. **Separate UI composition from 3D rendering.** A GPU-composed desktop and a hardware 3D API are related but distinct milestones. Neither proves the other is complete.
4. **Avoid false GPU-only claims.** CPU work remains necessary for application execution, input, command construction, scheduling, and resource management. The target is GPU-backed rendering/presentation for the supported desktop path, with any fallback clearly identified.
5. **Build Vulkan before DXVK.** Establish a documented Vulkan feature subset and test it with real workloads before integrating DXVK. DXVK support must be claimed per tested workload and device/backend.
6. **Protect working functionality.** Do not rewrite Explorer's folder system or the fixed mouse cursor as part of graphics refactoring. Add targeted regression checks around process exit, window unregister, focus, hit testing, and input.
7. **No placeholder completion.** Mark every subsystem as implemented, partial, untested, or unsupported. API names and exported symbols do not count as working behavior by themselves.

## Phase 1 work order

### P1.1 — Reproducible baseline
- Run `bash scripts/build.sh` and record the exact result and compiler/tool versions.
- Run `bash scripts/build.sh --run` where the environment supports QEMU.
- Preserve serial output and any compile/runtime failures as the baseline.

### P1.2 — Graphics path inventory
- [x] Trace initialization and active backend selection in `kernel/gfx.h`.
- [x] Trace graphics syscall handlers and the user-space calls that submit, flush, and present.
- [x] Trace the shared compositor and confirm that its pixel-copy loops run on the CPU before VirtIO-GPU transfer/flush.
- [x] Add sparse 2D flush/pixel-area telemetry to help quantify damage traffic.
- [ ] Build and boot the telemetry change; verify the log output and confirm no rendering/input regression.
- [ ] Document which operations are performed by the CPU, VirtIO-GPU 2D device, and VirGL path using runtime evidence.

### P1.3 — Release-blocking tests
- Build from a clean state.
- Boot to the desktop.
- Open, move, minimize, restore, and close windows repeatedly.
- Confirm process exit removes its window and leaves no stale pixels.
- Confirm input continues to work after closing a window.
- Measure frame time and redraw area rather than assuming 60 FPS.
- Record the active graphics backend and capabilities in diagnostics.

## Phase 1 exit criteria

Phase 1 is complete only when the baseline build result is recorded, the active graphics path is traced end-to-end, and the existing desktop regression tests have reproducible steps and outcomes. Vulkan and DXVK work should not be presented as complete until their own feature and workload tests pass.
