#ifndef VULKAN_H_
#define VULKAN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Vulkan 1.0 core types and constants (Khronos-compatible)
 * ========================================================================= */

#define VK_NULL_HANDLE 0

#define VK_MAKE_VERSION(major, minor, patch) \
    (((uint32_t)(major) << 22) | ((uint32_t)(minor) << 12) | (uint32_t)(patch))

#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)

#define VK_MAX_EXTENSION_NAME_SIZE 256
#define VK_MAX_DESCRIPTION_SIZE 256
#define VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define VK_UUID_SIZE 16
#define VK_LUID_SIZE 8

/* Result codes */
#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_TIMEOUT 2
#define VK_EVENT_SET 3
#define VK_EVENT_RESET 4
#define VK_INCOMPLETE 5
#define VK_ERROR_OUT_OF_HOST_MEMORY (-1)
#define VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)
#define VK_ERROR_INITIALIZATION_FAILED (-3)
#define VK_ERROR_DEVICE_LOST (-4)
#define VK_ERROR_MEMORY_MAP_FAILED (-5)
#define VK_ERROR_LAYER_NOT_PRESENT (-6)
#define VK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define VK_ERROR_FEATURE_NOT_PRESENT (-8)
#define VK_ERROR_INCOMPATIBLE_DRIVER (-9)
#define VK_ERROR_TOO_MANY_OBJECTS (-10)
#define VK_ERROR_FORMAT_NOT_SUPPORTED (-11)
#define VK_ERROR_FRAGMENTED_POOL (-12)
#define VK_ERROR_OUT_OF_POOL_MEMORY (-1000069000)
#define VK_ERROR_INVALID_EXTERNAL_HANDLE (-1000072003)
#define VK_ERROR_SURFACE_LOST_KHR (-1000000000)
#define VK_ERROR_NATIVE_WINDOW_IN_USE_KHR (-1000000001)
#define VK_SUBOPTIMAL_KHR 1000001003
#define VK_ERROR_OUT_OF_DATE_KHR 1000001004
#define VK_ERROR_INCOMPATIBLE_DISPLAY_KHR (-1000003001)
#define VK_ERROR_VALIDATION_FAILED_EXT (-1000011001)
#define VK_ERROR_INVALID_SHADER_NV (-1000012000)

/* Basic types */
typedef int32_t VkResult;
typedef int32_t VkBool32;
typedef uint32_t VkFlags;
typedef uint32_t VkStructureType;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkSampleCountFlags;
typedef uint32_t VkImageUsageFlags;
typedef uint32_t VkBufferUsageFlags;
typedef uint32_t VkShaderStageFlags;
typedef uint32_t VkAccessFlags;
typedef uint32_t VkPipelineStageFlags;
typedef uint32_t VkImageAspectFlags;
typedef uint32_t VkMemoryPropertyFlags;
typedef uint32_t VkMemoryHeapFlags;
typedef uint32_t VkDeviceCreateFlags;
typedef uint32_t VkInstanceCreateFlags;
typedef uint32_t VkDeviceQueueCreateFlags;
typedef uint32_t VkPipelineCreateFlags;
typedef uint32_t VkPipelineLayoutCreateFlags;
typedef uint32_t VkDescriptorSetLayoutCreateFlags;
typedef uint32_t VkDescriptorPoolCreateFlags;
typedef uint32_t VkDescriptorPoolResetFlags;
typedef uint32_t VkFramebufferCreateFlags;
typedef uint32_t VkRenderPassCreateFlags;
typedef uint32_t VkCommandPoolCreateFlags;
typedef uint32_t VkCommandBufferUsageFlags;
typedef uint32_t VkCommandBufferResetFlags;
typedef uint32_t VkSemaphoreCreateFlags;
typedef uint32_t VkFenceCreateFlags;
typedef uint32_t VkImageCreateFlags;
typedef uint32_t VkImageViewCreateFlags;
typedef uint32_t VkShaderModuleCreateFlags;
typedef uint32_t VkSamplerCreateFlags;
typedef uint32_t VkSwapchainCreateFlagsKHR;
typedef uint32_t VkSurfaceTransformFlagsKHR;
typedef uint32_t VkCompositeAlphaFlagsKHR;
typedef uint32_t VkPresentModeKHR;
typedef uint32_t VkColorSpaceKHR;
typedef uint32_t VkFormat;
typedef uint32_t VkImageType;
typedef uint32_t VkImageTiling;
typedef uint32_t VkPhysicalDeviceType;
typedef uint32_t VkQueryType;
typedef uint32_t VkPrimitiveTopology;
typedef uint32_t VkPolygonMode;
typedef uint32_t VkFrontFace;
typedef uint32_t VkCompareOp;
typedef uint32_t VkStencilOp;
typedef uint32_t VkLogicOp;
typedef uint32_t VkBlendFactor;
typedef uint32_t VkBlendOp;
typedef uint32_t VkDynamicState;
typedef uint32_t VkAttachmentLoadOp;
typedef uint32_t VkAttachmentStoreOp;
typedef uint32_t VkPipelineBindPoint;
typedef uint32_t VkImageLayout;
typedef uint32_t VkBufferCreateFlags;
typedef uint32_t VkImageViewType;
typedef uint32_t VkComponentSwizzle;
typedef uint32_t VkVertexInputRate;
typedef uint32_t VkIndexType;
typedef uint32_t VkSubpassContents;

