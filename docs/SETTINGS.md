# SentinelOS Settings

The native Settings application is built from `user/settings.c` as the PE32+
executable `magic/System32/settings.exe`. The Start menu launches that image.
Navigation works with the mouse, Up/Down plus Enter, or the number keys 1–0.
Closing the window exits the application normally.

Settings currently reads the active framebuffer mode and graphics backend,
kernel network-interface state and address, a kernel process snapshot, and
volume metadata when the filesystem provides the existing metadata syscall.
CPU vendor/family information comes from the processor's CPUID instruction;
the displayed build timestamp is compiled into the executable. These values
are refreshed when a page is selected or repainted.

The remaining pages identify capability gaps explicitly. SentinelOS currently
does not expose memory-capacity or uptime queries, dynamic display-mode changes,
desktop theme or wallpaper preferences, pointer-speed or keyboard-layout
controls, performance/power profiles, NTFS capacity metadata through the user
API, installed-application inventory/removal, or authenticated account
management. Settings does not persist user preferences because no suitable
configuration store is exposed. It does not claim to apply settings it cannot
apply.

The packaged-image contract is checked by `tests/native_settings_contract_test.sh`
after the root build has staged `build/rootfs`.
