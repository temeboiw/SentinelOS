# SentinelOS Win32 API implementation roadmap

## Compatibility goal

Implement a documented, testable Win32 compatibility surface for SentinelOS, one DLL per phase. This is not a promise to clone every Windows version's complete API at once: Win32 spans thousands of APIs and depends on kernel, loader, filesystem, scheduler, graphics, networking, and security features.

A DLL phase is complete only when:
- Every export in SentinelOS's declared compatibility contract has a real implementation or a documented, correct unsupported/error result.
- Calling conventions, argument layouts, return values, error codes, and A/W variants are checked.
- The implementation is tested with small user-mode conformance programs.
- The build and boot test are run; source-only changes are not described as runtime-verified.

## Phases

1. **kernel32.dll** — process/thread basics, handles, virtual memory, file I/O, clocks, error handling, paths, environment, and common string helpers. Kernel syscalls must be added where necessary; unsupported write/create/rename operations must not report success.
2. **user32.dll** — windows, window procedures, message queue, input, focus, keyboard/mouse, geometry, and window lifecycle.
3. **gdi32.dll** — DCs, objects, fonts, text metrics, bitmap operations, clipping, and drawing.
4. **advapi32.dll** — registry and security APIs, backed by real storage and access-control behavior.
5. **shell32.dll** — shell execution, known folders, command-line parsing, and file associations.
6. **ole32.dll** — COM initialization, apartment rules, GUIDs, class registration, and object lifetime.
7. **ws2_32.dll** — Winsock startup, sockets, TCP stream I/O, UDP, DNS, and error mapping.
8. **comctl32.dll / comdlg32.dll / winmm.dll / additional DLLs** — common controls, dialogs, multimedia, and application-driven compatibility.

## Current status

Phase 1 is the first target. The existing exports are a partial compatibility layer, not a complete Windows kernel32.dll. In particular, the current kernel only exposes read-only ext4 file opens, and process creation currently resolves boot-loaded images. APIs requiring filesystem mutation, arbitrary disk PE loading, or richer process/thread handles need kernel/loader work before they can be fully implemented.

Do not advertise an API as fully supported merely because its symbol is exported.