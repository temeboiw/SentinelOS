# SentinelOS Vulkan / DXVK integration status

## Current repository state

The current graphics driver exposes VirtIO-GPU 2D scanout and an experimental
VirGL/Gallium path. Neither is a Vulkan ICD. The kernel has no complete Vulkan
object model, validated command-buffer submission interface, Vulkan memory
requirements, fences/semaphores, shader/pipeline compiler, or swapchain
implementation. DXVK is not integrated.

`user/vulkan_sentinel.h` is a bring-up ABI worksheet for SentinelOS-owned code.
It is intentionally prefixed `SVk` and is **not** a substitute for the official
Khronos Vulkan headers. It is not a loader, ICD, or implementation. In
particular, its shortened property structure is not ABI-compatible with
`VkPhysicalDeviceProperties`; do not pass it to ordinary Vulkan applications.

## Required full integration

A complete implementation needs these layers:

1. Khronos Vulkan headers and a loader with correct dispatchable/non-dispatchable
   handles, proc-address rules, extension/layer negotiation, and ABI layout.
2. A kernel graphics ioctl/syscall contract with process-owned handles, checked
   buffers, GPU/CPU-visible memory allocation, command submission, synchronization,
   reset/device-loss handling, and presentation. Never accept caller-supplied
   physical addresses.
3. A real backend. VirtIO-GPU VirGL is not Vulkan by itself. A Venus backend
   requires the compatible virtio protocol and host implementation; a native
   Intel/AMD/NVIDIA backend requires GPU-family-specific driver code.
4. Shader modules, SPIR-V validation, pipelines, descriptor sets, image/buffer
   formats and layouts, barriers, render/compute execution, and accurate feature
   reporting. Unsupported features must return the corresponding Vulkan error.
5. Device enumeration, allocation/map/unmap, buffer/image, command submission,
   fence, clear/render, presentation, timeout/device-loss, and no-driver fallback
   tests running in the SentinelOS guest.
6. DXVK and its dependencies, then the required Windows PE loader behavior,
   TLS/unwind support, synchronization, virtual memory, DLL imports, and a
   working presentation bridge. DXVK must not be advertised until a real Vulkan
   ICD meets the version/features needed by the selected DXVK release.

## Verification gate

Do not set a Vulkan capability bit or report a Vulkan device based on PCI
enumeration, a framebuffer, VirGL availability, or this header. The gate is
passed only when an actual Vulkan program can enumerate a real backend, create
resources, submit work, synchronize, render, and present inside SentinelOS.
DXVK is a separate gate: run an actual Direct3D application through DXVK and
verify the rendered output and error paths.

Existing 2D scanout and firmware-framebuffer fallback must remain available
when Vulkan initialization fails.