/* Opaque handle types */
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkBuffer_T *VkBuffer;
typedef struct VkImage_T *VkImage;
typedef struct VkImageView_T *VkImageView;
typedef struct VkRenderPass_T *VkRenderPass;
typedef struct VkFramebuffer_T *VkFramebuffer;
typedef struct VkCommandPool_T *VkCommandPool;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkFence_T *VkFence;
typedef struct VkSemaphore_T *VkSemaphore;
typedef struct VkDeviceMemory_T *VkDeviceMemory;
typedef struct VkSampler_T *VkSampler;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;
typedef struct VkShaderModule_T *VkShaderModule;
typedef struct VkSurfaceKHR_T *VkSurfaceKHR;
typedef struct VkSwapchainKHR_T *VkSwapchainKHR;

/* Function pointer types */
typedef void (VKAPI_CALL *PFN_vkVoidFunction)(void);

#if !defined(VK_NO_PROTOTYPES)
# if defined(_WIN32)
#  define VKAPI_CALL __stdcall
# elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#  define VKAPI_CALL __attribute__((ms_abi))
# else
#  define VKAPI_CALL
# endif
#endif

#if !defined(VKAPI_ATTR)
# define VKAPI_ATTR
#endif
#if !defined(VKAPI_CALL_PTR)
# define VKAPI_CALL_PTR
#endif

typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetInstanceProcAddr)(VkInstance, const char *);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetDeviceProcAddr)(VkDevice, const char *);

/* =========================================================================
 * Structure types
 * ========================================================================= */

#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 4
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5
#define VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE 6
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 8
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 9
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12
#define VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO 13
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
#define VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO 17
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 30
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32
#define VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 33
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 34
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET 1000014000
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO 41
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO 38
#define VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO 37
#define VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO 43
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR 1000001000
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1000001002
#define VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR 1000009000

