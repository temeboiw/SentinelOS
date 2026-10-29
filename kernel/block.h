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
static volatile uint32_t SentinelBlockLockWord;
static uint32_t SentinelBlockInFlight[SENTINEL_BLOCK_MAX_DEVICES];

#ifndef SENTINEL_BLOCK_SPIN_WAIT
#define SENTINEL_BLOCK_SPIN_WAIT() __asm__ volatile("pause")
#endif

static void SentinelBlockLock(void) {
    while (__atomic_exchange_n(&SentinelBlockLockWord, 1u, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(&SentinelBlockLockWord, __ATOMIC_RELAXED))
            SENTINEL_BLOCK_SPIN_WAIT();
}
static void SentinelBlockUnlock(void) {
    __atomic_store_n(&SentinelBlockLockWord, 0u, __ATOMIC_RELEASE);
}
static int SentinelBlockNameLength(const char *name, uint32_t *length) {
    if (!name || !length) return 0;
    uint32_t n = 0;
    while (n < SENTINEL_BLOCK_NAME_MAX && name[n]) ++n;
    if (n == SENTINEL_BLOCK_NAME_MAX) return 0;
    *length = n;
    return n != 0;
}
static int SentinelBlockDeviceIndex(const SENTINEL_BLOCK_DEVICE *device) {
    uintptr_t p = (uintptr_t)device, base = (uintptr_t)SentinelBlockDevices;
    if (p < base) return -1;
    uintptr_t delta = p - base;
    if (delta >= sizeof(SentinelBlockDevices) || delta % sizeof(SentinelBlockDevices[0])) return -1;
    uint32_t index = (uint32_t)(delta / sizeof(SentinelBlockDevices[0]));
    return index < SentinelBlockDeviceCount ? (int)index : -1;
}
static int SentinelBlockCapacity(const SENTINEL_BLOCK_DEVICE *device, uint64_t *capacity) {
    if (!device || !capacity || !device->sector_size ||
        device->sector_count > UINT64_MAX / device->sector_size) return 0;
    *capacity = device->sector_count * (uint64_t)device->sector_size;
    return 1;
}
static int SentinelBlockBeginIoLocked(SENTINEL_BLOCK_DEVICE *device,
                                      uint64_t offset, uint64_t length,
                                      const void *buffer, int write,
                                      uint32_t *index_out) {
    int index = SentinelBlockDeviceIndex(device);
    uint64_t capacity;
    if (index < 0 || !device->online || !SentinelBlockCapacity(device, &capacity) ||
        offset > capacity || length > capacity - offset ||
        (length && !buffer) || (write ? !device->write : !device->read)) return 0;
    ++SentinelBlockInFlight[index];
    *index_out = (uint32_t)index;
    return 1;
}
static void SentinelBlockEndIo(uint32_t index) {
    SentinelBlockLock();
    if (index < SENTINEL_BLOCK_MAX_DEVICES && SentinelBlockInFlight[index])
        --SentinelBlockInFlight[index];
    SentinelBlockUnlock();
}

static int SentinelBlockRegister(const SENTINEL_BLOCK_DEVICE *device) {
    uint32_t name_length; uint64_t capacity;
    if (!device || device->size < sizeof(SENTINEL_BLOCK_DEVICE) ||
        device->abi_version != SENTINEL_BLOCK_ABI_VERSION ||
        !SentinelBlockNameLength(device->name, &name_length) ||
        !device->sector_count || !SentinelBlockCapacity(device, &capacity) ||
        !capacity || !device->read) return -1;
    (void)name_length;
    SentinelBlockLock();
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        const SENTINEL_BLOCK_DEVICE *old = &SentinelBlockDevices[i];
        if (!old->online) continue;
        uint32_t n = 0;
        while (n < SENTINEL_BLOCK_NAME_MAX && old->name[n] &&
               device->name[n] && old->name[n] == device->name[n]) ++n;
        if (n == SENTINEL_BLOCK_NAME_MAX ||
            (!old->name[n] && !device->name[n])) { SentinelBlockUnlock(); return -2; }
    }
    if (SentinelBlockDeviceCount >= SENTINEL_BLOCK_MAX_DEVICES) { SentinelBlockUnlock(); return -3; }
    SENTINEL_BLOCK_DEVICE *dst = &SentinelBlockDevices[SentinelBlockDeviceCount];
    *dst = *device;
    dst->online = 1;
    SentinelBlockInFlight[SentinelBlockDeviceCount] = 0;
    if (SentinelBlockPreferred < 0) SentinelBlockPreferred = (int)SentinelBlockDeviceCount;
    ++SentinelBlockDeviceCount;
    SentinelBlockUnlock();
    return 0;
}

/* Remove a device only when both its registered name and context match.
 * This prevents a driver from accidentally removing another driver's device. */
static int SentinelBlockUnregister(const char *name, void *context) {
    uint32_t name_length;
    if (!SentinelBlockNameLength(name, &name_length)) return -1;
    (void)name_length;
    SentinelBlockLock();
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        SENTINEL_BLOCK_DEVICE *device = &SentinelBlockDevices[i];
        uint32_t n = 0;
        while (n < SENTINEL_BLOCK_NAME_MAX && name[n] &&
               device->name[n] == name[n]) ++n;
        if (n >= SENTINEL_BLOCK_NAME_MAX || name[n] ||
            device->name[n] || device->context != context) continue;
        if (SentinelBlockInFlight[i]) { SentinelBlockUnlock(); return -3; }

        /* Keep the slot address stable: filesystem objects may retain this
         * pointer after registration. Tombstones are not reused, avoiding a
         * stale pointer silently becoming a different device. */
        device->online=0;device->read=NULL;device->write=NULL;device->context=NULL;
        device->name[0]=0;device->sector_size=0;device->sector_count=0;
        if (SentinelBlockPreferred == (int)i) {
            SentinelBlockPreferred=-1;
            for(uint32_t next=0;next<SentinelBlockDeviceCount;next++)
                if(SentinelBlockDevices[next].online){SentinelBlockPreferred=(int)next;break;}
        }
        SentinelBlockUnlock();
        return 0;
    }
    SentinelBlockUnlock();
    return -2;
}

