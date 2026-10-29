# SentinelOS storage compatibility

This document records the kernel storage paths currently implemented. A device
is listed as supported only when a driver initializes it and performs bounded
block I/O; PCI identification alone is not sufficient.

| Component | Implemented coverage | Validation |
|---|---|---|
| VirtIO block | Modern VirtIO PCI block device on PCI bus 0; one queue, polling, byte reads and read/modify/write byte writes. Capacity comes from the device configuration and sector requests are range checked. | Host block registry contract test passes. No QEMU disk boot/read test was run for this change. |
| AHCI/SATA | First discovered PCI AHCI controller, BIOS handoff and HBA reset, implemented SATA ports, IDENTIFY capacity/model, LBA48 DMA, 512-byte logical sectors, bounded polling, and serialized per-disk requests. ATAPI is rejected. | Main kernel translation unit compiled; no AHCI QEMU or physical test recorded. |
| ATA/PATA | Primary-channel master in legacy compatibility I/O mode, IDENTIFY-based LBA28 capacity, 512-byte PIO reads and writes. No slave or secondary channel; no LBA48. | No QEMU or physical test recorded. |
| NVMe / USB mass storage | No kernel transport implementation is present. | Unsupported. |
| MBR / GPT | Four primary MBR entries, including protective-MBR detection; primary GPT entries (up to 128), header and entry-array CRC checks, usable-range validation, overlap rejection, and overflow/bounds checks. | `tests/partition_table_test.c` passes with host GCC. |
| ext4 | Mounted from a validated partition on a registered disk and constrained to that partition's byte range. Existing kernel ext4 code provides its current read/write feature subset. | Build/runtime status is reported by the repository build and emulator checks; this change did not verify a QEMU mount. |
| NTFS | Existing read-only implementation: ordinary uncompressed files whose MFT and unnamed `$DATA` streams fit a single runlist attribute. It does not implement writes, compression, encryption, ATTRIBUTE_LIST continuation, or directory-index lookup. | Existing parser and filesystem contract tests only; no new runtime test. |
| FAT32 / exFAT | No kernel filesystem mount implementation is present in this storage path. FAT support used by EFI boot/installer tools is separate. | Unsupported by the running kernel filesystem layer. |

The block registry exposes bounded byte-addressed reads and writes plus a
controller name and model string when the device reports one. `SentinelBlockSnapshot`
copies enumeration metadata; consumers must reacquire a live registry pointer with
`SentinelBlockGet` before issuing I/O. `SentinelBlockByteCapacity` performs checked
capacity arithmetic. Discovery never formats or writes a disk.

The test performed for this increment was:

```text
gcc -std=c11 -Wall -Wextra -Wno-unused-function -pthread -o /tmp/sentinel-kernel-io-test tests/kernel_io_test.c
/tmp/sentinel-kernel-io-test
kernel I/O manager and block-device contracts: PASS

gcc -std=c11 -Wall -Wextra -Werror -o /tmp/sentinel-partition-test tests/partition_table_test.c
/tmp/sentinel-partition-test
MBR/GPT parser tests: PASS
```