typedef struct VkApplicationInfo {
    VkStructureType        sType;
    const void            *pNext;
    const char            *pApplicationName;
    uint32_t               applicationVersion;
    const char            *pEngineName;
    uint32_t               engineVersion;
    uint32_t               apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType             sType;
    const void                 *pNext;
    VkInstanceCreateFlags       flags;
    const VkApplicationInfo    *pApplicationInfo;
    uint32_t                    enabledLayerCount;
    const char * const         *ppEnabledLayerNames;
    uint32_t                    enabledExtensionCount;
    const char * const         *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkDeviceQueueCreateInfo {
    VkStructureType             sType;
    const void                 *pNext;
    VkDeviceQueueCreateFlags    flags;
    uint32_t                    queueFamilyIndex;
    uint32_t                    queueCount;
    const float                *pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    VkStructureType                 sType;
    const void                     *pNext;
    VkDeviceCreateFlags             flags;
    uint32_t                        queueCreateInfoCount;
    const VkDeviceQueueCreateInfo  *pQueueCreateInfos;
    uint32_t                        enabledLayerCount;
    const char * const             *ppEnabledLayerNames;
    uint32_t                        enabledExtensionCount;
    const char * const             *ppEnabledExtensionNames;
    const void                     *pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkExtensionProperties {
    char        extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t    specVersion;
} VkExtensionProperties;

typedef struct VkLayerProperties {
    char             layerName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t         specVersion;
    uint32_t         implementationVersion;
    char             description[VK_MAX_DESCRIPTION_SIZE];
} VkLayerProperties;

typedef struct VkSubmitInfo {
    VkStructureType                sType;
    const void                    *pNext;
    uint32_t                       waitSemaphoreCount;
    const VkSemaphore             *pWaitSemaphores;
    const VkPipelineStageFlags    *pWaitDstStageMask;
    uint32_t                       commandBufferCount;
    const VkCommandBuffer         *pCommandBuffers;
    uint32_t                       signalSemaphoreCount;
    const VkSemaphore             *pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkMemoryAllocateInfo {
    VkStructureType    sType;
    const void        *pNext;
    VkDeviceSize       allocationSize;
    uint32_t           memoryTypeIndex;
} VkMemoryAllocateInfo;

typedef struct VkMappedMemoryRange {
    VkStructureType    sType;
    const void        *pNext;
    VkDeviceMemory     memory;
    VkDeviceSize       offset;
    VkDeviceSize       size;
} VkMappedMemoryRange;

typedef struct VkMemoryRequirements {
    VkDeviceSize    size;
    VkDeviceSize    alignment;
    uint32_t        memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkMemoryType {
    VkMemoryPropertyFlags    propertyFlags;
    uint32_t                 heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap {
    VkDeviceSize        size;
    VkMemoryHeapFlags   flags;
} VkMemoryHeap;

typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t              memoryTypeCount;
    VkMemoryType          memoryTypes[32];
    uint32_t              memoryHeapCount;
    VkMemoryHeap          memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkFenceCreateInfo {
    VkStructureType      sType;
    const void          *pNext;
    VkFenceCreateFlags   flags;
} VkFenceCreateInfo;

typedef struct VkSemaphoreCreateInfo {
    VkStructureType         sType;
    const void             *pNext;
    VkSemaphoreCreateFlags  flags;
} VkSemaphoreCreateInfo;

typedef struct VkBufferCreateInfo {
    VkStructureType         sType;
    const void             *pNext;
    VkBufferCreateFlags     flags;
    VkDeviceSize            size;
    VkBufferUsageFlags      usage;
    VkSharingMode           sharingMode;
    uint32_t                queueFamilyIndexCount;
    const uint32_t         *pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkImageCreateInfo {
    VkStructureType         sType;
    const void             *pNext;
    VkImageCreateFlags      flags;
    VkImageType             imageType;
    VkFormat                format;
    VkExtent3D              extent;
    uint32_t                mipLevels;
    uint32_t                arrayLayers;
    VkSampleCountFlags      samples;
    VkImageTiling           tiling;
    VkImageUsageFlags       usage;
    VkSharingMode           sharingMode;
    uint32_t                queueFamilyIndexCount;
    const uint32_t         *pQueueFamilyIndices;
    VkImageLayout           initialLayout;
} VkImageCreateInfo;

typedef struct VkImageViewCreateInfo {
    VkStructureType          sType;
    const void              *pNext;
    VkImageViewCreateFlags   flags;
    VkImage                  image;
    VkImageViewType          viewType;
    VkFormat                 format;
    VkComponentMapping       components;
    VkImageSubresourceRange  subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkComponentMapping {
    VkComponentSwizzle    r;
    VkComponentSwizzle    g;
    VkComponentSwizzle    b;
    VkComponentSwizzle    a;
} VkComponentMapping;

typedef struct VkImageSubresourceRange {
    VkImageAspectFlags    aspectMask;
    uint32_t              baseMipLevel;
    uint32_t              levelCount;
    uint32_t              baseArrayLayer;
    uint32_t              layerCount;
} VkImageSubresourceRange;

typedef struct VkShaderModuleCreateInfo {
    VkStructureType          sType;
    const void              *pNext;
    VkShaderModuleCreateFlags flags;
    size_t                   codeSize;
    const uint32_t          *pCode;
} VkShaderModuleCreateInfo;

typedef struct VkPipelineCacheCreateInfo {
    VkStructureType          sType;
    const void              *pNext;
    VkPipelineCacheCreateFlags flags;
    size_t                   initialDataSize;
    const void              *pInitialData;
} VkPipelineCacheCreateInfo;

typedef struct VkPipelineLayoutCreateInfo {
    VkStructureType                   sType;
    const void                       *pNext;
    VkPipelineLayoutCreateFlags       flags;
    uint32_t                          setLayoutCount;
    const VkDescriptorSetLayout      *pSetLayouts;
    uint32_t                          pushConstantRangeCount;
    const VkPushConstantRange        *pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkPushConstantRange {
    VkShaderStageFlags    stageFlags;
    uint32_t              offset;
    uint32_t              size;
} VkPushConstantRange;

typedef struct VkDescriptorSetLayoutCreateInfo {
    VkStructureType                        sType;
    const void                            *pNext;
    VkDescriptorSetLayoutCreateFlags       flags;
    uint32_t                               bindingCount;
    const VkDescriptorSetLayoutBinding    *pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkDescriptorSetLayoutBinding {
    uint32_t              binding;
    VkDescriptorType      descriptorType;
    uint32_t              descriptorCount;
    VkShaderStageFlags    stageFlags;
    const VkSampler      *pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorPoolCreateInfo {
    VkStructureType              sType;
    const void                  *pNext;
    VkDescriptorPoolCreateFlags  flags;
    uint32_t                     maxSets;
    uint32_t                     poolSizeCount;
    const VkDescriptorPoolSize  *pPoolSizes;
} VkDescriptorPoolCreateInfo;

typedef struct VkDescriptorPoolSize {
    VkDescriptorType    type;
    uint32_t            descriptorCount;
} VkDescriptorPoolSize;

typedef struct VkDescriptorSetAllocateInfo {
    VkStructureType                sType;
    const void                    *pNext;
    VkDescriptorPool               descriptorPool;
    uint32_t                       descriptorSetCount;
    const VkDescriptorSetLayout   *pSetLayouts;
} VkDescriptorSetAllocateInfo;

typedef struct VkDescriptorSet {
    VkStructureType    sType;
    const void        *pNext;
} VkDescriptorSet;

typedef struct VkCommandPoolCreateInfo {
    VkStructureType             sType;
    const void                 *pNext;
    VkCommandPoolCreateFlags    flags;
    uint32_t                    queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    VkStructureType              sType;
    const void                  *pNext;
    VkCommandPool                commandPool;
    VkCommandBufferLevel         level;
    uint32_t                     commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    VkStructureType                     sType;
    const void                         *pNext;
    VkCommandBufferUsageFlags           flags;
    const VkCommandBufferInheritanceInfo *pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkCommandBufferInheritanceInfo {
    VkStructureType          sType;
    const void              *pNext;
    VkRenderPass             renderPass;
    uint32_t                 subpass;
    VkFramebuffer            framebuffer;
    VkBool32                 occlusionQueryEnable;
    VkQueryControlFlags      queryFlags;
    VkQueryPipelineStatisticFlags pipelineStatistics;
} VkCommandBufferInheritanceInfo;

typedef struct VkRenderPassCreateInfo {
    VkStructureType                  sType;
    const void                      *pNext;
    VkRenderPassCreateFlags          flags;
    uint32_t                         attachmentCount;
    const VkAttachmentDescription   *pAttachments;
    uint32_t                         subpassCount;
    const VkSubpassDescription      *pSubpasses;
    uint32_t                         dependencyCount;
    const VkSubpassDependency       *pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkAttachmentDescription {
    VkFlags               flags;
    VkFormat              format;
    VkSampleCountFlags    samples;
    VkAttachmentLoadOp    loadOp;
    VkAttachmentStoreOp   storeOp;
    VkAttachmentLoadOp    stencilLoadOp;
    VkAttachmentStoreOp   stencilStoreOp;
    VkImageLayout         initialLayout;
    VkImageLayout         finalLayout;
} VkAttachmentDescription;

typedef struct VkSubpassDescription {
    VkFlags                    flags;
    VkPipelineBindPoint        pipelineBindPoint;
    uint32_t                   inputAttachmentCount;
    const VkAttachmentReference *pInputAttachments;
    uint32_t                   colorAttachmentCount;
    const VkAttachmentReference *pColorAttachments;
    const VkAttachmentReference *pResolveAttachments;
    const VkAttachmentReference *pDepthStencilAttachment;
    uint32_t                   preserveAttachmentCount;
    const uint32_t            *pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkAttachmentReference {
    uint32_t        attachment;
    VkImageLayout   layout;
} VkAttachmentReference;

typedef struct VkSubpassDependency {
    uint32_t                 srcSubpass;
    uint32_t                 dstSubpass;
    VkPipelineStageFlags     srcStageMask;
    VkPipelineStageFlags     dstStageMask;
    VkAccessFlags            srcAccessMask;
    VkAccessFlags            dstAccessMask;
    VkDependencyFlags        dependencyFlags;
} VkSubpassDependency;

typedef struct VkFramebufferCreateInfo {
    VkStructureType             sType;
    const void                 *pNext;
    VkFramebufferCreateFlags    flags;
    VkRenderPass                renderPass;
    uint32_t                    attachmentCount;
    const VkImageView          *pAttachments;
    uint32_t                    width;
    uint32_t                    height;
    uint32_t                    layers;
} VkFramebufferCreateInfo;

typedef struct VkRenderPassBeginInfo {
    VkStructureType      sType;
    const void          *pNext;
    VkRenderPass         renderPass;
    VkFramebuffer        framebuffer;
    VkRect2D             renderArea;
    uint32_t             clearValueCount;
    const VkClearValue  *pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkClearValue {
    VkClearColorValue    color;
    VkClearDepthStencilValue depthStencil;
} VkClearValue;

typedef union VkClearColorValue {
    float       float32[4];
    int32_t     int32[4];
    uint32_t    uint32[4];
} VkClearColorValue;

typedef struct VkClearDepthStencilValue {
    float           depth;
    uint32_t        stencil;
} VkClearDepthStencilValue;

typedef struct VkRect2D {
    VkOffset2D    offset;
    VkExtent2D    extent;
} VkRect2D;

typedef struct VkOffset2D {
    int32_t    x;
    int32_t    y;
} VkOffset2D;

typedef struct VkExtent2D {
    uint32_t    width;
    uint32_t    height;
} VkExtent2D;

typedef struct VkExtent3D {
    uint32_t    width;
    uint32_t    height;
    uint32_t    depth;
} VkExtent3D;

typedef struct VkDescriptorPoolSize {
    VkDescriptorType    type;
    uint32_t            descriptorCount;
} VkDescriptorPoolSize;

/* =========================================================================
 * VK_KHR_swapchain extension
 * ========================================================================= */

#define VK_KHR_SWAPCHAIN_SPEC_VERSION 4
#define VK_KHR_SWAPCHAIN_EXTENSION_NAME "VK_KHR_swapchain"

#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR 1000001000u
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1000001002u
#define VK_SHARING_MODE_EXCLUSIVE 0u
#define VK_SHARING_MODE_CONCURRENT 1u
#define VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 1u
#define VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR 2u
#define VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR 4u
#define VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR 8u
#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 1u
#define VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR 2u
#define VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR 4u
#define VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR 8u
#define VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR 16u
#define VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR 32u
#define VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR 64u
#define VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR 128u
#define VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR 256u
#define VK_PRESENT_MODE_IMMEDIATE_KHR 0u
#define VK_PRESENT_MODE_MAILBOX_KHR 1u
#define VK_PRESENT_MODE_FIFO_KHR 2u
#define VK_PRESENT_MODE_FIFO_RELAXED_KHR 3u
#define VK_COLOR_SPACE_SRGB_NONLINEAR_KHR 0u
#define VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT 0
#define VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT 1
#define VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT 2
#define VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT 3

typedef struct VkSwapchainCreateInfoKHR {
    VkStructureType                 sType;
    const void                     *pNext;
    VkSwapchainCreateFlagsKHR       flags;
    VkSurfaceKHR                    surface;
    uint32_t                        minImageCount;
    VkFormat                        imageFormat;
    VkColorSpaceKHR                 imageColorSpace;
    VkExtent2D                      imageExtent;
    uint32_t                        imageArrayLayers;
    VkImageUsageFlags               imageUsage;
    VkSharingMode                   imageSharingMode;
    uint32_t                        queueFamilyIndexCount;
    const uint32_t                 *pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR   preTransform;
    VkCompositeAlphaFlagBitsKHR     compositeAlpha;
    VkPresentModeKHR                presentMode;
    VkBool32                        clipped;
    VkSwapchainKHR                  oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkPresentInfoKHR {
    VkStructureType          sType;
    const void              *pNext;
    uint32_t                 waitSemaphoreCount;
    const VkSemaphore       *pWaitSemaphores;
    uint32_t                 swapchainCount;
    const VkSwapchainKHR    *pSwapchains;
    const uint32_t          *pImageIndices;
    VkResult                *pResults;
} VkPresentInfoKHR;

/* =========================================================================
 * VK_KHR_surface extension
 * ========================================================================= */

#define VK_KHR_SURFACE_SPEC_VERSION 25
#define VK_KHR_SURFACE_EXTENSION_NAME "VK_KHR_surface"

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t                  minImageCount;
    uint32_t                  maxImageCount;
    VkExtent2D                currentExtent;
    VkExtent2D                minImageExtent;
    VkExtent2D                maxImageExtent;
    uint32_t                  maxImageArrayLayers;
    VkImageUsageFlags         supportedUsageFlags;
    VkSurfaceTransformFlagsKHR supportedTransforms;
    VkSurfaceTransformFlagBitsKHR currentTransform;
    VkCompositeAlphaFlagsKHR  supportedCompositeAlpha;
    VkImageUsageFlags         supportedUsageFlags2; /* placeholder */
    VkSurfaceTransformFlagsKHR supportedTransforms2; /* placeholder */
    VkSurfaceTransformFlagBitsKHR currentTransform2; /* placeholder */
    VkCompositeAlphaFlagsKHR  supportedCompositeAlpha2; /* placeholder */
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR {
    VkFormat         format;
    VkColorSpaceKHR  colorSpace;
} VkSurfaceFormatKHR;

typedef struct VkPresentModeKHR {
    VkPresentModeKHR  presentMode;
} VkPresentModeKHR;

typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *);
typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *);
typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR *);
typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkPresentModeKHR *);

/* =========================================================================
 * VK_KHR_win32_surface extension
 * ========================================================================= */

#define VK_KHR_WIN32_SURFACE_SPEC_VERSION 6
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"

typedef struct VkWin32SurfaceCreateInfoKHR {
    VkStructureType             sType;
    const void                 *pNext;
    VkWin32SurfaceCreateFlagsKHR flags;
    HINSTANCE                   hinstance;
    HWND                        hwnd;
} VkWin32SurfaceCreateInfoKHR;

typedef VkResult (VKAPI_CALL *PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR *, const VkAllocationCallbacks *, VkSurfaceKHR *);
typedef VkBool32 (VKAPI_CALL *PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)(VkPhysicalDevice, uint32_t);

#define VK_WIN32_SURFACE_CREATE_FLAGS_KHR 0

/* =========================================================================
 * Vulkan 1.0 core function declarations
 * ========================================================================= */

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(
    uint32_t *pApiVersion);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char *pName);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance *pInstance);

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties *pProperties);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures *pFeatures);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties *pFormatProperties);

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pMemoryProperties);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char *pName);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDevice *pDevice);

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue *pQueue);

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo *pSubmits,
    VkFence fence);

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(
    VkQueue queue);

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(
    VkDevice device);

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo *pAllocateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDeviceMemory *pMemory);

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkMemoryMapFlags flags,
    void **ppData);

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice device,
    VkDeviceMemory memory);

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange *pMemoryRanges);

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange *pMemoryRanges);

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize *pCommittedMemoryInBytes);

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice device,
    VkBuffer buffer,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset);

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice device,
    VkImage image,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset);

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice device,
    VkBuffer buffer,
    VkMemoryRequirements *pMemoryRequirements);

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device,
    VkImage image,
    VkMemoryRequirements *pMemoryRequirements);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device,
    const VkBufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkBuffer *pBuffer);

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device,
    VkBuffer buffer,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice device,
    const VkImageCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkImage *pImage);

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice device,
    VkImage image,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice device,
    const VkImageViewCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkImageView *pView);

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice device,
    VkImageView imageView,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device,
    const VkShaderModuleCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkShaderModule *pShaderModule);

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice device,
    VkShaderModule shaderModule,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice device,
    const VkPipelineCacheCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkPipelineCache *pPipelineCache);

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(
    VkDevice device,
    VkPipelineCache pipelineCache,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device,
    const VkPipelineLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkPipelineLayout *pPipelineLayout);

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice device,
    VkPipelineLayout pipelineLayout,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice device,
    const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorSetLayout *pDescriptorSetLayout);

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device,
    VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device,
    const VkDescriptorPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorPool *pDescriptorPool);

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    VkDescriptorPoolResetFlags flags);

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device,
    const VkDescriptorSetAllocateInfo *pAllocateInfo,
    VkDescriptorSet *pDescriptorSets);

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets);

