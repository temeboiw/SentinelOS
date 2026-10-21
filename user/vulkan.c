/*
 * SentinelOS Vulkan loader core.
 * This is a native PE loader surface, not a Vulkan ICD. It deliberately
 * advertises no physical devices until a real backend is registered.
 */
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
# define VKAPI_CALL __stdcall
#else
# define VKAPI_CALL
#endif
typedef int32_t VkResult;
typedef uint32_t VkBool32, VkFlags, VkStructureType, VkInstanceCreateFlags;
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef void (VKAPI_CALL *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetInstanceProcAddr)(VkInstance, const char *);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetDeviceProcAddr)(VkDevice, const char *);
#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_HOST_MEMORY (-1)
#define VK_ERROR_INITIALIZATION_FAILED (-3)
#define VK_ERROR_LAYER_NOT_PRESENT (-6)
#define VK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_API_VERSION_1_0 ((1u << 22) | (0u << 12) | 0u)
#define VK_MAX_EXTENSION_NAME_SIZE 256
#define VK_MAX_DESCRIPTION_SIZE 256
#define VK_INSTANCE_MAGIC 0x534E544CUL
typedef struct VkApplicationInfo {
    VkStructureType sType; const void *pNext; const char *pApplicationName;
    uint32_t applicationVersion; const char *pEngineName; uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;
typedef struct VkInstanceCreateInfo {
    VkStructureType sType; const void *pNext; VkInstanceCreateFlags flags;
    const VkApplicationInfo *pApplicationInfo; uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;
typedef struct VkExtensionProperties {
    char extensionName[VK_MAX_EXTENSION_NAME_SIZE]; uint32_t specVersion;
} VkExtensionProperties;
typedef struct VkLayerProperties {
    char layerName[VK_MAX_EXTENSION_NAME_SIZE]; uint32_t specVersion;
    uint32_t implementationVersion; char description[VK_MAX_DESCRIPTION_SIZE];
} VkLayerProperties;
typedef struct SentinelVkInstance { uint32_t magic, apiVersion; } SentinelVkInstance;
static SentinelVkInstance g_instance;
static uint8_t g_instance_live;
static int vk_instance_valid(VkInstance instance) {
    return instance == (VkInstance)&g_instance && g_instance_live &&
           g_instance.magic == VK_INSTANCE_MAGIC;
}
static int vk_str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}
__declspec(dllexport) VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
    if (!pApiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = VK_API_VERSION_1_0; return VK_SUCCESS;
}
__declspec(dllexport) VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (pLayerName) return VK_ERROR_LAYER_NOT_PRESENT;
    (void)pProperties; *pPropertyCount = 0; return VK_SUCCESS;
}
__declspec(dllexport) VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount, VkLayerProperties *pProperties) {
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    (void)pProperties; *pPropertyCount = 0; return VK_SUCCESS;
}
__declspec(dllexport) VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo, const void *pAllocator, VkInstance *pInstance) {
    if (!pCreateInfo || !pInstance ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pInstance = (VkInstance)0;
    if (pAllocator) return VK_ERROR_INITIALIZATION_FAILED;
    if (pCreateInfo->enabledLayerCount) return VK_ERROR_LAYER_NOT_PRESENT;
    if (pCreateInfo->enabledExtensionCount) return VK_ERROR_EXTENSION_NOT_PRESENT;
    if (pCreateInfo->pApplicationInfo &&
        pCreateInfo->pApplicationInfo->sType != VK_STRUCTURE_TYPE_APPLICATION_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (g_instance_live) return VK_ERROR_OUT_OF_HOST_MEMORY;
    g_instance.magic = VK_INSTANCE_MAGIC;
    g_instance.apiVersion = VK_API_VERSION_1_0;
    g_instance_live = 1; *pInstance = (VkInstance)&g_instance; return VK_SUCCESS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyInstance(VkInstance instance, const void *pAllocator) {
    if (pAllocator || !vk_instance_valid(instance)) return;
    g_instance.magic = 0; g_instance.apiVersion = 0; g_instance_live = 0;
}
__declspec(dllexport) VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices) {
    if (!vk_instance_valid(instance) || !pPhysicalDeviceCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    (void)pPhysicalDevices;
    /* No initialized Vulkan ICD exists yet; never fabricate a device. */
    *pPhysicalDeviceCount = 0; return VK_SUCCESS;
}
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char *pName) {
    (void)device; (void)pName; return (PFN_vkVoidFunction)0;
}
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char *pName) {
    if (!pName) return (PFN_vkVoidFunction)0;
#define VK_PROC(name) if (vk_str_eq(pName, #name)) return (PFN_vkVoidFunction)&name
    VK_PROC(vkGetInstanceProcAddr);
    VK_PROC(vkGetDeviceProcAddr);
    VK_PROC(vkEnumerateInstanceVersion);
    VK_PROC(vkEnumerateInstanceExtensionProperties);
    VK_PROC(vkEnumerateInstanceLayerProperties);
    VK_PROC(vkCreateInstance);
    VK_PROC(vkDestroyInstance);
    VK_PROC(vkEnumeratePhysicalDevices);
#undef VK_PROC
    if (!vk_instance_valid(instance)) return (PFN_vkVoidFunction)0;
    return (PFN_vkVoidFunction)0;
}
__declspec(dllexport) uint32_t VKAPI_CALL SentinelVulkanLoaderStatus(void) {
    /* bit 0 = loader present; bit 1 = instance exists; no ICD bit is set. */
    return 1u | (g_instance_live ? 2u : 0u);
}
