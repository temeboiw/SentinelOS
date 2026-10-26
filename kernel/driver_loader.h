#pragma once
/*
 * In-memory SentinelOS kernel driver loader.
 *
 * Modules use only SENTINEL_DRIVER_SERVICES; PE imports are validated and
 * then rejected until an explicit resolver exists. This keeps unresolved
 * Windows imports from becoming calls through an untrusted IAT.
 */
#include <stdint.h>
#include <stddef.h>
#include "driver.h"
#include "driver_pe.h"
#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "ext4k.h"

#define SENTINEL_DRIVER_IMAGE_MAX (32u * 1024u * 1024u)
#define SENTINEL_DRIVER_PAGE_SIZE 4096u

static void SentinelDriverServiceDebugPrint(const char *message) {
    if (message) DbgPrint("%s", message);
}

static void *SentinelDriverServiceAllocate(uint64_t bytes, uint32_t alignment) {
    if (!bytes) return 0;
    if (alignment < 16) alignment = 16;
    if ((alignment & (alignment - 1u)) || alignment > SENTINEL_DRIVER_PAGE_SIZE)
        return 0;
    if (bytes > (uint64_t)SIZE_MAX - alignment - sizeof(void *)) return 0;
    void *raw = kmalloc((size_t)(bytes + alignment - 1u + sizeof(void *)));
    if (!raw) return 0;
    uintptr_t start = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (start + alignment - 1u) & ~((uintptr_t)alignment - 1u);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

static void SentinelDriverServiceRelease(void *allocation) {
    if (allocation) kfree(((void **)allocation)[-1]);
}

static int32_t SentinelDriverServiceMapDeviceMemory(
    uint64_t physical, uint64_t bytes, volatile void **virtual_address) {
    if (!virtual_address || !bytes || physical > UINT64_MAX - bytes)
        return -1;
    if (!vmm_map_mmio(physical, bytes)) return -2;
    *virtual_address = (volatile void *)(uintptr_t)(HHDM_BASE + physical);
    return 0;
}

static int32_t SentinelDriverServiceAllocateDma(
    uint64_t bytes, uint32_t alignment,
    uint64_t *physical_address, volatile void **virtual_address) {
    if (!bytes || !physical_address || !virtual_address) return -1;
    if (alignment < SENTINEL_DRIVER_PAGE_SIZE) alignment = SENTINEL_DRIVER_PAGE_SIZE;
    if (alignment != SENTINEL_DRIVER_PAGE_SIZE) return -2;
    if (bytes > UINT64_MAX - (SENTINEL_DRIVER_PAGE_SIZE - 1u)) return -3;
    uint64_t pages = (bytes + SENTINEL_DRIVER_PAGE_SIZE - 1u) /
                     SENTINEL_DRIVER_PAGE_SIZE;
    uint64_t physical = pmm_alloc_n(pages);
    if (!physical) return -4;
    uint8_t *memory = (uint8_t *)(uintptr_t)(HHDM_BASE + physical);
    for (uint64_t i = 0; i < pages * SENTINEL_DRIVER_PAGE_SIZE; ++i)
        memory[i] = 0;
    *physical_address = physical;
    *virtual_address = memory;
    return 0;
}

static void SentinelDriverServiceReleaseDma(uint64_t physical_address,
                                             uint64_t bytes) {
    if (!bytes || !physical_address ||
        (physical_address & (SENTINEL_DRIVER_PAGE_SIZE - 1u)) ||
        bytes > UINT64_MAX - (SENTINEL_DRIVER_PAGE_SIZE - 1u))
        return;
    uint64_t pages = (bytes + SENTINEL_DRIVER_PAGE_SIZE - 1u) /
                     SENTINEL_DRIVER_PAGE_SIZE;
    for (uint64_t i = 0; i < pages; ++i)
        pmm_free(physical_address + i * SENTINEL_DRIVER_PAGE_SIZE);
}

static int32_t SentinelDriverServiceRegisterBlockDevice(
    const struct SENTINEL_BLOCK_DEVICE *device) {
    return SentinelBlockRegister(device);
}

static int32_t SentinelDriverServiceUnregisterBlockDevice(
    const char *name, void *context) {
    return SentinelBlockUnregister(name, context);
}

static const SENTINEL_DRIVER_SERVICES SentinelDriverServices = {
    sizeof(SENTINEL_DRIVER_SERVICES),
    SENTINEL_DRIVER_ABI_VERSION,
    SentinelDriverServiceDebugPrint,
    SentinelDriverServiceAllocate,
    SentinelDriverServiceRelease,
    SentinelDriverServiceMapDeviceMemory,
    {0},
    SentinelDriverServiceAllocateDma,
    SentinelDriverServiceReleaseDma,
    SentinelDriverServiceRegisterBlockDevice,
    {0},
    SentinelDriverServiceUnregisterBlockDevice
};

static int SentinelDriverHasSysSuffix(const char *name) {
    if (!name) return 0;
    uint32_t n = 0;
    while (name[n] && n < SENTINEL_DRIVER_NAME_MAX) ++n;
    if (n < 5 || n >= SENTINEL_DRIVER_NAME_MAX) return 0;
    const char *s = name + n - 4;
    return s[0] == '.' &&
        (s[1] == 's' || s[1] == 'S') &&
        (s[2] == 'y' || s[2] == 'Y') &&
        (s[3] == 's' || s[3] == 'S');
}

static int SentinelDriverPeRvaExecutable(const uint8_t *file,
                                        const SENTINEL_PE_IMAGE_INFO *info,
                                        uint32_t rva) {
    if (!file || !info) return 0;
    for (uint32_t i = 0; i < info->section_count; ++i) {
        const uint8_t *section = file + info->section_table_offset + (uint64_t)i * 40u;
        uint32_t va = SentinelPeRead32(section + 12);
        uint32_t virtual_size = SentinelPeRead32(section + 8);
        uint32_t raw_size = SentinelPeRead32(section + 16);
        uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
        uint32_t flags = SentinelPeRead32(section + 36);
        if ((flags & SENTINEL_PE_SCN_MEM_EXECUTE) &&
            rva >= va && (uint64_t)rva - va < span)
            return 1;
    }
    return 0;
}

/*
 * Load one trusted, already-read .sys file. The caller owns the input buffer.
 * The image stays allocated for the lifetime of the registered driver.
 * Modules must be self-contained and use the versioned service table; import
 * resolution and unload are deliberately constrained by the current kernel.
 */
static int SentinelDriverLoadImage(const char *name,
                                   const void *file,
                                   uint64_t file_size) {
    if (!SentinelDriverRegistryReady || !SentinelDriverHasSysSuffix(name) ||
        !file || !file_size)
        return -1;
    if (SentinelDriverFind(name)) return -2;

    SENTINEL_PE_IMAGE_INFO info = {0};
    int valid = SentinelPeValidateDriverFile(file, file_size, &info);
    if (valid) return -3;
    if (!info.image_size || info.image_size > SENTINEL_DRIVER_IMAGE_MAX)
        return -4;

    void *image = kzalloc(info.image_size);
    if (!image) return -7;
    if (SentinelPeMapImage(file, file_size, image, info.image_size, &info) != 0) {
        kfree(image);
        return -8;
    }
    if (info.import_rva || info.import_size) {
        if (SentinelPeValidateImports(image, info.image_size, &info) != 0) {
            kfree(image);
            return -9;
        }
        /* No implicit kernel export table: unresolved imports are not callable. */
        kfree(image);
        return -10;
    }
    if (SentinelPeApplyRelocations(image, info.image_size, &info,
                                   (uint64_t)(uintptr_t)image) != 0) {
        kfree(image);
        return -11;
    }
    if (!SentinelDriverPeRvaExecutable((const uint8_t *)file, &info,
                                       info.entry_rva)) {
        kfree(image);
        return -12;
    }

    SENTINEL_DRIVER_INITIALIZE initialize =
        (SENTINEL_DRIVER_INITIALIZE)((uintptr_t)image + info.entry_rva);
    SENTINEL_DRIVER_OBJECT candidate = {0};
    candidate.size = sizeof(candidate);
    candidate.abi_version = SENTINEL_DRIVER_ABI_VERSION;
    candidate.signature = SENTINEL_DRIVER_SIGNATURE;
    candidate.state = SENTINEL_DRIVER_LOADING;
    candidate.module_base = image;
    candidate.module_size = info.image_size;
    for (uint32_t i = 0; i < SENTINEL_DRIVER_NAME_MAX; ++i) {
        candidate.name[i] = name[i];
        if (!name[i]) break;
    }

    int32_t status = initialize(&candidate, &SentinelDriverServices);

    /* Validate a module-provided unload pointer before ever calling it. */
    int unload_valid = 0;
    if (candidate.unload) {
        uintptr_t unload_address = (uintptr_t)candidate.unload;
        uintptr_t base = (uintptr_t)image;
        unload_valid = unload_address >= base &&
            unload_address - base < info.image_size &&
            SentinelDriverPeRvaExecutable((const uint8_t *)file, &info,
                (uint32_t)(unload_address - base));
    }

    if (status != 0 ||
        candidate.size != sizeof(candidate) ||
        candidate.abi_version != SENTINEL_DRIVER_ABI_VERSION ||
        candidate.signature != SENTINEL_DRIVER_SIGNATURE ||
        candidate.module_base != image ||
        candidate.module_size != info.image_size) {
        /*
         * DriverEntry may have published device callbacks before failing.
         * The current block registry has no unregister operation, so freeing
         * this image could leave registry entries pointing into freed memory.
         * Keep the failed image resident and offline it through a validated
         * unload callback when available. This bounded leak is safer than a
         * kernel use-after-free until transactional device registration exists.
         */
        if (unload_valid) candidate.unload(&candidate);
        if (!SentinelBlockReferencesRange((uintptr_t)image, info.image_size))
            kfree(image);
        return status ? -13 : -14;
    }

    if (candidate.unload && !unload_valid) {
        kfree(image);
        return -17;
    }

    SENTINEL_DRIVER_MODULE_DESCRIPTOR module = {0};
    module.size = sizeof(module);
    module.abi_version = SENTINEL_DRIVER_ABI_VERSION;
    module.signature = SENTINEL_DRIVER_SIGNATURE;
    module.flags = candidate.flags;
    module.name = name;
    module.image_size = info.image_size;
    module.initialize = initialize;
    module.unload = candidate.unload;

    int registered = SentinelDriverRegister(&module, image,
                                             candidate.unload, 0);
    if (registered) {
        if (candidate.unload) candidate.unload(&candidate);
        if (!SentinelBlockReferencesRange((uintptr_t)image, info.image_size))
            kfree(image);
        return -16;
    }

    SENTINEL_DRIVER_OBJECT *object = SentinelDriverFind(name);
    if (!object) {
        if (candidate.unload) candidate.unload(&candidate);
        if (!SentinelBlockReferencesRange((uintptr_t)image, info.image_size))
            kfree(image);
        return -15;
    }
    object->flags = candidate.flags;
    object->device_context = candidate.device_context;
    object->unload = candidate.unload;
    return 0;
}

/*
 * Unload a registered driver by name. This is a serialized lifecycle operation:
 * callers must prevent concurrent dispatch into the module while it is unloading.
 * The image is retained if teardown leaves any registered block callback/context
 * pointing into it, avoiding a dangling function pointer at the cost of a leak.
 */
static int SentinelDriverUnloadByName(const char *name) {
    if (!SentinelDriverRegistryReady || !name || !name[0]) return -1;

    SENTINEL_DRIVER_OBJECT *object = SentinelDriverFind(name);
    if (!object) return -2;
    if (object->state != SENTINEL_DRIVER_RUNNING) return -3;
    if (!object->module_base || !object->module_size ||
        object->module_size > SENTINEL_DRIVER_IMAGE_MAX)
        return -4;

    uint32_t index = SENTINEL_DRIVER_REGISTRY_MAX;
    for (uint32_t i = 0; i < SENTINEL_DRIVER_REGISTRY_MAX; ++i) {
        if (SentinelDriverRegistry[i].used &&
            &SentinelDriverRegistry[i].object == object) {
            index = i;
            break;
        }
    }
    if (index == SENTINEL_DRIVER_REGISTRY_MAX) return -5;

    void *image = object->module_base;
    uint64_t image_size = object->module_size;
    SENTINEL_DRIVER_UNLOAD unload = object->unload;
    object->state = SENTINEL_DRIVER_UNLOADING;

    if (unload) unload(object);

    if (SentinelBlockReferencesRange((uintptr_t)image, image_size)) {
        object->state = SENTINEL_DRIVER_FAILED;
        object->last_status = -6;
        DbgPrint("driver: %s teardown left registered callbacks; image retained\n",
                 object->name);
        return -6;
    }

    kfree(image);
    for (uint32_t i = 0; i < sizeof(*object); ++i)
        ((uint8_t *)object)[i] = 0;
    object->state = SENTINEL_DRIVER_UNLOADED;
    SentinelDriverRegistry[index].used = 0;
    if (SentinelDriverRegistryCount) --SentinelDriverRegistryCount;
    DbgPrint("driver: %s unloaded\n", name);
    return 0;
}

/* Read a single module from /magic/System32/drivers on the mounted ext4
 * volume. The file is staged in kernel memory, validated, then released. */
static int SentinelDriverLoadFromDisk(const char *name) {
    if (!g_ext4_ok || !SentinelDriverHasSysSuffix(name)) return -1;
    uint32_t magic = e4_lookup(&g_ext4, 2, "magic");
    uint32_t system32 = magic ? e4_lookup(&g_ext4, magic, "System32") : 0;
    uint32_t drivers = system32 ? e4_lookup(&g_ext4, system32, "drivers") : 0;
    uint32_t ino = drivers ? e4_lookup(&g_ext4, drivers, name) : 0;
    if (!ino) return -2;

    uint8_t inode[256];
    if (e4_inode(&g_ext4, ino, inode) < 0 ||
        (E16(inode, 0) & 0xF000u) != 0x8000u)
        return -3;
    uint64_t size = e4_size(inode);
    if (!size || size > SENTINEL_DRIVER_IMAGE_MAX) return -4;

    uint8_t *file = (uint8_t *)kmalloc((size_t)size);
    if (!file) return -5;
    uint64_t pos = 0;
    uint32_t block_size = g_ext4.blksz;
    if (!block_size || block_size > 4096) {
        kfree(file);
        return -6;
    }
    while (pos < size) {
        uint64_t block_index = pos / block_size;
        if (block_index > UINT32_MAX) {
            kfree(file);
            return -7;
        }
        uint64_t physical_block = e4_map(&g_ext4, inode,
                                         (uint32_t)block_index);
        if (!physical_block ||
            physical_block > UINT64_MAX / block_size) {
            kfree(file);
            return -8;
        }
        uint64_t chunk = size - pos;
        if (chunk > block_size) chunk = block_size;
        if (e4_read(&g_ext4, physical_block * block_size,
                    file + pos, chunk) < 0) {
            kfree(file);
            return -9;
        }
        pos += chunk;
    }

    int result = SentinelDriverLoadImage(name, file, size);
    kfree(file);
    return result;
}


/*
 * Enumerate /magic/System32/drivers and attempt every regular .sys module.
 * Directory records are validated before their names are copied; the scan is
 * bounded so a corrupt filesystem cannot make boot spend unbounded time here.
 * Drivers requiring unresolved PE imports are intentionally rejected by the
 * single-image loader rather than being called with an incomplete IAT.
 */
static int SentinelDriverLoadAllFromDisk(void) {
    if (!g_ext4_ok || !g_ext4.blksz || g_ext4.blksz > 4096) return -1;

    uint32_t magic = e4_lookup(&g_ext4, 2, "magic");
    uint32_t system32 = magic ? e4_lookup(&g_ext4, magic, "System32") : 0;
    uint32_t drivers = system32 ? e4_lookup(&g_ext4, system32, "drivers") : 0;
    if (!drivers) return -2;

    uint8_t dir_inode[256];
    if (e4_inode(&g_ext4, drivers, dir_inode) < 0 ||
        (E16(dir_inode, 0) & 0xF000u) != 0x4000u)
        return -3;

    uint64_t dir_size = e4_size(dir_inode);
    if (!dir_size || dir_size > 1024u * 1024u) return -4;

    uint8_t block[4096];
    char name[SENTINEL_DRIVER_NAME_MAX];
    int attempted = 0, loaded = 0;
    uint32_t block_size = g_ext4.blksz;

    for (uint64_t pos = 0; pos < dir_size; ) {
        uint64_t logical = pos / block_size;
        if (logical > UINT32_MAX) return loaded ? loaded : -5;
        uint64_t physical = e4_map(&g_ext4, dir_inode, (uint32_t)logical);
        if (!physical || physical > UINT64_MAX / block_size ||
            e4_read(&g_ext4, physical * block_size, block, block_size) < 0)
            return loaded ? loaded : -6;

        uint32_t valid = (uint32_t)(dir_size - pos);
        if (valid > block_size) valid = block_size;
        for (uint32_t off = 0; off + 8u <= valid; ) {
            uint16_t rec_len = E16(block, off + 4);
            uint8_t name_len = block[off + 6];
            if (rec_len < 8u || (rec_len & 3u) || rec_len > valid - off ||
                name_len > rec_len - 8u)
                break;

            uint32_t ino = E32(block, off);
            if (ino && name_len > 0 && name_len < SENTINEL_DRIVER_NAME_MAX) {
                uint32_t k = 0;
                while (k < name_len) {
                    name[k] = (char)block[off + 8u + k];
                    ++k;
                }
                name[k] = 0;

                if (SentinelDriverHasSysSuffix(name)) {
                    int is_ata = SentinelDriverNameEqual(name, "ata.sys");
                    if (is_ata && SentinelBlockFind("ata0")) {
                        DbgPrint("driver: %s skipped; ata0 is already registered\n", name);
                    } else {
                        ++attempted;
                        int status = SentinelDriverLoadFromDisk(name);
                        if (status == 0) {
                            ++loaded;
                            DbgPrint("driver: %s loaded from /magic/System32/drivers\n", name);
                        } else {
                            DbgPrint("driver: %s load failed status=%d\n", name, status);
                        }
                    }
                }
            }
            off += rec_len;
        }
        pos += valid;
    }

    DbgPrint("driver: .sys scan complete attempted=%d loaded=%d\n",
             attempted, loaded);
    return loaded;
}
