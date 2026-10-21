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


## SPIR-V scalar control flow update

The bounded scalar helper now follows `OpBranch`, `OpBranchConditional`, and 32-bit `OpSwitch` targets within the selected entry function, and resolves `OpPhi` values using the predecessor block. Selection/loop merge declarations are accepted as metadata. Execution remains instruction-budgeted and rejects unsupported operations, malformed targets, and unresolved values. This expands scalar execution but is not a complete SPIR-V runtime or full Vulkan conformance.


The scalar SPIR-V subset also executes `OpSNegate`, `OpLogicalNot`, and `OpNot` with unit coverage. These are scalar operations only; they do not imply support for full shader interfaces, pointers, floating-point shader math, or storage-resource access.


## Additional command and memory validation batch

The CPU backend now executes bounded `vkCmdUpdateBuffer` writes (up to 256 bytes per recorded update), supports one full-extent `vkCmdCopyImage` region for matching single-mip RGBA8/BGRA8 images already in transfer layouts, and implements `vkResetCommandPool` by resetting its command buffers. Flush/invalidate mapped-memory calls now validate range structures, memory ownership, mapping state, and bounds instead of unconditionally succeeding. These are bounded software-backend operations, not graphics-pipeline or full-conformance support.


## Progress estimate (2026-10-21)

**Estimated full Vulkan implementation progress: about 5%.** This is a rough engineering estimate based on functional coverage, not export count or lines of code. The current implementation has a bounded CPU device, buffer/image transfer commands, basic synchronization objects, SPIR-V validation, and a limited scalar integer interpreter. The majority of Vulkan remains unimplemented: graphics/compute pipelines, descriptors, render passes/framebuffers, shader interfaces and floating-point/resource execution, full synchronization semantics, presentation/swapchains, and broad extension/format coverage. DXVK compatibility is not established.

### Additional scalar SPIR-V instructions

The bounded scalar interpreter now handles `OpBitReverse` (204) and `OpBitCount` (205) for 32-bit scalar values, with tests. This does not make it a general shader runtime.


### Additional scalar bitfield execution

The interpreter now executes bounded 32-bit `OpBitFieldInsert`, `OpBitFieldSExtract`, and `OpBitFieldUExtract` operations, with checks for invalid offsets/counts and tests for insertion plus signed and unsigned extraction. This expands the scalar subset only; it is not a complete shader runtime.


### Buffer-copy validation and multi-region execution

`vkCmdCopyBuffer` now validates the complete region list before recording any operation: buffer/device ownership, distinct source and destination handles, bound live memory, transfer usage, nonzero region sizes, 4-byte alignment, bounds, and command-buffer capacity. Invalid lists are rejected atomically rather than silently skipping bad regions or partially recording valid ones. Tests cover two discontiguous copy regions, untouched gaps, and rejection of an out-of-bounds region without changing the recorded command count.

## Progress estimate update (2026-10-21)

**Tracked implementation milestone: 10% (engineering target, not Vulkan conformance).** The implementation remains a limited software Vulkan subset. This milestone reflects the growing tested foundation for buffer/image transfer and scalar SPIR-V execution; it does not mean that graphics pipelines, descriptor sets, render passes, shader interfaces, presentation, or DXVK support exist. Those major functional areas remain unimplemented, and full Vulkan compatibility is far from complete.


### Atomic image-buffer transfer validation

`vkCmdCopyBufferToImage` and `vkCmdCopyImageToBuffer` now validate every region before recording any commands. They reject empty/oversized region lists, invalid handles or memory ownership, unsupported transfer usage, invalid layout/extent/packing, out-of-bounds offsets, and command-buffer overflow. A malformed region list can no longer silently skip an invalid region while recording other regions. Tests verify an out-of-bounds image-to-buffer copy leaves the command buffer unchanged.

## Progress estimate update (2026-10-21)

**Tracked implementation milestone: 15% (engineering estimate, not Vulkan conformance).** This milestone records the growing set of exercised buffer/image transfer commands, validation paths, and scalar SPIR-V operations. Graphics and compute pipelines, descriptor binding, render passes/framebuffers, general shader/resource execution, presentation, and DXVK compatibility remain unimplemented or unverified.