/* True while any published device still contains a pointer into an
 * image. The loader uses this after a driver's rollback callback. */
static int SentinelBlockReferencesRange(uintptr_t base, uint64_t bytes) {
    if (!base || !bytes || base > UINTPTR_MAX - bytes) return 1;
    uintptr_t end = base + (uintptr_t)bytes;
    SentinelBlockLock();
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        const SENTINEL_BLOCK_DEVICE *device = &SentinelBlockDevices[i];
        uintptr_t context = (uintptr_t)device->context;
        uintptr_t read_fn = (uintptr_t)device->read;
        uintptr_t write_fn = (uintptr_t)device->write;
        if ((context >= base && context < end) ||
            (read_fn >= base && read_fn < end) ||
            (write_fn >= base && write_fn < end)) { SentinelBlockUnlock(); return 1; }
    }
    SentinelBlockUnlock();
    return 0;
}

static SENTINEL_BLOCK_DEVICE *SentinelBlockGet(uint32_t index) {
    SentinelBlockLock();
    SENTINEL_BLOCK_DEVICE *device = index < SentinelBlockDeviceCount &&
        SentinelBlockDevices[index].online ? &SentinelBlockDevices[index] : 0;
    SentinelBlockUnlock();
    return device;
}

static SENTINEL_BLOCK_DEVICE *SentinelBlockFind(const char *name) {
    uint32_t name_length;
    if (!SentinelBlockNameLength(name, &name_length)) return 0;
    (void)name_length;
    SentinelBlockLock();
    for (uint32_t i = 0; i < SentinelBlockDeviceCount; ++i) {
        uint32_t n = 0;
        while (n < SENTINEL_BLOCK_NAME_MAX && name[n] &&
               SentinelBlockDevices[i].name[n] == name[n]) ++n;
        if (n < SENTINEL_BLOCK_NAME_MAX && !name[n] &&
            !SentinelBlockDevices[i].name[n] && SentinelBlockDevices[i].online) {
            SentinelBlockUnlock();
            return &SentinelBlockDevices[i];
        }
    }
    SentinelBlockUnlock();
    return 0;
}

static int SentinelBlockReadDevice(SENTINEL_BLOCK_DEVICE *device,
                                   uint64_t offset, void *dst, uint64_t length) {
    uint32_t index; SENTINEL_BLOCK_READ read; void *context;
    SentinelBlockLock();
    if (!SentinelBlockBeginIoLocked(device, offset, length, dst, 0, &index)) {
        SentinelBlockUnlock(); return -1;
    }
    read=device->read;context=device->context;
    SentinelBlockUnlock();
    int result=read(context, offset, dst, length);
    SentinelBlockEndIo(index);
    return result;
}

static int SentinelBlockWriteDevice(SENTINEL_BLOCK_DEVICE *device,
                                    uint64_t offset, const void *src, uint64_t length) {
    uint32_t index; SENTINEL_BLOCK_WRITE write; void *context;
    SentinelBlockLock();
    if (!SentinelBlockBeginIoLocked(device, offset, length, src, 1, &index)) {
        SentinelBlockUnlock(); return -1;
    }
    write=device->write;context=device->context;
    SentinelBlockUnlock();
    int result=write(context, offset, src, length);
    SentinelBlockEndIo(index);
    return result;
}

static int SentinelBlockRead(uint64_t offset, void *dst, uint64_t length) {
    uint32_t index; SENTINEL_BLOCK_READ read; void *context;
    SentinelBlockLock();
    if (SentinelBlockPreferred < 0 ||
        (uint32_t)SentinelBlockPreferred >= SentinelBlockDeviceCount ||
        !SentinelBlockBeginIoLocked(&SentinelBlockDevices[SentinelBlockPreferred],
                                    offset,length,dst,0,&index)) {
        SentinelBlockUnlock(); return -1;
    }
    read=SentinelBlockDevices[index].read;context=SentinelBlockDevices[index].context;
    SentinelBlockUnlock();
    int result=read(context,offset,dst,length);
    SentinelBlockEndIo(index);
    return result;
}

static int SentinelBlockWrite(uint64_t offset, const void *src, uint64_t length) {
    uint32_t index; SENTINEL_BLOCK_WRITE write; void *context;
    SentinelBlockLock();
    if (SentinelBlockPreferred < 0 ||
        (uint32_t)SentinelBlockPreferred >= SentinelBlockDeviceCount ||
        !SentinelBlockBeginIoLocked(&SentinelBlockDevices[SentinelBlockPreferred],
                                    offset,length,src,1,&index)) {
        SentinelBlockUnlock(); return -1;
    }
    write=SentinelBlockDevices[index].write;context=SentinelBlockDevices[index].context;
    SentinelBlockUnlock();
    int result=write(context,offset,src,length);
    SentinelBlockEndIo(index);
    return result;
}
