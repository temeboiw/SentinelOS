# SentinelOS V4: Vulkan and DXVK implementation

## Scope and compatibility contract

Vulkan and DXVK are separate deliverables. DXVK translates Direct3D 9/10/11 to Vulkan; it is not a Vulkan driver. SentinelOS must provide a usable Vulkan userspace runtime and a device implementation before DXVK can run.

The current graphics code in `kernel/gfx.h` is a custom VirtIO-GPU 2D path with experimental VirGL/Gallium command submission. That is not a Vulkan implementation, and a VirGL context must not be advertised as Vulkan support.

## Required stack

1. **Device/transport:** keep the firmware framebuffer fallback; finish VirtIO-GPU PCI capability parsing, feature negotiation, queue safety, resource lifetime, synchronization and reset/error handling.
2. **VM Vulkan path:** investigate VirtIO-GPU Venus capset and the QEMU/host requirements. Venus is the relevant Vulkan-over-VirtIO protocol; VirGL alone is an OpenGL/Gallium path. Report Venus as available only after querying and validating the actual device capset.
3. **Userspace Vulkan ICD/runtime:** implement or port a compatible Vulkan loader plus ICD. Define the supported Vulkan version and extension list from real implementation coverage; return errors for unsupported operations rather than fake success.
4. **Memory and synchronization:** provide GPU-visible memory allocation/mapping, command submission, fences/semaphores and safe process validation. No user process may submit arbitrary kernel physical addresses.
5. **DXVK prerequisites:** provide the Windows ABI and PE loader features DXVK needs, Vulkan loader/ICD discovery, required synchronization and memory semantics, and the Windows runtime dependencies. DXVK must be built for and tested against this specific ABI; ordinary Windows DXVK binaries are not automatically compatible with SentinelOS.
6. **Validation:** add a graphics self-test for device discovery, capset negotiation, allocation, command submission, completion and present. Keep software rendering and framebuffer paths available when GPU acceleration is absent.

## Acceptance criteria

- A normal QEMU VirtIO-VGA device reports 2D support only.
- Vulkan is reported only when a compatible Venus/Vulkan path is negotiated and tested.
- DXVK is marked unsupported until a Vulkan loader/ICD and the required Windows ABI/runtime are working.
- Every advertised capability is backed by a real operation and a test; unsupported paths fail cleanly.
- Build and boot tests run with both VirtIO-VGA and `SENTINELOS_GPU=none`.

## VM experiment

The run script now has an opt-in QEMU Venus device configuration:

```sh
SENTINELOS_VENUS=1 SENTINELOS_DISPLAY=window ./scripts/run.sh
```

This requires a QEMU build whose `virtio-vga-gl` device supports `venus`, `blob`, and `hostmem`, plus a host graphics stack that can initialize it. The option only configures the virtual device; it does **not** mean the SentinelOS guest has Vulkan yet. If QEMU rejects an option, use a QEMU build with Venus enabled rather than silently falling back and claiming Vulkan support.

## Immediate implementation order

1. Audit and harden the existing VirtIO-GPU queue and PCI capability code.
2. Add a capability/capset query path and distinguish 2D, VirGL, and Venus.
3. Implement the minimum Vulkan ICD/device/memory/queue path supported by the chosen VM transport.
4. Port/implement Vulkan loader integration and run conformance tests.
5. Port DXVK dependencies only after Vulkan and PE/runtime requirements pass.

Do not add Vulkan/DXVK branding or success logs until those acceptance tests pass.
