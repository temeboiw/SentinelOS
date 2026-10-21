# SentinelOS V4: in-OS Vulkan and DXVK

## Requirement

Vulkan must be implemented as part of SentinelOS. Do not treat a QEMU command-line option, host Vulkan passthrough, or a virtual GPU feature toggle as the implementation. QEMU may be used to test a driver, but the Vulkan API/runtime, kernel interfaces, memory management, command submission, synchronization, and capability reporting belong to SentinelOS.

DXVK is a separate compatibility component that translates Direct3D 9/10/11 to Vulkan. It depends on a working SentinelOS Vulkan implementation and on the Windows-compatible PE loader, DLL ABI, synchronization semantics, memory APIs, and runtime dependencies required by the DXVK build.

## Existing graphics baseline

`kernel/gfx.h` currently provides a custom VirtIO-GPU 2D path and experimental VirGL/Gallium command submission. That is not Vulkan. The current graphics ABI exposes framebuffer/2D/3D status but has no Vulkan loader, Vulkan ICD, Vulkan device/queue API, or Vulkan capability negotiation. Do not report Vulkan support merely because a VirtIO-GPU device or VirGL context exists.

## Required in-OS architecture

1. **Vulkan public ABI and loader:** add the Vulkan headers/ABI expected by applications, a loader that resolves supported entry points, instance/physical-device enumeration, extension/version reporting, and ICD discovery. Only expose functions and extensions actually implemented.
2. **Kernel graphics-device interface:** define a safe driver contract for device discovery, GPU-visible allocations, command submission, fences, and presentation. Validate all user pointers, handles, lengths, and process ownership; user mode must never submit arbitrary physical addresses.
3. **Hardware driver:** implement a real driver for the chosen GPU/device target, including its PCI identification, BAR/MMIO mapping, DMA/IOMMU constraints where applicable, queue setup, memory model, reset, timeout, and device-loss handling. Keep the firmware framebuffer fallback.
4. **First Vulkan backend:** choose a concrete target and implement a minimal but real Vulkan feature set end to end: enumerate device, create device/queue, allocate and map memory, create buffers/images, submit commands, synchronize, and present. Return correct errors for unsupported features; do not fake success.
5. **Shader and pipeline support:** implement or port shader compilation/translation and pipeline handling for the target GPU. A 2D framebuffer and VirGL command stream do not automatically supply Vulkan semantics.
6. **DXVK integration:** only after the Vulkan ICD passes basic tests, port/build DXVK and its dependencies against SentinelOS's Windows ABI. Resolve required DLLs, imports, TLS/unwind/runtime behavior, memory/synchronization APIs, and present path.
7. **Validation:** add device enumeration, allocation/map, command submission, fence, image clear, and present tests. Test failure and fallback paths as well as success.

## Hardware target decision

A Vulkan implementation needs a defined hardware target. The current code is VirtIO-GPU-centric and contains experimental VirGL submission, but does not yet provide Vulkan. The first backend should be explicitly selected and documented (for example, a specific VirtIO-GPU protocol or a specific physical GPU family); avoid claiming universal Vulkan support. A driver/protocol that relies on a host-side implementation may be useful as a test backend, but it must still be driven by SentinelOS's own in-OS Vulkan loader/ICD and must not be mistaken for a native driver for physical GPUs.

## Acceptance criteria

- The Vulkan loader and ICD are compiled as SentinelOS components and run in the SentinelOS guest.
- Vulkan device enumeration reflects a real initialized driver, not a hard-coded fake device.
- Supported versions, extensions, formats, and limits match the actual implementation.
- Buffer/image allocation, command submission, synchronization, and presentation are exercised by tests.
- Unsupported features fail cleanly and never return fabricated success.
- DXVK remains marked unsupported until a compatible Vulkan ICD and required Windows runtime/ABI features are verified.
- Existing framebuffer and 2D graphics paths continue working when the Vulkan-capable driver is unavailable.

## Implementation order

1. Select the first GPU/transport target and write its driver contract.
2. Audit and harden current PCI/MMIO and VirtIO queue code before reusing it.
3. Implement the in-OS Vulkan loader/ICD and one end-to-end vertical slice on that target.
4. Add tests and only then expand Vulkan coverage.
5. Bring up DXVK and its Windows runtime prerequisites after Vulkan passes those tests.

The QEMU run script intentionally does not provide a special Venus/Vulkan mode: configuring QEMU is not a substitute for implementing Vulkan inside SentinelOS.
