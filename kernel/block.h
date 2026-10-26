#pragma once
#include <stdint.h>

#define SENTINEL_BLOCK_MAX_DEVICES 16u
#define SENTINEL_BLOCK_NAME_MAX 32u
#define SENTINEL_BLOCK_ABI_VERSION 1u

typedef int (*SENTINEL_BLOCK_READ)(void *context, uint64_t offset,
                                   void *destination, uint64_t length);
typedef int (*SENTINEL_BLOCK_WRITE)(void *context, uint64_t offset,
                                    const void *source, uint64_t length);

typedef struct SENTINEL_BLOCK_DEVICE {
    uint32_t size;
    uint32_t abi_version;
    char name[SENTINEL_BLOCK_NAME_MAX];
    uint32_t sector_size;
    uint32_t flags;
    uint64_t sector_count;
    void *context;
    SENTINEL_BLOCK_READ read;
    SENTINEL_BLOCK_WRITE write;
    int online;
} SENTINEL_BLOCK_DEVICE;

static SENTINEL_BLOCK_DEVICE SentinelBlockDevices[SENTINEL_BLOCK_MAX_DEVICES];
static uint32_t SentinelBlockDeviceCount;
static int SentinelBlockPreferred = -1;

static int SentinelBlockRegister(const SENTINEL_BLOCK_DEVICE *device) {
    if (!device || device->size < sizeof(SENTINEL_BLOCK_DEVICE) ||
        device->abi_version != SENTINEL_BLOCK_ABI_VERSION ||
        !device->name[0] || !device->sector_size || !device->sector_count ||
        !device->read) return -1;
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        const SENTINEL_BLOCK_DEVICE *old = &SentinelBlockDevices[i];
        uint32_t n = 0;
        while (n < SENTINEL_BLOCK_NAME_MAX && old->name[n] &&
               device->name[n] && old->name[n] == device->name[n]) ++n;
        if (n == SENTINEL_BLOCK_NAME_MAX ||
            (!old->name[n] && !device->name[n])) return -2;
    }
    if (SentinelBlockDeviceCount >= SENTINEL_BLOCK_MAX_DEVICES) return -3;
    SENTINEL_BLOCK_DEVICE *dst = &SentinelBlockDevices[SentinelBlockDeviceCount];
    *dst = *device;
    dst->online = 1;
    if (SentinelBlockPreferred < 0) SentinelBlockPreferred = (int)SentinelBlockDeviceCount;
    ++SentinelBlockDeviceCount;
    return 0;
}

/* Remove a device only when both its registered name and context match.
 * This prevents a driver from accidentally removing another driver's device. */
static int SentinelBlockUnregister(const char *name, void *context) {
    if (!name || !name[0]) return -1;
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        SENTINEL_BLOCK_DEVICE *device = &SentinelBlockDevices[i];
        uint32_t n = 0;
        while (n < SENTINEL_BLOCK_NAME_MAX && name[n] &&
               device->name[n] == name[n]) ++n;
        if (n >= SENTINEL_BLOCK_NAME_MAX || name[n] ||
            device->name[n] || device->context != context) continue;

        for (uint32_t j = i + 1; j < SentinelBlockDeviceCount; ++j)
            SentinelBlockDevices[j - 1] = SentinelBlockDevices[j];
        --SentinelBlockDeviceCount;
        for (uint32_t j = 0; j < SENTINEL_BLOCK_MAX_DEVICES; ++j)
            if (j >= SentinelBlockDeviceCount)
                SentinelBlockDevices[j] = (SENTINEL_BLOCK_DEVICE){0};

        if (SentinelBlockPreferred == (int)i) {
            SentinelBlockPreferred = SentinelBlockDeviceCount ? 0 : -1;
        } else if (SentinelBlockPreferred > (int)i) {
            --SentinelBlockPreferred;
        }
        return 0;
    }
    return -2;
}

/* True while any published device still contains a pointer into an
 * image. The loader uses this after a driver's rollback callback. */
static int SentinelBlockReferencesRange(uintptr_t base, uint64_t bytes) {
    if (!base || !bytes || base > UINTPTR_MAX - bytes) return 1;
    uintptr_t end = base + (uintptr_t)bytes;
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        const SENTINEL_BLOCK_DEVICE *device = &SentinelBlockDevices[i];
        uintptr_t context = (uintptr_t)device->context;
        uintptr_t read_fn = (uintptr_t)device->read;
        uintptr_t write_fn = (uintptr_t)device->write;
        if ((context >= base && context < end) ||
            (read_fn >= base && read_fn < end) ||
            (write_fn >= base && write_fn < end)) return 1;
    }
    return 0;
}

static SENTINEL_BLOCK_DEVICE *SentinelBlockGet(uint32_t index) {
    return index < SentinelBlockDeviceCount ? &SentinelBlockDevices[index] : 0;
}

static SENTINEL_BLOCK_DEVICE *SentinelBlockFind(const char *name) {
    if (!name) return 0;
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        uint32_t n = 0;
        while (n < SENTINEL_BLOCK_NAME_MAX && name[n] &&
               SentinelBlockDevices[i].name[n] == name[n]) ++n;
        if (n < SENTINEL_BLOCK_NAME_MAX && !name[n] &&
            !SentinelBlockDevices[i].name[n] && SentinelBlockDevices[i].online)
            return &SentinelBlockDevices[i];
    }
    return 0;
}

static int SentinelBlockReadDevice(SENTINEL_BLOCK_DEVICE *device,
                                   uint64_t offset, void *dst, uint64_t length) {
    if (!device || !device->online || !device->read || (!dst && length) ||
        offset > device->sector_count * (uint64_t)device->sector_size ||
        length > device->sector_count * (uint64_t)device->sector_size - offset)
        return -1;
    return device->read(device->context, offset, dst, length);
}

static int SentinelBlockWriteDevice(SENTINEL_BLOCK_DEVICE *device,
                                    uint64_t offset, const void *src, uint64_t length) {
    if (!device || !device->online || !device->write || (!src && length) ||
        offset > device->sector_count * (uint64_t)device->sector_size ||
        length > device->sector_count * (uint64_t)device->sector_size - offset)
        return -1;
    return device->write(device->context, offset, src, length);
}

static int SentinelBlockRead(uint64_t offset, void *dst, uint64_t length) {
    if (SentinelBlockPreferred < 0 ||
        (uint32_t)SentinelBlockPreferred >= SentinelBlockDeviceCount) return -1;
    return SentinelBlockReadDevice(&SentinelBlockDevices[SentinelBlockPreferred],
                                   offset, dst, length);
}

static int SentinelBlockWrite(uint64_t offset, const void *src, uint64_t length) {
    if (SentinelBlockPreferred < 0 ||
        (uint32_t)SentinelBlockPreferred >= SentinelBlockDeviceCount) return -1;
    return SentinelBlockWriteDevice(&SentinelBlockDevices[SentinelBlockPreferred],
                                    offset, src, length);
}
