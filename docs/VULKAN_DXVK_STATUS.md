# SentinelOS Vulkan / DXVK status

## Implemented in the current main branch

`user/vulkan.c` is now a bounded, freestanding CPU Vulkan runtime rather
than just an empty loader. It builds as `vulkan.dll` and `vulkan-1.dll`
and implements a limited subset of the Vulkan 1.0 object and command model:

- instance creation/destruction and CPU physical-device enumeration;
- one software graphics/transfer queue family with a tested, bounded triangle-draw path;
- device and queue creation;
- fixed-size host-visible memory allocations, buffer creation/binding/mapping;
- command pools and command-buffer allocation;
- CPU execution of `vkCmdFillBuffer`, `vkCmdCopyBuffer`, and `vkCmdClearColorImage` on queue submission;
- bounded RGBA8/BGRA8 images in CPU memory, including a restricted color-attachment path used by the software triangle rasterizer;
- tightly packed whole-image buffer-to-image and image-to-buffer transfers, with explicit layout checks;
- basic fence status/wait/reset and device/queue idle operations;
- bounded SPIR-V 1.0 shader-module structure validation and module lifetime;
- implemented bounded-subset `vkCreateGraphicsPipelines`, `vkCmdBindPipeline`, `vkCmdBindVertexBuffers`, and `vkCmdDraw` entry points for a narrow two-stage scalar-shader path;
- a restricted standard-shaped render-target path: `vkCreateImageView`, single-color `vkCreateRenderPass`, `vkCreateFramebuffer`, `vkCmdBeginRenderPass`, and `vkCmdEndRenderPass`, with clear/store behavior and final-layout tracking;
- an end-to-end host regression test that submits a shader-driven triangle draw through command recording, shader evaluation, and CPU rasterization; the updated regression test uses the restricted render-pass/framebuffer path and no longer requires the SentinelOS-only color-target hook for that pipeline draw;
- a tested, bounded scalar SPIR-V instruction interpreter for selected 32-bit integer arithmetic, bitwise/shift operations, comparisons, logical operations, copy/bitcast, and select instructions; unsupported executable instructions fail closed;
- synchronous binary-semaphore consume/signal for already-signaled waits (an unsignaled wait returns `VK_NOT_READY` because this backend cannot suspend a queue).
- procedure lookup for implemented entry points.

This is an **experimental, incomplete Vulkan software backend**, not a
conformant full Vulkan 1.0 implementation. The new graphics-pipeline path is a
narrow bring-up path, not general Vulkan graphics support: it consumes three
pre-transformed `VkSoftVertex` records, executes only scalar SPIR-V results
supported by the existing interpreter, and uses a SentinelOS-specific target
binding hook. It does not yet implement standard vertex-input declarations,
shader interfaces/variables, descriptor sets, image views, render passes,
framebuffers, compute pipelines, full synchronization, swapchains/surface
presentation, or the broad feature/limit set expected by normal engines. It is
not DXVK-ready.

Shader-module creation validates the SPIR-V header and bounded instruction
stream; it does not prove full shader semantic validity. The new scalar
interpreter is an internal execution primitive covered by host tests, not yet a
complete Vulkan shader stage: it does not implement general control flow,
variables/pointers, interfaces, floating-point arithmetic, or resource access.
The software rasterizer in `user/vk_softpipe.c` is connected to the bounded `vkCmdDraw` path and exercised by the shader-driven triangle regression test.

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


## SPIR-V scalar control flow update

The bounded scalar helper now follows `OpBranch`, `OpBranchConditional`, and 32-bit `OpSwitch` targets within the selected entry function, and resolves `OpPhi` values using the predecessor block. Selection/loop merge declarations are accepted as metadata. Execution remains instruction-budgeted and rejects unsupported operations, malformed targets, and unresolved values. This expands scalar execution but is not a complete SPIR-V runtime or full Vulkan conformance.


