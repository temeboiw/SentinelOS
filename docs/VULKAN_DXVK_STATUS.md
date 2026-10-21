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
- CPU execution of `vkCmdFillBuffer`, `vkCmdCopyBuffer`, and `vkCmdClearColorImage` on queue submission;
- bounded 2D RGBA8/BGRA8 image allocation and clear operations in CPU memory, restricted to transfer-source/transfer-destination usage; sampled, storage, and color-attachment usages are rejected until their actual operations are implemented;
- tightly packed whole-image buffer-to-image and image-to-buffer transfers, with explicit layout checks;
- basic fence status/wait/reset and device/queue idle operations;
- bounded SPIR-V 1.0 shader-module structure validation and module lifetime;
- a tested, bounded scalar SPIR-V instruction interpreter for selected 32-bit integer arithmetic, bitwise/shift operations, comparisons, logical operations, copy/bitcast, and select instructions; unsupported executable instructions fail closed;
- synchronous binary-semaphore consume/signal for already-signaled waits (an unsignaled wait returns `VK_NOT_READY` because this backend cannot suspend a queue).
- procedure lookup for implemented entry points.

This is an **experimental, incomplete Vulkan software backend**, not a
conformant full Vulkan 1.0 implementation. It does not implement graphics or
compute pipelines, shader modules/SPIR-V execution, image views, render passes,
descriptor sets, full synchronization semantics, swapchains/surface
presentation, or the broad feature/limit set expected by normal engines. It is
not DXVK-ready.

Shader-module creation validates the SPIR-V header and bounded instruction\nstream; it does not prove full shader semantic validity. The new scalar\ninterpreter is an internal execution primitive covered by host tests, not yet a\ncomplete Vulkan shader stage: it does not implement general control flow,\nvariables/pointers, interfaces, floating-point arithmetic, or resource access.\nThe software rasterizer in `user/vk_softpipe.c` remains separate and is not yet\nconnected to Vulkan draw commands.\n\nThe physical-device properties expose only a minimal software-device identity;
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
