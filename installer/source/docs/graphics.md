# SentinelOS graphics backends

## VM configuration

The normal QEMU configuration uses **virtio-vga** for the primary display. The kernel initializes the VirtIO-GPU control queue and 2D scanout during **bootvid.exe**, while preserving the firmware framebuffer when device initialization fails. When QEMU VirGL is enabled and context setup succeeds, SentinelOS also exposes a VirGL 3D command-submission and presentation path. This is a GPU-accelerated graphics backend, not yet a complete Vulkan driver or a Vulkan-conformant implementation.

For hosts with QEMU's GTK OpenGL support, opt in to the VirGL-capable device:

```sh
SENTINELOS_VIRGL=1 JOBS=2 ./scripts/build.sh --run
```

This requires a host OpenGL-capable display. Do not use that option with the VNC display mode. Without VirGL, the software 3D renderer remains available and GFX_CAP_ACCEL_3D is not reported.

## Kernel/user boundary

- SYS_NtInitializeGraphics discovers and initializes the backend, then maps the active surface into the calling process.
- SYS_NtFlushGraphics transfers a damaged rectangle to the VirtIO-GPU scanout.
- SYS_NtSubmit3D submits a bounded VirGL command stream only when a VirGL context and render resources were successfully created.
- SYS_NtPresent3D transfers the VirGL render target to guest backing memory and presents it through the 2D scanout.
- SYS_NtUploadVirGLBuffer uploads shared vertex/index buffer contents before drawing.

The kernel owns PCI configuration, BAR mappings, the control virtqueue, and command submission. User-mode code never accesses PCI configuration ports or GPU MMIO directly.

## 3D API

user/soft3d.h exposes both paths:

- S3DDrawTriangle is a fixed-point software reference renderer with a depth buffer.
- S3DClearVirGL uses the host VirGL renderer for an accelerated clear when available.
- S3DDrawTriangleVirGL submits a Gallium/VirGL triangle pipeline when a real VirGL context is active.
- S3DGetSurface returns the active display surface and, when supported, the mapped shared VirGL buffer.
- S3DPresentVirGL reads the VirGL target back into shared backing and flushes it to the display.

The shared VirGL resource IDs are S3D_VIRGL_RENDER_TARGET_ID (2) and S3D_VIRGL_VERTEX_BUFFER_ID (3). The buffer is 1 MiB; the current triangle path uploads the first 84 bytes for three position-plus-color vertices.

For the software renderer, vertex x/y/z are signed 16.16 camera-space values and z must be positive. For the VirGL triangle API, x/y/z are signed 16.16 clip-space values in [-1, 1].

## Validation status

Do not merge this branch until the build workflow passes and the QEMU boot test verifies all of the following:

1. bootvid.exe remains visible during GPU initialization.
2. Unsupported/no-VirGL mode keeps the firmware framebuffer fallback.
3. USER32 windows, taskbar, cursor, movement, and close buttons still render correctly.
4. Dirty-region updates reach the VirtIO-GPU scanout without full-screen redraws.
5. S3DClearVirGL and S3DDrawTriangleVirGL render through a VirGL-enabled QEMU device.
6. The software triangle renderer works when VirGL is unavailable.

The VirGL path is capability-gated: no 3D acceleration flag is reported unless context creation, render-target setup, and the shared vertex buffer all succeed.