The scalar SPIR-V subset also executes `OpSNegate`, `OpLogicalNot`, and `OpNot` with unit coverage. These are scalar operations only; they do not imply support for full shader interfaces, pointers, floating-point shader math, or storage-resource access.


## Additional command and memory validation batch

The CPU backend now executes bounded `vkCmdUpdateBuffer` writes (up to 256 bytes per recorded update), supports one full-extent `vkCmdCopyImage` region for matching single-mip RGBA8/BGRA8 images already in transfer layouts, and implements `vkResetCommandPool` by resetting its command buffers. Flush/invalidate mapped-memory calls now validate range structures, memory ownership, mapping state, and bounds instead of unconditionally succeeding. These are bounded software-backend operations, not graphics-pipeline or full-conformance support.


## Progress estimate (2026-10-21)

**Estimated full Vulkan implementation progress: about 5%.** This is a rough engineering estimate based on functional coverage, not export count or lines of code. The current implementation has a bounded CPU device, buffer/image transfer commands, basic synchronization objects, SPIR-V validation, and a limited scalar integer interpreter. The majority of Vulkan remains unimplemented: general graphics/compute pipelines, descriptors, render passes/framebuffers, shader interfaces and resource execution, full synchronization semantics, presentation/swapchains, and broad extension/format coverage. DXVK compatibility is not established.

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


### Bound-memory lifetime safety

`vkFreeMemory` now refuses to release an allocation while any live buffer or image still references it, preventing stale image-memory pointers and use-after-free behavior. Buffer and image memory binding now reject rebinding an already-bound resource and reject binding while the allocation is mapped. Regression tests cover freeing allocations still referenced by a buffer/image and duplicate binding attempts.

## Progress estimate update (2026-10-21)

**Tracked implementation milestone: 20% (engineering estimate, not Vulkan conformance).** This step improves resource lifetime and binding correctness in addition to tested transfer operations and the scalar SPIR-V subset. It does not represent graphics/compute pipeline support: pipelines, descriptors, render passes/framebuffers, general shader resource access, presentation, and DXVK compatibility remain major missing areas.


### Atomic image pipeline-barrier validation

Image-only `vkCmdPipelineBarrier` now validates the complete barrier list and command-buffer capacity before recording anything. It rejects unsupported memory/buffer barriers, non-null extension chains, queue-family transfers, invalid subresource ranges, no-op layout transitions, and transfer layouts inconsistent with image usage. Regression coverage verifies that an invalid second barrier leaves the command buffer unchanged and that a valid two-barrier list records both operations.

## Progress estimate update (2026-10-21)

**Tracked implementation milestone: 25% (engineering estimate, not Vulkan conformance).** This milestone adds atomic pipeline-barrier validation on top of tested resource lifetime and transfer paths. The runtime still lacks real graphics/compute pipelines, descriptor sets, render passes/framebuffers, a complete shader runtime, presentation, and DXVK compatibility.


### Same-buffer copy support

`vkCmdCopyBuffer` now accepts copies whose source and destination are the same buffer when the byte ranges are disjoint, including buffers bound to a shared memory allocation with distinct offsets. It rejects overlapping source/destination ranges before recording any operation. Regression tests verify successful same-buffer copying and atomic rejection of overlapping ranges.

## Progress estimate update (2026-10-21)

**Tracked implementation milestone: 30% (engineering estimate, not Vulkan conformance).** This milestone adds tested same-buffer transfer behavior and overlap validation. It does not imply graphics/compute pipeline, descriptor, render-pass, presentation, or DXVK support; those remain major gaps.


### Scalar floating-point SPIR-V execution

The bounded scalar interpreter now executes 32-bit `OpFNegate`, `OpFAdd`, `OpFSub`, `OpFMul`, `OpFDiv`, and ordered/unordered floating-point comparisons (`OpFOrd*`/`OpFUnord*`). Arithmetic uses IEEE-754 single-precision bit patterns, with tests covering addition, multiplication, negation, and ordered comparison. This is still not a general shader runtime: vector operations, memory/pointer access, interfaces, and graphics/compute pipeline execution remain unsupported.

