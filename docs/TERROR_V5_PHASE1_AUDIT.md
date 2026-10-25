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

## Runtime evidence supplied by the user

The user reports that desktop checks passed and supplied serial output showing:

- File Explorer starts as PID 5, creates and shows its window, reloads rows, and reaches `fileexplorer: ready`.
- The compositor presents bounded damage rectangles, including `760×16`, `760×32`, and `760×48`, and reports successful VirtIO-GPU flushes.
- A window drag produces successive old/new rectangle records, including movement partly beyond the screen edge.
- Mouse input and presentation events continue after launch and during the drag sequence.

**Interpretation:** this is positive runtime evidence for boot-to-app operation, partial-region composition, successful flush calls, and window-position updates. The serial excerpt does not by itself prove visual correctness at screen edges, complete old-region restoration, stable frame pacing, or GPU-side rasterization. User-reported checks are recorded as such; they are not independent test execution by the maintainer.

**Next focused regression:** drag a window partly beyond each screen edge, release it, move the pointer away, and verify that the exposed background is restored and no stale pixels remain. Keep this test separate from Explorer folder behavior and cursor implementation.

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


## Next milestone — P1.4: Damage correctness and frame pacing

This milestone deliberately keeps the current compositor, Explorer folder implementation, and mouse cursor implementation intact. It adds measurable acceptance criteria before any renderer rewrite.

### Work sequence
1. **Damage correctness:** exercise window movement at all four screen edges and all four corners. After each drag, release the pointer and move it away. Check both the old rectangle and newly exposed desktop for stale pixels, including areas clipped by the screen boundary.
2. **Lifecycle restoration:** close a window while it overlaps another window; verify the lower window and desktop are restored. Repeat minimize/restore and close/reopen, then verify input still reaches the correct visible window.
3. **Frame pacing baseline:** capture a timed sample during idle desktop, pointer movement, window dragging, and Explorer scrolling. Report elapsed time, frame/present count, and p50/p95/max frame interval. Do not infer FPS from flush count alone because one visible frame may trigger multiple flushes.
4. **Composition cost:** pair frame intervals with the existing composed-pixel and flush-pixel counters. Distinguish CPU composition work from the pixels transferred to the VirtIO-GPU backend.
5. **Optimization gate:** only optimize after the baseline identifies the dominant cost. Preserve the current telemetry and rerun the same scenarios after each change.

### Acceptance criteria
- No stale pixels in the exposed region after movement, clipping, minimize, restore, or process exit.
- No lost or misrouted input after a window lifecycle operation.
- Timed output states the measurement source and sampling duration, and does not label flushes as frames unless they are explicitly synchronized one-to-one.
- Report measured results, including regressions; do not claim a 60 Hz / 60 FPS target without timing evidence.
- Clean build and boot checks are recorded separately from user-supplied runtime observations.

**Current state:** the user has reported that the existing telemetry build passed their desktop checks. The tests above remain the next reproducible qualification step; frame-time instrumentation and benchmark results are not yet implemented or verified.


### P1.4 implementation update — synchronous flush latency telemetry

- [x] Add TSC-based elapsed-tick measurement around the VirtIO-GPU 2D transfer-plus-flush command path.
- [x] Track cumulative and maximum observed latency ticks, total flushed pixels, and logarithmic latency buckets in memory.
- [x] Emit sparse logs for the first four successful flushes and then every 256th successful flush.
- [ ] Build the branch and confirm the compiler accepts the new inline RDTSC code.
- [ ] Boot and compare latency logs during idle, pointer movement, window dragging, and Explorer scrolling.
- [ ] Use measured TSC frequency or a calibrated clock before converting ticks to time units. The current output deliberately does not claim milliseconds, frame intervals, or FPS.

**Measurement boundary:** this timing wraps the synchronous VirtIO-GPU transfer and flush commands. It does not include CPU-side composition before the flush call, and flush count is not frame count. The histogram is currently collected in memory but not printed; it can support percentile reporting after a bounded sampling/reporting interface is added. These changes are committed source, not yet build- or runtime-verified.