VKAPI_ATTR VkResult VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device,
    uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet *pDescriptorWrites,
    uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet *pDescriptorCopies);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice device,
    const VkFramebufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkFramebuffer *pFramebuffer);

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(
    VkDevice device,
    VkFramebuffer framebuffer,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice device,
    const VkRenderPassCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkRenderPass *pRenderPass);

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(
    VkDevice device,
    VkRenderPass renderPass,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device,
    const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkCommandPool *pCommandPool);

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers);

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers);

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo *pBeginInfo);

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer commandBuffer);

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkCommandBufferResetFlags flags);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(
    VkDevice device,
    const VkSamplerCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSampler *pSampler);

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice device,
    VkSampler sampler,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device,
    const VkFenceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkFence *pFence);

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(
    VkDevice device,
    VkFence fence);

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence *pFences,
    VkBool32 waitAll,
    uint64_t timeout);

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence *pFences);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device,
    const VkSemaphoreCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSemaphore *pSemaphore);

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice device,
    VkSemaphore semaphore,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain);

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint32_t *pSwapchainImageCount,
    VkImage *pSwapchainImages);

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t *pImageIndex);

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR *pPresentInfo);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(
    VkInstance instance,
    const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSurfaceKHR *pSurface);

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex);

