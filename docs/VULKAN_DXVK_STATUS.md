# SentinelOS Vulkan / DXVK status

## Implemented in the current main branch

`user/vulkan.c` is now a bounded, freestanding CPU Vulkan runtime rather
than just an empty loader. It builds as `vulkan.dll` and `vulkan-1.dll`
and implements a limited subset of the Vulkan 1.0 object and command model:

- instance creation/destruction and CPU physical-device enumeration;
- one software transfer-only queue family;
- device and queue creation;
- fixed-size host-visible memory allocations, buffer creation/binding/mapping;
- command pools and command-buffer allocation;
- CPU execution of `vkCmdFillBuffer` and `vkCmdCopyBuffer` on queue submission;
- basic fence status/wait/reset and device/queue idle operations;
- procedure lookup for implemented entry points.

This is an **experimental, incomplete Vulkan software backend**, not a
conformant full Vulkan 1.0 implementation. It does not implement graphics or
compute pipelines, shader modules/SPIR-V execution, images, render passes,
descriptor sets, full synchronization semantics, swapchains/surface
presentation, or the broad feature/limit set expected by normal engines. It is
not DXVK-ready.

The physical-device properties expose only a minimal software-device identity;
feature bits and image-format capabilities remain disabled. Unsupported
extensions are rejected rather than falsely advertised. The CPU runtime is not
wired to the existing VirtIO-GPU/VirGL renderer and does not provide native
Intel/AMD/NVIDIA Vulkan drivers.

## DXVK

DXVK is not integrated yet. It requires a substantially more complete Vulkan
implementation and a working Windows runtime/loader environment. A successful
DLL link is not proof that DXVK or any Direct3D game works.

## Verification gate

A full Vulkan claim requires API/resource/synchronization/error-path tests and
an in-guest program that executes graphics/compute work and presents visible
output. DXVK requires an actual Direct3D application rendered through DXVK in
the SentinelOS guest. These gates have not been passed.
