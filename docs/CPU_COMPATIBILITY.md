# CPU compatibility

## Implemented baseline

The kernel targets x86-64 and assumes the architectural x86-64 baseline,
including CPUID and SSE2. BSP and started application processors enable x87/SSE
execution and initialize MXCSR before kernel or user execution. CPU vendor,
brand string, family, model, stepping, and selected CPUID feature bits are
reported during initialization.

The kernel does not enable AVX, AVX2, or XSAVE state management. Seeing those
bits in the diagnostic line only means the processor advertises the feature;
the current scheduler does not save and restore extended vector state per
thread. Code that requires those instructions must not be used in kernel or
userland until context-state support is implemented.

AP startup is based on ACPI MADT processor entries and an identity-mapped,
below-1-MiB INIT/SIPI trampoline supplied by UEFI. xAPIC destinations above
255 cannot be started; x2APIC operation is used only when firmware has already
enabled it. If ACPI tables, the trampoline, or AP initialization are
unavailable, the system continues on the BSP when possible. CPU discovery and
the CPUID diagnostic have been compile-checked; physical Intel/AMD coverage and
multi-CPU runtime behavior have not been verified as part of this change.
