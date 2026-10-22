#pragma once
/*
 * SentinelOS Vulkan bring-up ABI.
 *
 * This is an explicitly incomplete Vulkan 1.x ABI subset for native
 * SentinelOS components. It is NOT a Khronos Vulkan SDK header and must not
 * be used to advertise Vulkan support to third-party applications until a
 * conformant loader/ICD and the required entry points are implemented.
 */
#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
# define SVK_CALL __cdecl
#else
# define SVK_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t SVkResult;
typedef uint32_t SVkFlags;
typedef uint32_t SVkBool32;
typedef uint64_t SVkDeviceSize;
typedef uint64_t SVkDeviceAddress;
typedef uint32_t SVkStructureType;
typedef uint32_t SVkFormat;
typedef uint32_t SVkImageLayout;
typedef uint32_t SVkQueueFlags;
typedef uint32_t SVkMemoryPropertyFlags;
typedef uint32_t SVkBufferUsageFlags;
typedef uint32_t SVkImageUsageFlags;

typedef struct SVkInstance_T *SVkInstance;
typedef struct SVkPhysicalDevice_T *SVkPhysicalDevice;
typedef struct SVkDevice_T *SVkDevice;
typedef struct SVkQueue_T *SVkQueue;
typedef struct SVkCommandPool_T *SVkCommandPool;
typedef struct SVkCommandBuffer_T *SVkCommandBuffer;
typedef struct SVkBuffer_T *SVkBuffer;
typedef struct SVkImage_T *SVkImage;
typedef struct SVkDeviceMemory_T *SVkDeviceMemory;
typedef struct SVkFence_T *SVkFence;
typedef struct SVkSemaphore_T *SVkSemaphore;
typedef struct SVkShaderModule_T *SVkShaderModule;
typedef struct SVkPipeline_T *SVkPipeline;
typedef struct SVkPipelineLayout_T *SVkPipelineLayout;
typedef struct SVkRenderPass_T *SVkRenderPass;
typedef struct SVkFramebuffer_T *SVkFramebuffer;
typedef struct SVkSwapchainKHR_T *SVkSwapchainKHR;

#define SVK_SUCCESS 0
#define SVK_NOT_READY 1
#define SVK_TIMEOUT 2
#define SVK_INCOMPLETE 5
#define SVK_ERROR_OUT_OF_HOST_MEMORY (-1)
#define SVK_ERROR_OUT_OF_DEVICE_MEMORY (-2)
#define SVK_ERROR_INITIALIZATION_FAILED (-3)
#define SVK_ERROR_DEVICE_LOST (-4)
#define SVK_ERROR_MEMORY_MAP_FAILED (-5)
#define SVK_ERROR_LAYER_NOT_PRESENT (-6)
#define SVK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define SVK_ERROR_FEATURE_NOT_PRESENT (-8)
#define SVK_ERROR_INCOMPATIBLE_DRIVER (-9)
#define SVK_ERROR_TOO_MANY_OBJECTS (-10)
#define SVK_ERROR_FORMAT_NOT_SUPPORTED (-11)
#define SVK_ERROR_UNKNOWN (-13)

#define SVK_API_VERSION_1_0 ((1u << 22) | (0u << 12) | 0u)
#define SVK_API_VERSION_1_1 ((1u << 22) | (1u << 12) | 0u)
#define SVK_API_VERSION_1_2 ((1u << 22) | (2u << 12) | 0u)
#define SVK_API_VERSION_1_3 ((1u << 22) | (3u << 12) | 0u)
#define SVK_API_VERSION_1_4 ((1u << 22) | (4u << 12) | 0u)

#define SVK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define SVK_UUID_SIZE 16
#define SVK_MAX_EXTENSION_NAME_SIZE 256
#define SVK_MAX_DESCRIPTION_SIZE 256

typedef struct {
    uint32_t sType;
    const void *pNext;
    SVkFlags flags;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} SVkApplicationInfo;

typedef struct {
    SVkStructureType sType;
    const void *pNext;
    SVkFlags flags;
    const SVkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} SVkInstanceCreateInfo;

typedef struct {
    char extensionName[SVK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
} SVkExtensionProperties;

typedef struct {
    char layerName[SVK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char description[SVK_MAX_DESCRIPTION_SIZE];
} SVkLayerProperties;

typedef struct {
    SVkStructureType sType;
    const void *pNext;
    SVkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float *pQueuePriorities;
} SVkDeviceQueueCreateInfo;

typedef struct {
    SVkStructureType sType;
    const void *pNext;
    SVkFlags flags;
    uint32_t queueCreateInfoCount;
    const SVkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
    const void *pEnabledFeatures;
} SVkDeviceCreateInfo;

typedef struct {
    SVkStructureType sType;
    void *pNext;
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[SVK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t pipelineCacheUUID[SVK_UUID_SIZE];
    uint8_t _reserved_for_complete_vk_physical_device_limits_and_sparse_properties[1024];
} SVkPhysicalDeviceProperties;

/* Reserved for the runtime's implementation state, not a Vulkan public struct. */
typedef struct {
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t backend;
    uint32_t capabilities;
    uint32_t initialized;
    uint32_t deviceLost;
    uint64_t submissionCount;
    uint64_t completedFenceCount;
} SVkSentinelRuntimeStatus;

typedef SVkResult (SVK_CALL *PFN_SVkCreateInstance)(
    const SVkInstanceCreateInfo *, const void *, SVkInstance *);
typedef void (SVK_CALL *PFN_SVkDestroyInstance)(SVkInstance, const void *);
typedef SVkResult (SVK_CALL *PFN_SVkEnumeratePhysicalDevices)(
    SVkInstance, uint32_t *, SVkPhysicalDevice *);
typedef SVkResult (SVK_CALL *PFN_SVkCreateDevice)(
    SVkPhysicalDevice, const SVkDeviceCreateInfo *, const void *, SVkDevice *);
typedef void (SVK_CALL *PFN_SVkDestroyDevice)(SVkDevice, const void *);
typedef SVkResult (SVK_CALL *PFN_SVkDeviceWaitIdle)(SVkDevice);
typedef void (SVK_CALL *PFN_SVkGetDeviceQueue)(
    SVkDevice, uint32_t, uint32_t, SVkQueue *);
typedef void (SVK_CALL *PFN_SVkGetPhysicalDeviceProperties)(
    SVkPhysicalDevice, SVkPhysicalDeviceProperties *);

typedef void (SVK_CALL *PFN_SVkVoidFunction)(void);
typedef PFN_SVkVoidFunction (SVK_CALL *PFN_SVkGetInstanceProcAddr)(
    SVkInstance, const char *);

#ifdef __cplusplus
}
#endif
