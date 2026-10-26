#pragma once
/*
 * SentinelOS kernel driver ABI.
 *
 * Driver modules are kernel-mode code. A module must be validated, mapped,
 * relocated, and have all imports resolved before DriverEntry is invoked.
 * Do not cast arbitrary PE entry points to DRIVER_INITIALIZE.
 */
#include <stdint.h>

#define SENTINEL_DRIVER_ABI_VERSION 1u
#define SENTINEL_DRIVER_NAME_MAX 64u
#define SENTINEL_DRIVER_REGISTRY_MAX 32u
#define SENTINEL_DRIVER_SIGNATURE 0x56524453u /* "SDRV" */

typedef struct SENTINEL_DRIVER_OBJECT SENTINEL_DRIVER_OBJECT;
typedef struct SENTINEL_DRIVER_SERVICES SENTINEL_DRIVER_SERVICES;

typedef int32_t (*SENTINEL_DRIVER_INITIALIZE)(
    SENTINEL_DRIVER_OBJECT *driver,
    const SENTINEL_DRIVER_SERVICES *services);
typedef void (*SENTINEL_DRIVER_UNLOAD)(SENTINEL_DRIVER_OBJECT *driver);

/* Kernel services deliberately use a versioned table instead of unresolved
 * imports. Keep this structure append-only; check size/version before access. */
struct SENTINEL_DRIVER_SERVICES {
    uint32_t size;
    uint32_t abi_version;
    void (*debug_print)(const char *message);
    void *(*allocate)(uint64_t bytes, uint32_t alignment);
    void (*release)(void *allocation);
    int32_t (*map_device_memory)(uint64_t physical, uint64_t bytes,
                                 volatile void **virtual_address);
    uint64_t reserved[8];
};

struct SENTINEL_DRIVER_OBJECT {
    uint32_t size;
    uint32_t abi_version;
    uint32_t signature;
    uint32_t flags;
    char name[SENTINEL_DRIVER_NAME_MAX];
    void *module_base;
    uint64_t module_size;
    void *device_context;
    SENTINEL_DRIVER_UNLOAD unload;
    uint32_t state;
    int32_t last_status;
    uint64_t reserved[4];
};

typedef struct SENTINEL_DRIVER_MODULE_DESCRIPTOR {
    uint32_t size;
    uint32_t abi_version;
    uint32_t signature;
    uint32_t flags;
    const char *name;
    uint64_t image_size;
    SENTINEL_DRIVER_INITIALIZE initialize;
    SENTINEL_DRIVER_UNLOAD unload;
    uint64_t reserved[4];
} SENTINEL_DRIVER_MODULE_DESCRIPTOR;

enum {
    SENTINEL_DRIVER_UNLOADED = 0,
    SENTINEL_DRIVER_LOADING = 1,
    SENTINEL_DRIVER_RUNNING = 2,
    SENTINEL_DRIVER_FAILED = 3
};

typedef struct SENTINEL_DRIVER_REGISTRY_ENTRY {
    SENTINEL_DRIVER_OBJECT object;
    int used;
} SENTINEL_DRIVER_REGISTRY_ENTRY;

static SENTINEL_DRIVER_REGISTRY_ENTRY SentinelDriverRegistry[SENTINEL_DRIVER_REGISTRY_MAX];
static uint32_t SentinelDriverRegistryCount;
static int SentinelDriverRegistryReady;

static int SentinelDriverNameEqual(const char *a, const char *b) {
    if (!a || !b) return 0;
    for (uint32_t i = 0; i < SENTINEL_DRIVER_NAME_MAX; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
    return 0;
}

static void SentinelDriverManagerInitialize(void) {
    for (uint32_t i = 0; i < SENTINEL_DRIVER_REGISTRY_MAX; ++i) {
        SentinelDriverRegistry[i].used = 0;
        SentinelDriverRegistry[i].object.state = SENTINEL_DRIVER_UNLOADED;
        SentinelDriverRegistry[i].object.last_status = 0;
    }
    SentinelDriverRegistryCount = 0;
    SentinelDriverRegistryReady = 1;
}

/* Registers a successfully initialized module. The caller must first validate
 * the PE image and call its exported driver initializer using this ABI. */
static int SentinelDriverRegister(const SENTINEL_DRIVER_MODULE_DESCRIPTOR *module,
                                  void *image_base,
                                  SENTINEL_DRIVER_UNLOAD unload,
                                  int32_t init_status) {
    if (!SentinelDriverRegistryReady || !module ||
        module->size < sizeof(SENTINEL_DRIVER_MODULE_DESCRIPTOR) ||
        module->abi_version != SENTINEL_DRIVER_ABI_VERSION ||
        module->signature != SENTINEL_DRIVER_SIGNATURE ||
        !module->name || !module->name[0] ||
        !module->initialize || !image_base || !module->image_size)
        return -1;

    uint32_t name_len = 0;
    while (module->name[name_len] && name_len < SENTINEL_DRIVER_NAME_MAX)
        ++name_len;
    if (!name_len || name_len >= SENTINEL_DRIVER_NAME_MAX) return -2;

    for (uint32_t i = 0; i < SENTINEL_DRIVER_REGISTRY_MAX; ++i) {
        if (SentinelDriverRegistry[i].used &&
            SentinelDriverNameEqual(SentinelDriverRegistry[i].object.name,
                                    module->name))
            return -3;
    }
    if (SentinelDriverRegistryCount >= SENTINEL_DRIVER_REGISTRY_MAX) return -4;

    SENTINEL_DRIVER_REGISTRY_ENTRY *entry = 0;
    for (uint32_t i = 0; i < SENTINEL_DRIVER_REGISTRY_MAX; ++i) {
        if (!SentinelDriverRegistry[i].used) {
            entry = &SentinelDriverRegistry[i];
            break;
        }
    }
    if (!entry) return -4;

    SENTINEL_DRIVER_OBJECT *object = &entry->object;
    object->size = sizeof(*object);
    object->abi_version = SENTINEL_DRIVER_ABI_VERSION;
    object->signature = SENTINEL_DRIVER_SIGNATURE;
    object->flags = module->flags;
    for (uint32_t i = 0; i < SENTINEL_DRIVER_NAME_MAX; ++i) {
        object->name[i] = module->name[i];
        if (!module->name[i]) break;
    }
    object->module_base = image_base;
    object->module_size = module->image_size;
    object->unload = unload ? unload : module->unload;
    object->state = init_status == 0 ? SENTINEL_DRIVER_RUNNING : SENTINEL_DRIVER_FAILED;
    object->last_status = init_status;
    entry->used = 1;
    ++SentinelDriverRegistryCount;
    return init_status == 0 ? 0 : -5;
}

static SENTINEL_DRIVER_OBJECT *SentinelDriverFind(const char *name) {
    if (!SentinelDriverRegistryReady || !name) return 0;
    for (uint32_t i = 0; i < SENTINEL_DRIVER_REGISTRY_MAX; ++i)
        if (SentinelDriverRegistry[i].used &&
            SentinelDriverNameEqual(SentinelDriverRegistry[i].object.name, name))
            return &SentinelDriverRegistry[i].object;
    return 0;
}
