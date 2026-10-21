# SentinelOS V4: Vulkan and DXVK bring-up

## Goal

Add a real Vulkan path to SentinelOS and, only after that path and the required Win32 runtime are usable, bring up DXVK for Direct3D 9/10/11 applications. Do not advertise an API or capability until a real implementation passes its tests.

## Current baseline

- `kernel/gfx.h` contains a VirtIO-GPU 2D backend and an experimental VirGL command path.
- `scripts/run.sh` defaults to VirtIO-VGA and has an optional VirGL/OpenGL QEMU mode.
- `user/soft3d.c` is a CPU software rasterizer; it is useful as a fallback, not a Vulkan implementation.
- These are not yet a Vulkan ICD, a Vulkan loader, or a DXVK-compatible runtime.

## Architecture decision

For virtual-machine graphics, investigate the VirtIO-GPU **Venus** protocol as the Vulkan transport. VirGL carries Gallium/OpenGL commands and does not, by itself, expose Vulkan. The VM configuration, QEMU/virglrenderer support, guest memory model, blob resources, and Venus capset negotiation must all agree.

Keep the firmware framebuffer and existing 2D path available as fallbacks. Keep Vulkan user-space code out of the kernel wherever possible; the kernel should provide safe device, memory, synchronization, and submission primitives.

## Stages and acceptance criteria

### V4.1 — Audit and isolate graphics interfaces
- Trace the current graphics syscalls, user-pointer validation, physical-memory allocation, DMA mapping, and process address-space rules.
- Verify VirtIO queue layout, feature negotiation, queue notification, device reset/error paths, and resource lifetimes.
- Keep the existing desktop and cursor paths unchanged during driver work.
- Acceptance: current build and VM boot remain reproducible; fallback is verified.

### V4.2 — VirtIO-GPU transport required by Venus
- Implement and test the required modern VirtIO-GPU feature negotiation and command/response handling.
- Add the Venus capset query and negotiation, blob-resource/memory handling, and required context initialization only where supported by the QEMU device.
- Validate DMA addresses, bounds, alignment, response types, timeouts, and cleanup.
- Acceptance: a deterministic kernel self-test proves Venus capability negotiation and resource setup on a compatible QEMU/virglrenderer host; unsupported hosts remain on the framebuffer/2D fallback.

### V4.3 — Vulkan userspace ABI and ICD
- Bring in matching Khronos Vulkan headers and define a versioned SentinelOS userspace ABI.
- Implement a Vulkan loader/ICD boundary and device discovery, then a deliberately small, documented feature set.
- Implement and test instance/device enumeration, queue submission, memory allocation, command buffers, fences/semaphores, buffers, images, and presentation before claiming those features.
- Report only supported Vulkan extensions/features. Do not ship success-returning placeholders for unsupported entry points.
- Acceptance: a Vulkan conformance-oriented smoke test enumerates the device and renders/presents a triangle through the real backend; the software/framebuffer fallback remains functional.

### V4.4 — Window-system integration
- Connect Vulkan presentation to SentinelOS's existing window manager and composition model.
- Define swapchain ownership, surface lifetime, resize handling, synchronization, and damage/present behavior.
- Acceptance: a Vulkan sample can repeatedly resize, present, and close a window without stale frames, resource leaks, or regressions in desktop layering/cursor behavior.

### V4.5 — DXVK compatibility prerequisites
- Audit PE loading, DLL imports/exports, exception/unwind support, TLS, synchronization, virtual memory, file mapping, threads, and Win32 windowing needed by DXVK.
- Implement only the missing, testable Windows APIs needed by real DXVK dependencies; do not add fake exports.
- Supply a compatible Vulkan loader, Win32 surface integration, shader/compiler dependencies, and the required runtime DLL loading behavior.
- Acceptance: a small native Vulkan Win32 test program works before DXVK is introduced.

### V4.6 — DXVK bring-up
- Pin a specific upstream DXVK version and record its license/dependencies.
- Build and run the real DXVK DLLs against SentinelOS's Vulkan runtime and Win32 compatibility layer.
- Start with a small Direct3D 9/11 test application; test shader compilation, resource mapping, presentation, synchronization, and device loss.
- Acceptance: a real DXVK test application renders correctly in the VM and passes repeatable regression tests. Only then expand game/application coverage.

### V4.7 — Hardware and regression coverage
- Add a separate hardware-driver track after the virtual Venus path is stable.
- Keep framebuffer and CPU software rendering as reliable fallback modes.
- Test boot, login, Explorer, window movement/resize, cursor layering, shutdown, and disk/filesystem behavior after every graphics change.

## Important constraints

- DXVK translates Direct3D 9/10/11 to Vulkan; it does not implement Vulkan or a GPU driver.
- VirGL/OpenGL support alone is not Vulkan support. For the VM path, Venus support must be verified end-to-end.
- Vulkan and DXVK are substantial projects. Progress must be measured by working tests and implemented API coverage, not by filenames, headers, or capability bits.
- Do not merge this work to `main` until each completed stage builds and its stated acceptance tests pass.