/* =========================================================================
 * Additional type declarations used above (minimal definitions)
 * ========================================================================= */

typedef struct VkPhysicalDeviceProperties {
    uint32_t                           apiVersion;
    uint32_t                           driverVersion;
    uint32_t                           vendorID;
    uint32_t                           deviceID;
    VkPhysicalDeviceType               deviceType;
    char                               deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t                            pipelineCacheUUID[VK_UUID_SIZE];
} VkPhysicalDeviceProperties;

typedef struct VkPhysicalDeviceFeatures {
    VkBool32   robustBufferAccess;
    VkBool32   fullDrawIndexUint32;
    VkBool32   imageCubeArray;
    VkBool32   independentBlend;
    VkBool32   geometryShader;
    VkBool32   tessellationShader;
    VkBool32   sampleRateShading;
    VkBool32   dualSrcBlend;
    VkBool32   logicOp;
    VkBool32   multiDrawIndirect;
    VkBool32   drawIndirectFirstInstance;
    VkBool32   depthClamp;
    VkBool32   depthBiasClamp;
    VkBool32   fillModeNonSolid;
    VkBool32   depthBounds;
    VkBool32   wideLines;
    VkBool32   largePoints;
    VkBool32   alphaToOne;
    VkBool32   multiViewport;
    VkBool32   samplerAnisotropy;
    VkBool32   textureCompressionETC2;
    VkBool32   textureCompressionASTC_LDR;
    VkBool32   textureCompressionBC;
    VkBool32   occlusionQueryPrecise;
    VkBool32   pipelineStatisticsQuery;
    VkBool32   vertexPipelineStoresAndAtomics;
    VkBool32   fragmentStoresAndAtomics;
    VkBool32   shaderTessellationAndGeometryPointSize;
    VkBool32   shaderImageGatherExtended;
    VkBool32   shaderStorageImageExtendedFormats;
    VkBool32   shaderStorageImageReadWithoutFormat;
    VkBool32   shaderStorageImageWriteWithoutFormat;
    VkBool32   shaderUniformBufferArrayDynamicIndexing;
    VkBool32   shaderSampledImageArrayDynamicIndexing;
    VkBool32   shaderStorageBufferArrayDynamicIndexing;
    VkBool32   shaderStorageImageArrayDynamicIndexing;
    VkBool32   shaderClipDistance;
    VkBool32   shaderCullDistance;
    VkBool32   shaderFloat64;
    VkBool32   shaderInt64;
    VkBool32   shaderInt16;
    VkBool32   shaderResourceResidency;
    VkBool32   shaderResourceMinLod;
    VkBool32   sparseBinding;
    VkBool32   sparseResidencyBuffer;
    VkBool32   sparseResidencyImage2D;
    VkBool32   sparseResidencyImage3D;
    VkBool32   sparseResidency2Samples;
    VkBool32   sparseResidency4Samples;
    VkBool32   sparseResidency8Samples;
    VkBool32   sparseResidency16Samples;
    VkBool32   sparseResidencyAliased;
    VkBool32   variableMultisampleRate;
    VkBool32   inheritedQueries;
} VkPhysicalDeviceFeatures;