### Vulkan buffer-copy conformance correction

The copy command now rejects identical source and destination buffer handles, matching Vulkan's command validity requirements. The earlier self-copy experiment was invalid Vulkan usage and has been replaced with a regression test confirming that the command buffer remains unchanged when such a copy is attempted.

## Progress estimate update (2026-10-21)

**Tracked implementation milestone: 50% (engineering estimate, not Vulkan conformance).** This is a coarse project tracking target after a bundled implementation batch; it does not mean half of Vulkan 1.0 or DXVK is implemented. Major missing pieces include graphics/compute pipelines, descriptors, render passes/framebuffers, shader interfaces and resource access, presentation, and DXVK compatibility.


### Scalar float conversion and classification

Expanded the scalar SPIR-V subset with 32-bit `OpConvertFToU`, `OpConvertFToS`, `OpConvertSToF`, `OpConvertUToF`, and `OpFConvert`, plus `OpIsNan`, `OpIsInf`, `OpIsFinite`, `OpIsNormal`, and `OpSignBitSet`. Float-to-integer conversions reject non-finite and out-of-range values before C casts to avoid undefined behavior. Tests exercise float-to-unsigned, signed-to-float, finite classification, and sign-bit detection.


### Additional scalar SPIR-V arithmetic

Added bounded 32-bit `OpFRem` and `OpFMod` support, including the distinct truncation-versus-floor behavior for negative operands. The interpreter rejects zero divisors and quotient magnitudes outside its safe integer-reduction range rather than performing an unsafe cast. The 32-bit `OpUConvert` and `OpSConvert` forms are also recognized as bit-preserving conversions in this scalar-only implementation. Regression tests cover positive remainder and negative modulo behavior.

## Progress estimate update (2026-10-21)

**Tracked implementation milestone: 70% (engineering estimate, not Vulkan conformance).** This is a coarse internal project milestone for cumulative implementation effort, not a claim that 70% of Vulkan 1.0 or DXVK is implemented. Graphics/compute pipeline execution, descriptor/resource binding, render passes/framebuffers, full shader interfaces/resource access, presentation/swapchains, and broad DXVK compatibility remain unimplemented.


### Cross-region buffer alias validation

Hardened `vkCmdCopyBuffer` for distinct buffer handles that share one device-memory allocation. The runtime now validates every source region against every destination region before recording any operations, rejecting cross-region aliasing atomically. Regression coverage constructs two buffer objects bound to overlapping portions of one allocation and verifies the invalid batch records zero commands.

This is a targeted correctness improvement, not a large percentage jump in Vulkan coverage. Graphics/compute pipelines, descriptor sets, render passes/framebuffers, shader resource access, presentation, and DXVK compatibility remain major missing areas.


### Buffer alias regression test ordering fix

Moved the cross-region alias test after the original fill/update submission and readback. The test now verifies the invalid multi-region copy records no commands without resetting and discarding the command buffer used by the pre-existing fill/update assertions. The latest CI run exposed this test-ordering regression; the fix must be validated by a fresh run.


### First command-buffer software triangle draw bridge

Added an explicitly experimental SentinelOS-only `SentinelVulkanCmdDrawTriangle` command that records three pre-transformed vertices into a command buffer and executes them through the CPU triangle rasterizer on a bound color-attachment image. It validates command-buffer state, device ownership, image usage/format/size, memory binding, and rasterizer outcome. The queue advertises graphics capability and supported RGBA/BGRA formats advertise color-attachment support. CI now compiles the Vulkan smoke test with the rasterizer and checks a covered pixel plus an untouched pixel after queue submission.

**Tracked implementation milestone: 75% (engineering estimate, not Vulkan conformance).** This is a small real rendering bridge, not core Vulkan graphics-pipeline support: standard `vkCmdDraw`, shader-driven vertex processing, pipeline state, descriptor sets, render passes/framebuffers, and presentation/swapchains remain unimplemented. DXVK compatibility remains unverified.