typedef struct VkFormatProperties {
    VkFormatFeatureFlags    linearTilingFeatures;
    VkFormatFeatureFlags    optimalTilingFeatures;
    VkFormatFeatureFlags    bufferFeatures;
} VkFormatProperties;

typedef struct VkImageFormatProperties {
    VkExtent3D              maxExtent;
    uint32_t                maxMipLevels;
    uint32_t                maxArrayLayers;
    VkSampleCountFlags      sampleCounts;
    VkDeviceSize            maxResourceSize;
} VkImageFormatProperties;

typedef struct VkQueueFamilyProperties {
    VkQueueFlags              queueFlags;
    uint32_t                  queueCount;
    uint32_t                  timestampValidBits;
    VkExtent3D                minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkCommandBufferInheritanceInfo {
    VkStructureType                     sType;
    const void                         *pNext;
    VkRenderPass                        renderPass;
    uint32_t                            subpass;
    VkFramebuffer                       framebuffer;
    VkBool32                            occlusionQueryEnable;
    VkQueryControlFlags                 queryFlags;
    VkQueryPipelineStatisticFlags       pipelineStatistics;
} VkCommandBufferInheritanceInfo;

typedef struct VkWriteDescriptorSet {
    VkStructureType             sType;
    const void                 *pNext;
    VkDescriptorSet             dstSet;
    uint32_t                    dstBinding;
    uint32_t                    dstArrayElement;
    uint32_t                    descriptorCount;
    VkDescriptorType            descriptorType;
    const VkDescriptorImageInfo *pImageInfo;
    const VkDescriptorBufferInfo *pBufferInfo;
    const VkBufferView         *pTexelBufferView;
} VkWriteDescriptorSet;

typedef struct VkCopyDescriptorSet {
    VkStructureType              sType;
    const void                  *pNext;
    VkDescriptorSet              srcSet;
    uint32_t                     srcBinding;
    uint32_t                     srcArrayElement;
    VkDescriptorSet              dstSet;
    uint32_t                     dstBinding;
    uint32_t                     dstArrayElement;
    uint32_t                     descriptorCount;
} VkCopyDescriptorSet;

typedef struct VkDescriptorImageInfo {
    VkSampler      sampler;
    VkImageView    imageView;
    VkImageLayout  imageLayout;
} VkDescriptorImageInfo;

typedef struct VkDescriptorBufferInfo {
    VkBuffer        buffer;
    VkDeviceSize    offset;
    VkDeviceSize    range;
} VkDescriptorBufferInfo;

typedef struct VkPipelineCacheCreateInfo {
    VkStructureType                sType;
    const void                    *pNext;
    VkPipelineCacheCreateFlags     flags;
    size_t                         initialDataSize;
    const void                    *pInitialData;
} VkPipelineCacheCreateInfo;

typedef struct VkPipelineVertexInputStateCreateInfo {
    VkStructureType                             sType;
    const void                                 *pNext;
    VkPipelineVertexInputStateCreateFlags       flags;
    uint32_t                                    vertexBindingDescriptionCount;
    const VkVertexInputBindingDescription      *pVertexBindingDescriptions;
    uint32_t                                    vertexAttributeDescriptionCount;
    const VkVertexInputAttributeDescription    *pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo {
    VkStructureType                            sType;
    const void                                *pNext;
    VkPipelineInputAssemblyStateCreateFlags    flags;
    VkPrimitiveTopology                        topology;
    VkBool32                                   primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkPipelineTessellationStateCreateInfo {
    VkStructureType                         sType;
    const void                             *pNext;
    VkPipelineTessellationStateCreateFlags  flags;
    uint32_t                                patchControlPoints;
} VkPipelineTessellationStateCreateInfo;

typedef struct VkPipelineViewportStateCreateInfo {
    VkStructureType                      sType;
    const void                          *pNext;
    VkPipelineViewportStateCreateFlags   flags;
    uint32_t                             viewportCount;
    const VkViewport                    *pViewports;
    uint32_t                             scissorCount;
    const VkRect2D                      *pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineRasterizationStateCreateInfo {
    VkStructureType                           sType;
    const void                               *pNext;
    VkPipelineRasterizationStateCreateFlags   flags;
    VkBool32                                  depthClampEnable;
    VkBool32                                  rasterizerDiscardEnable;
    VkPolygonMode                             polygonMode;
    VkCullModeFlags                           cullMode;
    VkFrontFace                               frontFace;
    VkBool32                                  depthBiasEnable;
    float                                     depthBiasConstantFactor;
    float                                     depthBiasClamp;
    float                                     depthBiasSlopeFactor;
    float                                     lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineMultisampleStateCreateInfo {
    VkStructureType                              sType;
    const void                                  *pNext;
    VkPipelineMultisampleStateCreateFlags        flags;
    VkSampleCountFlags                           rasterizationSamples;
    VkBool32                                     sampleShadingEnable;
    float                                        minSampleShading;
    const VkSampleMask                          *pSampleMask;
    VkBool32                                     alphaToCoverageEnable;
    VkBool32                                     alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkPipelineDepthStencilStateCreateInfo {
    VkStructureType                            sType;
    const void                                *pNext;
    VkPipelineDepthStencilStateCreateFlags     flags;
    VkBool32                                   depthTestEnable;
    VkBool32                                   depthWriteEnable;
    VkCompareOp                                depthCompareOp;
    VkBool32                                   depthBoundsTestEnable;
    VkBool32                                   stencilTestEnable;
    VkStencilOpState                           front;
    VkStencilOpState                           back;
    float                                      minDepthBounds;
    float                                      maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;

typedef struct VkStencilOpState {
    VkStencilOp    failOp;
    VkStencilOp    passOp;
    VkStencilOp    depthFailOp;
    VkCompareOp    compareOp;
    uint32_t       compareMask;
    uint32_t       writeMask;
    uint32_t       reference;
} VkStencilOpState;

typedef struct VkPipelineColorBlendStateCreateInfo {
    VkStructureType                             sType;
    const void                                 *pNext;
    VkPipelineColorBlendStateCreateFlags        flags;
    VkBool32                                    logicOpEnable;
    VkLogicOp                                   logicOp;
    uint32_t                                    attachmentCount;
    const VkPipelineColorBlendAttachmentState  *pAttachments;
    float                                       blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState {
    VkBool32               blendEnable;
    VkBlendFactor          srcColorBlendFactor;
    VkBlendFactor          dstColorBlendFactor;
    VkBlendOp              colorBlendOp;
    VkBlendFactor          srcAlphaBlendFactor;
    VkBlendFactor          dstAlphaBlendFactor;
    VkBlendOp              alphaBlendOp;
    VkColorComponentFlags  colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineDynamicStateCreateInfo {
    VkStructureType                       sType;
    const void                           *pNext;
    VkPipelineDynamicStateCreateFlags     flags;
    uint32_t                              dynamicStateCount;
    const VkDynamicState                 *pDynamicStates;
} VkPipelineDynamicStateCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
    VkStructureType                                 sType;
    const void                                     *pNext;
    VkPipelineCreateFlags                           flags;
    uint32_t                                        stageCount;
    const VkPipelineShaderStageCreateInfo          *pStages;
    const VkPipelineVertexInputStateCreateInfo     *pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo   *pInputAssemblyState;
    const VkPipelineTessellationStateCreateInfo    *pTessellationState;
    const VkPipelineViewportStateCreateInfo        *pViewportState;
    const VkPipelineRasterizationStateCreateInfo   *pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo     *pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo    *pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo      *pColorBlendState;
    const VkPipelineDynamicStateCreateInfo         *pDynamicState;
    VkPipelineLayout                                layout;
    VkRenderPass                                    renderPass;
    uint32_t                                        subpass;
    VkPipeline                                      basePipelineHandle;
    int32_t                                         basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkPipelineShaderStageCreateInfo {
    VkStructureType                     sType;
    const void                         *pNext;
    VkPipelineShaderStageCreateFlags    flags;
    VkShaderStageFlagBits               stage;
    VkShaderModule                      module;
    const char                         *pName;
    const VkSpecializationInfo         *pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkSpecializationInfo {
    uint32_t                       mapEntryCount;
    const VkSpecializationMapEntry *pMapEntries;
    uint32_t                       dataSize;
    const void                    *pData;
} VkSpecializationInfo;

typedef struct VkSpecializationMapEntry {
    uint32_t    constantID;
    uint32_t    offset;
    size_t      size;
} VkSpecializationMapEntry;

typedef struct VkGraphicsPipelineCreateInfo {
    VkStructureType                                 sType;
    const void                                     *pNext;
    VkPipelineCreateFlags                           flags;
    uint32_t                                        stageCount;
    const VkPipelineShaderStageCreateInfo          *pStages;
    const VkPipelineVertexInputStateCreateInfo     *pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo   *pInputAssemblyState;
    const VkPipelineTessellationStateCreateInfo    *pTessellationState;
    const VkPipelineViewportStateCreateInfo        *pViewportState;
    const VkPipelineRasterizationStateCreateInfo   *pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo     *pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo    *pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo      *pColorBlendState;
    const VkPipelineDynamicStateCreateInfo         *pDynamicState;
    VkPipelineLayout                                layout;
    VkRenderPass                                    renderPass;
    uint32_t                                        subpass;
    VkPipeline                                      basePipelineHandle;
    int32_t                                         basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

/* Additional types needed by the above */
typedef struct VkViewport {
    float    x;
    float    y;
    float    width;
    float    height;
    float    minDepth;
    float    maxDepth;
} VkViewport;

typedef struct VkVertexInputBindingDescription {
    uint32_t             binding;
    uint32_t             stride;
    VkVertexInputRate    inputRate;
} VkVertexInputBindingDescription;

typedef struct VkVertexInputAttributeDescription {
    uint32_t    location;
    uint32_t    binding;
    VkFormat    format;
    uint32_t    offset;
} VkVertexInputAttributeDescription;

typedef struct VkPipelineCache {
    VkStructureType    sType;
    const void        *pNext;
} VkPipelineCache;

typedef struct VkAllocationCallbacks {
    void *pUserData;
    PFN_vkAllocationFunction  pfnAllocation;
    PFN_vkReallocationFunction pfnReallocation;
    PFN_vkFreeFunction         pfnFree;
    PFN_vkInternalAllocationNotification pfnInternalAllocation;
    PFN_vkInternalFreeNotification pfnInternalFree;
} VkAllocationCallbacks;

/* =========================================================================
 * VK_KHR_swapchain function declarations
 * ========================================================================= */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain);

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint32_t *pSwapchainImageCount,
    VkImage *pSwapchainImages);

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t *pImageIndex);

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR *pPresentInfo);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_H_ */