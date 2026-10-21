/*
 * SentinelOS CPU Vulkan runtime (native PE vulkan.dll).
 *
 * This implements a real, bounded Vulkan 1.0 software-device core:
 * instance/device/queue discovery, CPU memory, buffers, command pools,
 * command buffers, buffer fill/copy, fences and queue-idle semantics.
 * This remains a deliberately bounded software subset; unsupported Vulkan
 * semantics are rejected rather than advertised as fully conformant.
 */
#include <stdint.h>
#include <stddef.h>
#include "vk_softpipe.h"

#define VKAPI_CALL __attribute__((ms_abi))
#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_TIMEOUT 2
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
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14

#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO 38
#define VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO 37
#define VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO 43
#define VK_IMAGE_VIEW_TYPE_2D 1u
#define VK_ATTACHMENT_LOAD_OP_LOAD 0u
#define VK_ATTACHMENT_LOAD_OP_CLEAR 1u
#define VK_ATTACHMENT_LOAD_OP_DONT_CARE 2u
#define VK_ATTACHMENT_STORE_OP_STORE 0u
#define VK_ATTACHMENT_STORE_OP_DONT_CARE 1u
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER 45
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define VK_API_VERSION_1_0 ((1u << 22) | (0u << 12))
#define VK_MAX_EXTENSION_NAME_SIZE 256
#define VK_MAX_DESCRIPTION_SIZE 256
#define VK_INSTANCE_MAGIC 0x534E544CUL
#define VK_OBJECT_MAGIC 0x564B534FUL
#define VK_MAX_INSTANCES 8
#define VK_MAX_DEVICES 4
#define VK_MAX_QUEUES 8
#define VK_MAX_MEMORY 8
#define VK_MEMORY_BYTES (1024u*1024u)
#define VK_MAX_BUFFERS 64
#define VK_MAX_POOLS 8
#define VK_MAX_CMDS 32
#define VK_MAX_FENCES 32
#define VK_MAX_SEMAPHORES 32
#define VK_QUEUE_GRAPHICS_BIT 1u
#define VK_QUEUE_COMPUTE_BIT 2u
#define VK_QUEUE_TRANSFER_BIT 4u
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 1u
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 2u
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 4u
#define VK_MEMORY_PROPERTY_HOST_CACHED_BIT 8u
#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT 1u
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT 2u
#define VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT 4u
#define VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT 8u
#define VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT 0x10u
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 0x20u
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT 0x40u
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 0x80u
#define VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT 0x100u
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 1u
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 2u
#define VK_IMAGE_USAGE_SAMPLED_BIT 4u
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x10u
#define VK_IMAGE_USAGE_STORAGE_BIT 8u
#define VK_IMAGE_ASPECT_COLOR_BIT 1u
#define VK_IMAGE_LAYOUT_GENERAL 1u
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 7u
#define VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL 6u
#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 1u
#define VK_PIPELINE_STAGE_TRANSFER_BIT 0x1000u
#define VK_FORMAT_R8G8B8A8_UNORM 37u
#define VK_FORMAT_B8G8R8A8_UNORM 44u
#define VK_FORMAT_FEATURE_TRANSFER_SRC_BIT 0x4000u
#define VK_FORMAT_FEATURE_TRANSFER_DST_BIT 0x8000u
#define VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT 0x80u
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY 0u
#define VK_COMMAND_BUFFER_LEVEL_SECONDARY 1u
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 1u
#define VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT 2u
#define VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT 4u
#define VK_FENCE_CREATE_SIGNALED_BIT 1u
#define VK_WHOLE_SIZE (~(uint64_t)0)

typedef int32_t VkResult;
typedef uint32_t VkBool32, VkFlags, VkStructureType, VkInstanceCreateFlags;
typedef uint32_t VkDeviceQueueCreateFlags, VkDeviceCreateFlags, VkQueueFlags;
typedef uint32_t VkMemoryPropertyFlags, VkMemoryHeapFlags, VkBufferCreateFlags;
typedef uint32_t VkBufferUsageFlags, VkSharingMode, VkCommandPoolCreateFlags;
typedef uint32_t VkCommandPoolResetFlags, VkCommandBufferLevel, VkCommandBufferUsageFlags;
typedef uint32_t VkFenceCreateFlags, VkSemaphoreCreateFlags;
typedef uint64_t VkDeviceSize;
typedef struct VkMemoryObj *VkDeviceMemory;
typedef struct VkInstanceObj *VkInstance;
typedef struct VkPhysicalObj *VkPhysicalDevice;
typedef struct VkDeviceObj *VkDevice;
typedef struct VkQueueObj *VkQueue;
typedef struct VkBufferObj *VkBuffer;
typedef struct VkImageViewObj *VkImageView;
typedef struct VkRenderPassObj *VkRenderPass;
typedef struct VkFramebufferObj *VkFramebuffer;
typedef struct VkImageObj *VkImage;
typedef struct VkPoolObj *VkCommandPool;
typedef struct VkCmdObj *VkCommandBuffer;
typedef struct VkFenceObj *VkFence;
typedef struct VkSemaphoreObj *VkSemaphore;
typedef struct VkPipelineObj *VkPipeline;
typedef void (VKAPI_CALL *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetInstanceProcAddr)(VkInstance,const char *);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetDeviceProcAddr)(VkDevice,const char *);

typedef struct { VkStructureType sType; const void *pNext; const char *pApplicationName; uint32_t applicationVersion; const char *pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo;
typedef struct { VkStructureType sType; const void *pNext; VkInstanceCreateFlags flags; const VkApplicationInfo *pApplicationInfo; uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames; } VkInstanceCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceQueueCreateFlags flags; uint32_t queueFamilyIndex; uint32_t queueCount; const float *pQueuePriorities; } VkDeviceQueueCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceCreateFlags flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo *pQueueCreateInfos; uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames; const void *pEnabledFeatures; } VkDeviceCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkBufferCreateFlags flags; VkDeviceSize size; VkBufferUsageFlags usage; VkSharingMode sharingMode; uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices; } VkBufferCreateInfo;
typedef struct { uint32_t width,height,depth; } VkExtent3D;
typedef struct { int32_t x,y,z; } VkOffset3D;
typedef struct { VkExtent3D maxExtent; uint32_t maxMipLevels,maxArrayLayers,sampleCounts; VkDeviceSize maxResourceSize; } VkImageFormatProperties;
typedef struct { VkStructureType sType; const void *pNext; uint32_t flags,imageType,format; VkExtent3D extent; uint32_t mipLevels,arrayLayers,samples,tiling,usage,sharingMode,queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices; uint32_t initialLayout; } VkImageCreateInfo;
typedef struct { uint32_t aspectMask,baseMipLevel,levelCount,baseArrayLayer,layerCount; } VkImageSubresourceRange;
typedef struct { uint32_t aspectMask,mipLevel,baseArrayLayer,layerCount; } VkImageSubresourceLayers;
typedef struct { VkImageSubresourceLayers srcSubresource; VkOffset3D srcOffset; VkImageSubresourceLayers dstSubresource; VkOffset3D dstOffset; VkExtent3D extent; } VkImageCopy;
typedef struct { VkDeviceSize bufferOffset; uint32_t bufferRowLength,bufferImageHeight; VkImageSubresourceLayers imageSubresource; VkOffset3D imageOffset; VkExtent3D imageExtent; } VkBufferImageCopy;
typedef union { float float32[4]; int32_t int32[4]; uint32_t uint32[4]; } VkClearColorValue;
typedef struct { VkStructureType sType; const void *pNext; uint32_t srcAccessMask,dstAccessMask,oldLayout,newLayout,srcQueueFamilyIndex,dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange; } VkImageMemoryBarrier;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceMemory memory; VkDeviceSize offset,size; } VkMappedMemoryRange;
typedef struct { VkDeviceSize size; VkDeviceSize alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { uint32_t propertyFlags; uint32_t heapIndex; } VkMemoryType;
typedef struct { VkDeviceSize size; VkMemoryHeapFlags flags; } VkMemoryHeap;
typedef struct { uint32_t memoryTypeCount; VkMemoryType memoryTypes[32]; uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[16]; } VkPhysicalDeviceMemoryProperties;
typedef struct { VkQueueFlags queueFlags; uint32_t queueCount; uint32_t timestampValidBits; struct { int32_t width,height,depth; } minImageTransferGranularity; } VkQueueFamilyProperties;
typedef struct { VkStructureType sType; const void *pNext; VkCommandPoolCreateFlags flags; uint32_t queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkCommandPool commandPool; VkCommandBufferLevel level; uint32_t commandBufferCount; } VkCommandBufferAllocateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkCommandBufferUsageFlags flags; const void *pInheritanceInfo; } VkCommandBufferBeginInfo;
typedef struct { VkStructureType sType; const void *pNext; VkFenceCreateFlags flags; } VkFenceCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkSemaphoreCreateFlags flags; } VkSemaphoreCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; uint32_t waitSemaphoreCount; const VkSemaphore *pWaitSemaphores; const VkFlags *pWaitDstStageMask; uint32_t commandBufferCount; const VkCommandBuffer *pCommandBuffers; uint32_t signalSemaphoreCount; const VkSemaphore *pSignalSemaphores; } VkSubmitInfo;
typedef struct VkInstanceObj { uint32_t magic, apiVersion; } VkInstanceObj;
typedef struct VkPhysicalObj { uint32_t magic; VkInstance parent; } VkPhysicalObj;
typedef struct VkDeviceObj { uint32_t magic; VkPhysicalDevice physical; uint32_t queueCount; VkQueue queues[VK_MAX_QUEUES]; } VkDeviceObj;
typedef struct VkQueueObj { uint32_t magic; VkDevice device; uint32_t family,index; } VkQueueObj;
typedef struct VkBufferObj { uint32_t magic; VkDevice device; VkDeviceSize size; uint32_t usage; VkDeviceMemory memory; VkDeviceSize offset; } VkBufferObj;
typedef struct VkImageObj { uint32_t magic; VkDevice device; VkDeviceSize size; uint32_t width,height,depth,format,usage,tiling,mipLevels,arrayLayers,samples,layout; VkDeviceMemory memory; VkDeviceSize offset; } VkImageObj;
typedef struct VkImageViewObj { uint32_t magic; VkDevice device; VkImage image; uint32_t format; } VkImageViewObj;
typedef struct VkRenderPassObj { uint32_t magic; VkDevice device; uint32_t format,loadOp,storeOp,initialLayout,finalLayout; } VkRenderPassObj;
typedef struct VkFramebufferObj { uint32_t magic; VkDevice device; VkRenderPass renderPass; VkImageView view; uint32_t width,height; } VkFramebufferObj;
typedef struct { VkStructureType sType; const void *pNext; VkFlags flags; VkImage image; uint32_t viewType,format; struct { uint32_t r,g,b,a; } components; VkImageSubresourceRange subresourceRange; } VkImageViewCreateInfo;
typedef struct { uint32_t flags,format,samples,loadOp,storeOp,stencilLoadOp,stencilStoreOp,initialLayout,finalLayout; } VkAttachmentDescription;
typedef struct { uint32_t attachment,layout; } VkAttachmentReference;
typedef struct { uint32_t flags,pipelineBindPoint,inputAttachmentCount; const VkAttachmentReference *pInputAttachments; uint32_t colorAttachmentCount; const VkAttachmentReference *pColorAttachments; const VkAttachmentReference *pResolveAttachments; const VkAttachmentReference *pDepthStencilAttachment; uint32_t preserveAttachmentCount; const uint32_t *pPreserveAttachments; } VkSubpassDescription;
typedef struct { uint32_t srcSubpass,dstSubpass,srcStageMask,dstStageMask,srcAccessMask,dstAccessMask,dependencyFlags; } VkSubpassDependency;
typedef struct { VkStructureType sType; const void *pNext; VkFlags flags; uint32_t attachmentCount; const VkAttachmentDescription *pAttachments; uint32_t subpassCount; const VkSubpassDescription *pSubpasses; uint32_t dependencyCount; const VkSubpassDependency *pDependencies; } VkRenderPassCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkFlags flags; VkRenderPass renderPass; uint32_t attachmentCount; const VkImageView *pAttachments; uint32_t width,height,layers; } VkFramebufferCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkRenderPass renderPass; VkFramebuffer framebuffer; uint32_t renderArea_x,renderArea_y,renderArea_width,renderArea_height; uint32_t clearValueCount; const VkClearColorValue *pClearValues; } VkRenderPassBeginInfo;
typedef struct VkMemoryObj { uint32_t magic; VkDevice device; VkDeviceSize size; uint8_t used,mapped; uint8_t bytes[VK_MEMORY_BYTES]; } VkMemoryObj;
typedef struct VkPoolObj { uint32_t magic; VkDevice device; uint32_t family; } VkPoolObj;
typedef struct VkCmdObj { uint32_t magic; VkDevice device; VkCommandPool pool; uint8_t level,state; uint16_t count; VkPipeline bound_pipeline; VkBuffer bound_vertex; VkDeviceSize bound_vertex_offset; VkImage bound_target; uint32_t active_render_final_layout; struct { uint8_t op; uint8_t pad[7]; VkBuffer a,b; VkImage image; VkDeviceSize offset,size,offset2; uint32_t value,layout; float color[4]; uint8_t data[256]; } ops[128]; } VkCmdObj;
typedef struct VkFenceObj { uint32_t magic; VkDevice device; uint8_t signaled; } VkFenceObj;
typedef struct VkSemaphoreObj { uint32_t magic; VkDevice device; uint8_t signaled; } VkSemaphoreObj;
#define VK_MAX_SHADER_MODULES 8
#define VK_MAX_SPIRV_WORDS 4096
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
typedef struct VkShaderModuleObj { uint32_t magic; VkDevice device; uint32_t wordCount; uint32_t words[VK_MAX_SPIRV_WORDS]; } VkShaderModuleObj;
typedef struct VkShaderModuleCreateInfo { VkStructureType sType; const void *pNext; VkFlags flags; size_t codeSize; const uint32_t *pCode; } VkShaderModuleCreateInfo;
typedef struct VkShaderModuleObj *VkShaderModule;
#define VK_MAX_PIPELINES 16
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18
#define VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 28
#define VK_PIPELINE_BIND_POINT_GRAPHICS 0u
typedef struct VkPipelineObj { uint32_t magic; VkDevice device; VkShaderModule vertex, fragment; uint32_t vertex_result, fragment_result; } VkPipelineObj;
typedef struct { VkStructureType sType; const void *pNext; VkFlags flags; uint32_t stage; VkShaderModule module; const char *pName; const void *pSpecializationInfo; } VkPipelineShaderStageCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkFlags flags; uint32_t stageCount; const VkPipelineShaderStageCreateInfo *pStages; const void *pVertexInputState,*pInputAssemblyState,*pTessellationState,*pViewportState,*pRasterizationState,*pMultisampleState,*pDepthStencilState,*pColorBlendState,*pDynamicState; void *layout; void *renderPass; uint32_t subpass; VkPipeline basePipelineHandle; int32_t basePipelineIndex; } VkGraphicsPipelineCreateInfo;
static VkImageViewObj g_image_views[32];
static VkRenderPassObj g_render_passes[16];
static VkFramebufferObj g_framebuffers[32];
static VkShaderModuleObj g_shader_modules[VK_MAX_SHADER_MODULES];
static VkPipelineObj g_pipelines[VK_MAX_PIPELINES];
static VkInstanceObj g_instances[VK_MAX_INSTANCES];
static VkPhysicalObj g_phys[VK_MAX_DEVICES];
static VkDeviceObj g_devices[VK_MAX_DEVICES];
static VkQueueObj g_queues[VK_MAX_QUEUES];
static VkBufferObj g_buffers[VK_MAX_BUFFERS];
static VkImageObj g_images[32];
static VkMemoryObj g_memories[VK_MAX_MEMORY];
static VkPoolObj g_pools[VK_MAX_POOLS];
static VkCmdObj g_cmds[VK_MAX_CMDS];
static VkFenceObj g_fences[VK_MAX_FENCES];
static VkSemaphoreObj g_semaphores[VK_MAX_SEMAPHORES];
static const uint32_t MAGIC_INSTANCE=VK_INSTANCE_MAGIC;
static void vk_zero(void *p, size_t n) { uint8_t *b=(uint8_t*)p; while(n--) *b++=0; }
static void vk_copy(void *d,const void *s,size_t n) { uint8_t *a=(uint8_t*)d; const uint8_t *b=(const uint8_t*)s; while(n--) *a++=*b++; }
static int vk_eq(const char *a,const char *b) { if(!a||!b)return 0; while(*a&&*b&&*a==*b){a++;b++;}return *a==*b; }
#define VALID(h,arr,typ,magicv) ((h)!=0 && (h)>=(typ*)&arr[0] && (h)<=(typ*)&arr[(sizeof(arr)/sizeof(arr[0]))-1] && (h)->magic==(magicv))
static int inst_ok(VkInstance h) { for(unsigned i=0;i<VK_MAX_INSTANCES;i++)if(h==(VkInstance)&g_instances[i]&&g_instances[i].magic==MAGIC_INSTANCE)return 1;return 0; }
static int phys_ok(VkPhysicalDevice h) { for(unsigned i=0;i<VK_MAX_DEVICES;i++)if(h==(VkPhysicalDevice)&g_phys[i]&&g_phys[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int dev_ok(VkDevice h) { for(unsigned i=0;i<VK_MAX_DEVICES;i++)if(h==(VkDevice)&g_devices[i]&&g_devices[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int buf_ok(VkBuffer h) { for(unsigned i=0;i<VK_MAX_BUFFERS;i++)if(h==(VkBuffer)&g_buffers[i]&&g_buffers[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int image_ok(VkImage h) { for(unsigned i=0;i<32;i++)if(h==(VkImage)&g_images[i]&&g_images[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int mem_ok(VkDeviceMemory h) { for(unsigned i=0;i<VK_MAX_MEMORY;i++)if(h==(VkDeviceMemory)&g_memories[i]&&g_memories[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int pool_ok(VkCommandPool h) { for(unsigned i=0;i<VK_MAX_POOLS;i++)if(h==(VkCommandPool)&g_pools[i]&&g_pools[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int cmd_ok(VkCommandBuffer h) { for(unsigned i=0;i<VK_MAX_CMDS;i++)if(h==(VkCommandBuffer)&g_cmds[i]&&g_cmds[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int fence_ok(VkFence h) { for(unsigned i=0;i<VK_MAX_FENCES;i++)if(h==(VkFence)&g_fences[i]&&g_fences[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int sem_ok(VkSemaphore h) { for(unsigned i=0;i<VK_MAX_SEMAPHORES;i++)if(h==(VkSemaphore)&g_semaphores[i]&&g_semaphores[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int shader_ok(VkShaderModule h) { for(unsigned i=0;i<VK_MAX_SHADER_MODULES;i++)if(h==(VkShaderModule)&g_shader_modules[i]&&g_shader_modules[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int image_view_ok(VkImageView h) { for(unsigned i=0;i<32;i++)if(h==(VkImageView)&g_image_views[i]&&g_image_views[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int render_pass_ok(VkRenderPass h) { for(unsigned i=0;i<16;i++)if(h==(VkRenderPass)&g_render_passes[i]&&g_render_passes[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int framebuffer_ok(VkFramebuffer h) { for(unsigned i=0;i<32;i++)if(h==(VkFramebuffer)&g_framebuffers[i]&&g_framebuffers[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int pipeline_ok(VkPipeline h) { for(unsigned i=0;i<VK_MAX_PIPELINES;i++)if(h==&g_pipelines[i]&&h->magic==VK_OBJECT_MAGIC)return 1;return 0; }
/* Bounded SPIR-V 1.0 structural validation. This verifies the instruction
 * stream before storing it; it does not claim to execute shader instructions. */
static int vk_spirv_validate(const uint32_t *w, size_t n) {
 if(!w || n<5 || n>VK_MAX_SPIRV_WORDS || w[0]!=0x07230203u ||
    w[1]<0x00010000u || w[1]>0x00010000u || !w[3] || w[4]!=0) return 0;
 size_t i=5; int memory_model=0, entry=0, function=0;
 while(i<n) {
  uint32_t wc=w[i]>>16, op=w[i]&0xffffu;
  if(!wc || wc>n-i) return 0;
  if(op==14u) { if(wc!=3u) return 0; memory_model=1; }
  if(op==15u) { if(wc<4u) return 0; entry=1; }
  if(op==54u) { if(wc<5u) return 0; function=1; }
  i+=wc;
 }
 return i==n && memory_model && entry && function;
}
/* A bounded SPIR-V scalar interpreter for the integer subset used by
 * simple shader helpers. Unsupported executable opcodes fail closed.
 * Values are 32-bit SPIR-V bit patterns; no host pointers are dereferenced. */
#define VK_SPIRV_MAX_IDS 4096u
static uint32_t g_spirv_ids[VK_SPIRV_MAX_IDS];
static uint8_t g_spirv_defined[VK_SPIRV_MAX_IDS];
static size_t vk_spirv_find_label(const uint32_t *w,size_t n,uint32_t entry,uint32_t label) {
 size_t i=5; uint32_t in_entry=0;
 while(i<n) {
  uint32_t wc=w[i]>>16,op=w[i]&0xffffu;
  if(!wc||wc>n-i)return n;
  if(op==54u&&wc>=5u)in_entry=(w[i+2]==entry);
  else if(op==56u)in_entry=0;
  else if(in_entry&&op==248u&&wc==2u&&w[i+1]==label)return i;
  i+=wc;
 }
 return n;
}
/* Bounded control-flow-aware SPIR-V scalar interpreter. Branches stay within
 * the selected entry function, Phi selects by predecessor, and an instruction
 * budget prevents malformed loops from hanging the OS. */
static int vk_spirv_execute_u32(const uint32_t *w,size_t n,uint32_t entry,
                                uint32_t output_id,uint32_t *out_bits) {
 uint32_t *ids=g_spirv_ids; uint8_t *defined=g_spirv_defined;
 size_t i=5; uint32_t active=0, found=0, steps=0, current_label=0, predecessor=0;
 if(!w||!out_bits||!vk_spirv_validate(w,n)||!entry||
    output_id>=VK_SPIRV_MAX_IDS)return 0;
 vk_zero(g_spirv_ids,sizeof(g_spirv_ids));vk_zero(g_spirv_defined,sizeof(g_spirv_defined));
 /* Constants are module-scope declarations and are available to the entry. */
 while(i<n) {
  uint32_t wc=w[i]>>16,op=w[i]&0xffffu;
  if(op==43u && wc==4u && w[i+2]<VK_SPIRV_MAX_IDS) {
   ids[w[i+2]]=w[i+3];defined[w[i+2]]=1;
  }
  i+=wc;
 }
 i=5;
 while(i<n) {
  uint32_t wc=w[i]>>16,op=w[i]&0xffffu;
  if(op==54u && wc>=5u) { active=(w[i+2]==entry);current_label=0;predecessor=0; }
  else if(active && op==56u) { active=0;found=1; }
  else if(active) {
   uint32_t rid=0,a=0,b=0,v=0;
   if(++steps>2048u)return 0;
   if(op==248u&&wc==2u) { current_label=w[i+1]; }
   else if(op==247u||op==246u||op==0u) { /* merge declarations and nop */ }
   else if(op==253u&&wc==1u) { active=0;found=1; }
   else if(op==254u&&wc==2u) { active=0;found=1; }
   else if(op==249u&&wc==2u) {
    size_t target=vk_spirv_find_label(w,n,entry,w[i+1]);
    if(target>=n)return 0;predecessor=current_label;i=target;continue;
   }
   else if(op==250u&&wc==4u) {
    uint32_t cond=w[i+1];if(cond>=VK_SPIRV_MAX_IDS||!defined[cond])return 0;
    size_t target=vk_spirv_find_label(w,n,entry,w[ids[cond]?i+2:i+3]);
    if(target>=n)return 0;predecessor=current_label;i=target;continue;
   }
   else if(op==251u&&wc>=3u&&((wc-3u)&1u)==0u) {
    uint32_t selector=w[i+1],label=w[i+2];if(selector>=VK_SPIRV_MAX_IDS||!defined[selector])return 0;
    for(uint32_t k=3;k+1<wc;k+=2)if(w[i+k]==ids[selector]){label=w[i+k+1];break;}
    size_t target=vk_spirv_find_label(w,n,entry,label);
    if(target>=n)return 0;predecessor=current_label;i=target;continue;
   }
   else if(op==245u&&wc>=5u&&((wc-3u)&1u)==0u) {
    rid=w[i+2];if(rid>=VK_SPIRV_MAX_IDS)return 0;
    int selected=0;
    for(uint32_t k=3;k+1<wc;k+=2)if(w[i+k+1]==predecessor) {
     uint32_t src=w[i+k];if(src>=VK_SPIRV_MAX_IDS||!defined[src])return 0;
     ids[rid]=ids[src];defined[rid]=1;selected=1;break;
    }
    if(!selected)return 0;
   }
   else if(op==43u && wc==4u) { /* local constant declarations */ 
    rid=w[i+2];v=w[i+3];if(rid>=VK_SPIRV_MAX_IDS)return 0;ids[rid]=v;defined[rid]=1;
   }
   else if((op>=156u&&op<=160u)&&wc==4u) {
    rid=w[i+2];a=w[i+3];if(rid>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||!defined[a])return 0;
    uint32_t bits=ids[a],exp=bits&0x7F800000u,frac=bits&0x007FFFFFu;
    if(op==156u)v=(exp==0x7F800000u&&frac!=0u);
    else if(op==157u)v=(exp==0x7F800000u&&frac==0u);
    else if(op==158u)v=(exp!=0x7F800000u);
    else if(op==159u)v=(exp!=0u&&exp!=0x7F800000u);
    else v=(bits>>31)!=0u;
    ids[rid]=v;defined[rid]=1;
   }
   else if((op==109u||op==110u||op==111u||op==112u||op==113u||op==114u||op==115u)&&wc==4u) {
    rid=w[i+2];a=w[i+3];if(rid>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||!defined[a])return 0;
    union{uint32_t u;float f;}x,y;x.u=ids[a];
    if(op==109u){if(!(x.f>=0.0f&&x.f<4294967296.0f))return 0;v=(uint32_t)x.f;}
    else if(op==110u){if(!(x.f>=-2147483648.0f&&x.f<2147483648.0f))return 0;v=(uint32_t)(int32_t)x.f;}
    else if(op==111u){y.f=(float)(int32_t)ids[a];v=y.u;}
    else if(op==112u){y.f=(float)ids[a];v=y.u;}
    else v=ids[a]; /* FConvert is identity for the supported 32-bit float format. */
    ids[rid]=v;defined[rid]=1;
   }
   else if((op==126u||op==127u||op==168u||op==200u||op==204u||op==205u)&&wc==4u) {
    rid=w[i+2];a=w[i+3];
    if(rid>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||!defined[a])return 0;
    if(op==126u)v=0u-ids[a];
    else if(op==127u){union{uint32_t u;float f;}x;x.u=ids[a];x.f=-x.f;v=x.u;}
    else if(op==168u)v=(ids[a]==0u);
    else if(op==200u)v=~ids[a];
    else if(op==204u) {
     uint32_t x=ids[a],y=0;
     for(unsigned bit=0;bit<32;bit++)y|=((x>>bit)&1u)<<(31u-bit);
     v=y;
    } else {
     uint32_t x=ids[a],count=0;
     while(x){count+=x&1u;x>>=1;}
     v=count;
    }
    ids[rid]=v;defined[rid]=1;
   }
   else if((op==201u||op==202u||op==203u)&&(wc==(op==201u?7u:6u))) {
    rid=w[i+2];a=w[i+3];
    if(rid>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||!defined[a])return 0;
    uint32_t offset=w[i+(op==201u?5:4)],count=w[i+(op==201u?6:5)];
    if(offset>=32u||count==0u||count>32u||offset+count>32u)return 0;
    uint32_t mask=count==32u?0xFFFFFFFFu:((1u<<count)-1u);
    if(op==201u) {
     uint32_t insert_id=w[i+4];
     if(insert_id>=VK_SPIRV_MAX_IDS||!defined[insert_id])return 0;
     v=(ids[a]&~(mask<<offset))|((ids[insert_id]&mask)<<offset);
    } else {
     v=(ids[a]>>offset)&mask;
     if(op==202u&&count<32u&&(v&(1u<<(count-1u))))v|=~mask;
    }
    ids[rid]=v;defined[rid]=1;
   }
   else if((op==128u||op==129u||op==130u||op==131u||op==132u||op==133u||op==134u||op==135u||op==136u||
            op==137u||op==138u||op==139u||op==140u||op==141u||op==164u||op==165u||
            op==166u||op==167u||op==170u||op==171u||op==172u||
            op==173u||op==174u||op==175u||op==176u||op==177u||
            op==178u||op==179u||op==180u||op==181u||op==182u||op==183u||
            op==184u||op==185u||op==186u||op==187u||op==188u||op==189u||op==190u||op==191u||
            op==194u||op==195u||op==196u||op==197u||op==198u||op==199u) && wc==5u) {
    rid=w[i+2];a=w[i+3];b=w[i+4];
    if(rid>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||b>=VK_SPIRV_MAX_IDS||
       !defined[a]||!defined[b])return 0;
    if(op==128u)v=ids[a]+ids[b];
    else if(op==129u||op==131u||op==133u||op==136u){
     union{uint32_t u;float f;}x,y,z;x.u=ids[a];y.u=ids[b];
     if(op==129u)z.f=x.f+y.f;else if(op==131u)z.f=x.f-y.f;else if(op==133u)z.f=x.f*y.f;else z.f=x.f/y.f;
     v=z.u;
    }
    else if(op==130u)v=ids[a]-ids[b];
    else if(op==132u)v=ids[a]*ids[b];
    else if(op==134u){if(!ids[b])return 0;v=ids[a]/ids[b];}
    else if(op==135u){if(!ids[b]||((int32_t)ids[a]==INT32_MIN&&(int32_t)ids[b]==-1))return 0;v=(uint32_t)((int32_t)ids[a]/(int32_t)ids[b]);}
    else if(op==137u){if(!ids[b])return 0;v=ids[a]%ids[b];}
    else if(op==138u){if(!ids[b]||((int32_t)ids[a]==INT32_MIN&&(int32_t)ids[b]==-1))return 0;v=(uint32_t)((int32_t)ids[a]%(int32_t)ids[b]);}
    else if(op==139u){if(!ids[b]||((int32_t)ids[a]==INT32_MIN&&(int32_t)ids[b]==-1))return 0;int32_t rem=(int32_t)ids[a]%(int32_t)ids[b];if(rem&&((rem<0)!=((int32_t)ids[b]<0)))rem+=(int32_t)ids[b];v=(uint32_t)rem;}
    else if(op==140u||op==141u){
     union{uint32_t u;float f;}x,y,z,q;x.u=ids[a];y.u=ids[b];
     if(y.f==0.0f)return 0;
     q.f=x.f/y.f;
     if(!(q.f>=-2147483648.0f&&q.f<2147483648.0f))return 0;
     int32_t whole=(int32_t)q.f;
     if(op==141u&&q.f<(float)whole)whole--;
     z.f=x.f-(float)whole*y.f;v=z.u;
    }
    else if(op==164u)v=(ids[a]==ids[b]);
    else if(op==165u)v=(ids[a]!=ids[b]);
    else if(op==166u)v=(ids[a]||ids[b]);
    else if(op==167u)v=(ids[a]&&ids[b]);
    else if(op==170u)v=(ids[a]==ids[b]);
    else if(op==171u)v=(ids[a]!=ids[b]);
    else if(op==172u)v=(ids[a]>ids[b]);
    else if(op==173u)v=((int32_t)ids[a]>(int32_t)ids[b]);
    else if(op==174u)v=(ids[a]>=ids[b]);
    else if(op==175u)v=((int32_t)ids[a]>=(int32_t)ids[b]);
    else if(op==176u)v=(ids[a]<ids[b]);
    else if(op==177u)v=((int32_t)ids[a]<(int32_t)ids[b]);
    else if(op==178u)v=(ids[a]<=ids[b]);
    else if(op==179u)v=((int32_t)ids[a]<=(int32_t)ids[b]);
    else if(op>=180u&&op<=191u){
     union{uint32_t u;float f;}x,y;x.u=ids[a];y.u=ids[b];
     int unordered=(x.f!=x.f)||(y.f!=y.f);
     switch(op){
      case 180u:v=!unordered&&(x.f==y.f);break;case 181u:v=unordered||(x.f==y.f);break;
      case 182u:v=!unordered&&(x.f!=y.f);break;case 183u:v=unordered||(x.f!=y.f);break;
      case 184u:v=!unordered&&(x.f<y.f);break;case 185u:v=unordered||(x.f<y.f);break;
      case 186u:v=!unordered&&(x.f>y.f);break;case 187u:v=unordered||(x.f>y.f);break;
      case 188u:v=!unordered&&(x.f<=y.f);break;case 189u:v=unordered||(x.f<=y.f);break;
      case 190u:v=!unordered&&(x.f>=y.f);break;default:v=unordered||(x.f>=y.f);break;
     }
    }
    else if(op==194u)v=ids[a]>>(ids[b]&31u);
    else if(op==195u)v=(uint32_t)((int32_t)ids[a]>>(ids[b]&31u));
    else if(op==196u)v=ids[a]<<(ids[b]&31u);
    else if(op==197u)v=ids[a]|ids[b];
    else if(op==198u)v=ids[a]^ids[b];
    else v=ids[a]&ids[b];
    ids[rid]=v;defined[rid]=1;
   }
   else if((op==83u||op==124u)&&wc==4u) {
    rid=w[i+2];a=w[i+3];if(rid>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||!defined[a])return 0;
    ids[rid]=ids[a];defined[rid]=1;
   }
   else if(op==169u&&wc==6u) {
    rid=w[i+2];a=w[i+3];b=w[i+4];v=w[i+5];
    if(rid>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||b>=VK_SPIRV_MAX_IDS||v>=VK_SPIRV_MAX_IDS||!defined[a]||!defined[b]||!defined[v])return 0;
    ids[rid]=ids[a]?ids[b]:ids[v];defined[rid]=1;
   }
   else return 0;
  }
  i+=wc;
 }
 if(!found||!defined[output_id])return 0;
 *out_bits=ids[output_id];return 1;
}
static VkResult unsupported_extensions(uint32_t n,const char *const *names) { (void)names; return n?VK_ERROR_EXTENSION_NOT_PRESENT:VK_SUCCESS; }

__declspec(dllexport) VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *v) { if(!v)return VK_ERROR_INITIALIZATION_FAILED;*v=VK_API_VERSION_1_0;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *layer,uint32_t *count,void *props) { if(!count)return VK_ERROR_INITIALIZATION_FAILED;if(layer)return VK_ERROR_LAYER_NOT_PRESENT;(void)props;*count=0;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *count,void *props) { if(!count)return VK_ERROR_INITIALIZATION_FAILED;(void)props;*count=0;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *ci,const void *alloc,VkInstance *out) {
 if(!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc)return VK_ERROR_INITIALIZATION_FAILED;
 if(ci->enabledLayerCount)return VK_ERROR_LAYER_NOT_PRESENT;VkResult er=unsupported_extensions(ci->enabledExtensionCount,ci->ppEnabledExtensionNames);if(er)return er;
 if(ci->pApplicationInfo&&(ci->pApplicationInfo->sType!=VK_STRUCTURE_TYPE_APPLICATION_INFO||ci->pApplicationInfo->apiVersion>VK_API_VERSION_1_0))return VK_ERROR_INCOMPATIBLE_DRIVER;
 for(unsigned i=0;i<VK_MAX_INSTANCES;i++)if(!g_instances[i].magic){g_instances[i].magic=MAGIC_INSTANCE;g_instances[i].apiVersion=VK_API_VERSION_1_0;*out=(VkInstance)&g_instances[i];for(unsigned p=0;p<VK_MAX_DEVICES;p++)if(!g_phys[p].magic){g_phys[p].magic=VK_OBJECT_MAGIC;g_phys[p].parent=*out;break;}return VK_SUCCESS;}return VK_ERROR_OUT_OF_HOST_MEMORY;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyInstance(VkInstance h,const void *alloc) { if(alloc||!inst_ok(h))return;for(unsigned i=0;i<VK_MAX_DEVICES;i++)if(g_phys[i].parent==h){int used=0;for(unsigned d=0;d<VK_MAX_DEVICES;d++)if(g_devices[d].magic&&g_devices[d].physical==(VkPhysicalDevice)&g_phys[i])used=1;if(!used)g_phys[i].magic=0;}h->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance h,uint32_t *count,VkPhysicalDevice *out) { if(!inst_ok(h)||!count)return VK_ERROR_INITIALIZATION_FAILED;uint32_t n=0;for(unsigned i=0;i<VK_MAX_DEVICES;i++)if(g_phys[i].magic==VK_OBJECT_MAGIC&&g_phys[i].parent==h)n++;if(!out){*count=n;return VK_SUCCESS;}uint32_t cap=*count,k=0;for(unsigned i=0;i<VK_MAX_DEVICES&&k<cap;i++)if(g_phys[i].magic==VK_OBJECT_MAGIC&&g_phys[i].parent==h)out[k++]=(VkPhysicalDevice)&g_phys[i];*count=k;return k<n?VK_INCOMPLETE:VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p,uint32_t *count,VkQueueFamilyProperties *out) { if(!count)return;if(!phys_ok(p)){*count=0;return;}if(!out){*count=1;return;}if(*count){vk_zero(out,sizeof(*out));out->queueFlags=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_TRANSFER_BIT;out->queueCount=1;out->timestampValidBits=64;out->minImageTransferGranularity.width=1;out->minImageTransferGranularity.height=1;out->minImageTransferGranularity.depth=1;*count=1;}else *count=0; }
__declspec(dllexport) void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice p,void *out) {
 if(!out)return;
 /* Standard Vulkan 1.0 properties prefix; limits remain conservatively zero. */
 uint8_t *b=(uint8_t*)out;
 vk_zero(b,824); /* sizeof(VkPhysicalDeviceProperties on x86-64 */
 if(!phys_ok(p))return;
 uint32_t api=VK_API_VERSION_1_0,driver=1,vendor=0x534E544Cu,device=1,type=4;
 vk_copy(b,&api,4);vk_copy(b+4,&driver,4);vk_copy(b+8,&vendor,4);vk_copy(b+12,&device,4);vk_copy(b+16,&type,4);
 const char name[]="SentinelOS CPU Vulkan";vk_copy(b+20,name,sizeof(name));
 const uint8_t uuid[16]={0x53,0x65,0x6e,0x74,0x69,0x6e,0x65,0x6c,0x4f,0x53,0x2d,0x43,0x50,0x55,0x01,0x00};vk_copy(b+276,uuid,16);
}
__declspec(dllexport) void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice p,void *out) {
 if(out)vk_zero(out,55u*sizeof(uint32_t));
 (void)p;
}
__declspec(dllexport) void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice p,uint32_t format,void *out) {
 if(!out)return;
 uint32_t *f=(uint32_t*)out;f[0]=f[1]=f[2]=0;
 if(!phys_ok(p))return;
 if(format==VK_FORMAT_R8G8B8A8_UNORM||format==VK_FORMAT_B8G8R8A8_UNORM) {
  f[0]=VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  f[1]=VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
 }
}
__declspec(dllexport) VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice p,uint32_t format,uint32_t type,uint32_t tiling,uint32_t usage,uint32_t flags,VkImageFormatProperties *out) {
 if(!phys_ok(p)||!out)return VK_ERROR_INITIALIZATION_FAILED;
 vk_zero(out,sizeof(*out));
 if((format!=VK_FORMAT_R8G8B8A8_UNORM&&format!=VK_FORMAT_B8G8R8A8_UNORM)||type!=1||tiling>1||flags||
    !usage||(usage&~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)))return VK_ERROR_FORMAT_NOT_SUPPORTED;
 out->maxExtent.width=512;out->maxExtent.height=512;out->maxExtent.depth=1;
 out->maxMipLevels=1;out->maxArrayLayers=1;out->sampleCounts=1;out->maxResourceSize=VK_MEMORY_BYTES;
 return VK_SUCCESS;
}
__declspec(dllexport) void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *out) { if(!out)return;vk_zero(out,sizeof(*out));if(!phys_ok(p))return;out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;out->memoryTypes[0].heapIndex=0;out->memoryHeapCount=1;out->memoryHeaps[0].size=(VkDeviceSize)VK_MAX_MEMORY*VK_MEMORY_BYTES; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice p,const VkDeviceCreateInfo *ci,const void *alloc,VkDevice *out) {
 if(!phys_ok(p)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc)return VK_ERROR_INITIALIZATION_FAILED;
 if(ci->enabledLayerCount)return VK_ERROR_LAYER_NOT_PRESENT;VkResult er=unsupported_extensions(ci->enabledExtensionCount,ci->ppEnabledExtensionNames);if(er)return er;
 if(ci->pEnabledFeatures){const uint32_t *f=(const uint32_t*)ci->pEnabledFeatures;for(unsigned i=0;i<55;i++)if(f[i])return VK_ERROR_FEATURE_NOT_PRESENT;}
 if(ci->queueCreateInfoCount!=1||!ci->pQueueCreateInfos)return VK_ERROR_INITIALIZATION_FAILED;
 for(uint32_t q=0;q<ci->queueCreateInfoCount;q++){const VkDeviceQueueCreateInfo *qi=&ci->pQueueCreateInfos[q];if(qi->sType!=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO||qi->queueFamilyIndex!=0||!qi->queueCount||qi->queueCount>1||!qi->pQueuePriorities)return VK_ERROR_INITIALIZATION_FAILED;}
 for(unsigned i=0;i<VK_MAX_DEVICES;i++)if(!g_devices[i].magic){VkDeviceObj *d=&g_devices[i];d->magic=VK_OBJECT_MAGIC;d->physical=p;for(uint32_t q=0;q<ci->queueCreateInfoCount;q++){for(unsigned k=0;k<VK_MAX_QUEUES;k++)if(!g_queues[k].magic){VkQueueObj *qo=&g_queues[k];qo->magic=VK_OBJECT_MAGIC;qo->device=(VkDevice)d;qo->family=0;qo->index=0;d->queues[d->queueCount++]=(VkQueue)qo;break;}}*out=(VkDevice)d;return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyDevice(VkDevice d,const void *alloc) { if(alloc||!dev_ok(d))return;for(unsigned i=0;i<VK_MAX_QUEUES;i++)if(g_queues[i].device==d)g_queues[i].magic=0;for(unsigned i=0;i<VK_MAX_BUFFERS;i++)if(g_buffers[i].device==d)g_buffers[i].magic=0;for(unsigned i=0;i<VK_MAX_MEMORY;i++)if(g_memories[i].device==d)g_memories[i].magic=0;for(unsigned i=0;i<VK_MAX_POOLS;i++)if(g_pools[i].device==d)g_pools[i].magic=0;for(unsigned i=0;i<VK_MAX_CMDS;i++)if(g_cmds[i].device==d)g_cmds[i].magic=0;for(unsigned i=0;i<VK_MAX_FENCES;i++)if(g_fences[i].device==d)g_fences[i].magic=0;for(unsigned i=0;i<VK_MAX_SEMAPHORES;i++)if(g_semaphores[i].device==d)g_semaphores[i].magic=0;for(unsigned i=0;i<VK_MAX_SHADER_MODULES;i++)if(g_shader_modules[i].device==d)g_shader_modules[i].magic=0;for(unsigned i=0;i<VK_MAX_PIPELINES;i++)if(g_pipelines[i].device==d)g_pipelines[i].magic=0;d->magic=0; }
__declspec(dllexport) void VKAPI_CALL vkGetDeviceQueue(VkDevice d,uint32_t family,uint32_t index,VkQueue *out) { if(!out)return;*out=0;if(!dev_ok(d)||family)return;for(uint32_t i=0;i<d->queueCount;i++)if(((VkQueueObj*)d->queues[i])->index==index){*out=d->queues[i];return;} }
__declspec(dllexport) VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d) { return dev_ok(d)?VK_SUCCESS:VK_ERROR_DEVICE_LOST; }
__declspec(dllexport) VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue q) { for(unsigned i=0;i<VK_MAX_QUEUES;i++)if(q==(VkQueue)&g_queues[i]&&g_queues[i].magic==VK_OBJECT_MAGIC)return VK_SUCCESS;return VK_ERROR_DEVICE_LOST; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateBuffer(VkDevice d,const VkBufferCreateInfo *ci,const void *alloc,VkBuffer *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc||!ci->size||ci->size>UINT32_MAX)return VK_ERROR_OUT_OF_HOST_MEMORY;if(ci->sharingMode==1&&ci->queueFamilyIndexCount>1)return VK_ERROR_FEATURE_NOT_PRESENT;for(unsigned i=0;i<VK_MAX_BUFFERS;i++)if(!g_buffers[i].magic){VkBufferObj *b=&g_buffers[i];vk_zero(b,sizeof(*b));b->magic=VK_OBJECT_MAGIC;b->device=d;b->size=ci->size;b->usage=ci->usage;*out=(VkBuffer)b;return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) void VKAPI_CALL vkDestroyBuffer(VkDevice d,VkBuffer b,const void *alloc) { if(alloc||!dev_ok(d)||!buf_ok(b)||b->device!=d)return;b->magic=0; }
__declspec(dllexport) void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d,VkBuffer b,VkMemoryRequirements *out) { if(!out)return;vk_zero(out,sizeof(*out));if(!dev_ok(d)||!buf_ok(b)||b->device!=d)return;out->size=(b->size+15u)&~(VkDeviceSize)15u;out->alignment=16;out->memoryTypeBits=1; }

__declspec(dllexport) VkResult VKAPI_CALL vkCreateImage(VkDevice d,const VkImageCreateInfo *ci,const void *alloc,VkImage *out) {
 if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;
 *out=0;
 if(alloc||ci->imageType!=1||!ci->extent.width||!ci->extent.height||ci->extent.depth!=1||ci->mipLevels!=1||ci->arrayLayers!=1||ci->samples!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
 if(ci->format!=VK_FORMAT_R8G8B8A8_UNORM&&ci->format!=VK_FORMAT_B8G8R8A8_UNORM)return VK_ERROR_FORMAT_NOT_SUPPORTED;
 if(ci->tiling>1||(ci->usage&~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))||!ci->usage)return VK_ERROR_FEATURE_NOT_PRESENT;
 uint64_t bytes=(uint64_t)ci->extent.width*(uint64_t)ci->extent.height*4u;
 if(bytes>VK_MEMORY_BYTES||bytes/4u/(uint64_t)ci->extent.width!=ci->extent.height)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
 for(unsigned i=0;i<32;i++)if(!g_images[i].magic){VkImageObj *im=&g_images[i];vk_zero(im,sizeof(*im));im->magic=VK_OBJECT_MAGIC;im->device=d;im->size=bytes;im->width=ci->extent.width;im->height=ci->extent.height;im->depth=1;im->format=ci->format;im->usage=ci->usage;im->tiling=ci->tiling;im->mipLevels=1;im->arrayLayers=1;im->samples=1;im->layout=ci->initialLayout;*out=(VkImage)im;return VK_SUCCESS;}
 return VK_ERROR_TOO_MANY_OBJECTS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyImage(VkDevice d,VkImage im,const void *alloc) { if(alloc||!dev_ok(d)||!image_ok(im)||im->device!=d)return;im->magic=0; }
__declspec(dllexport) void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d,VkImage im,VkMemoryRequirements *out) { if(!out)return;vk_zero(out,sizeof(*out));if(!dev_ok(d)||!image_ok(im)||im->device!=d)return;out->size=(im->size+15u)&~(VkDeviceSize)15u;out->alignment=16;out->memoryTypeBits=1; }
__declspec(dllexport) VkResult VKAPI_CALL vkBindImageMemory(VkDevice d,VkImage im,VkDeviceMemory m,VkDeviceSize off) { if(!dev_ok(d)||!image_ok(im)||!mem_ok(m)||im->device!=d||m->device!=d||im->memory||m->mapped)return VK_ERROR_INITIALIZATION_FAILED;if((off&15u)||off>m->size||im->size>m->size-off)return VK_ERROR_OUT_OF_DEVICE_MEMORY;im->memory=m;im->offset=off;return VK_SUCCESS; }
static uint8_t vk_unorm8(const float *fp) {
 uint32_t bits;vk_copy(&bits,fp,4);
 if(bits&0x80000000u)return 0;
 uint32_t e=(bits>>23)&255u,m=bits&0x7FFFFFu;
 if(e>=127u)return 255u;
 if(!e)return 0;
 m|=0x800000u;
 uint32_t shift=150u-e;
 if(shift>=63u)return 0;
 uint64_t v=(uint64_t)m*255u+(1ULL<<(shift-1u));
 return (uint8_t)(v>>shift);
}
__declspec(dllexport) VkResult VKAPI_CALL vkAllocateMemory(VkDevice d,const VkMemoryAllocateInfo *ai,const void *alloc,VkDeviceMemory *out) { if(!dev_ok(d)||!ai||!out||ai->sType!=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc||!ai->allocationSize||ai->allocationSize>VK_MEMORY_BYTES||ai->memoryTypeIndex!=0)return VK_ERROR_OUT_OF_DEVICE_MEMORY;for(unsigned i=0;i<VK_MAX_MEMORY;i++)if(!g_memories[i].magic){VkMemoryObj *m=&g_memories[i];m->magic=VK_OBJECT_MAGIC;m->device=d;m->size=ai->allocationSize;m->used=1;m->mapped=0;vk_zero(m->bytes,VK_MEMORY_BYTES);*out=(VkDeviceMemory)m;return VK_SUCCESS;}return VK_ERROR_OUT_OF_HOST_MEMORY; }
__declspec(dllexport) void VKAPI_CALL vkFreeMemory(VkDevice d,VkDeviceMemory m,const void *alloc) { if(alloc||!dev_ok(d)||!mem_ok(m)||m->device!=d||m->mapped)return;for(unsigned i=0;i<VK_MAX_BUFFERS;i++)if(g_buffers[i].magic&&g_buffers[i].memory==m)return;for(unsigned i=0;i<32;i++)if(g_images[i].magic&&g_images[i].memory==m)return;m->magic=0;m->used=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize off) { if(!dev_ok(d)||!buf_ok(b)||!mem_ok(m)||b->device!=d||m->device!=d||b->memory||m->mapped)return VK_ERROR_INITIALIZATION_FAILED;if((off&15u)||off>m->size||b->size>m->size-off)return VK_ERROR_OUT_OF_DEVICE_MEMORY;b->memory=m;b->offset=off;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkMapMemory(VkDevice d,VkDeviceMemory m,VkDeviceSize off,VkDeviceSize size,VkFlags flags,void **out) { (void)flags;if(!dev_ok(d)||!mem_ok(m)||m->device!=d||!out||off>m->size)return VK_ERROR_MEMORY_MAP_FAILED;if(size==VK_WHOLE_SIZE)size=m->size-off;if(size>m->size-off||m->mapped)return VK_ERROR_MEMORY_MAP_FAILED;m->mapped=1;*out=&m->bytes[off];return VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkUnmapMemory(VkDevice d,VkDeviceMemory m) { if(dev_ok(d)&&mem_ok(m)&&m->device==d)m->mapped=0; }
static VkResult vk_validate_mapped_ranges(VkDevice d,uint32_t n,const VkMappedMemoryRange *r) { if(!dev_ok(d)||(!r&&n))return VK_ERROR_INITIALIZATION_FAILED;for(uint32_t i=0;i<n;i++){if(r[i].sType!=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE||r[i].pNext||!mem_ok(r[i].memory)||r[i].memory->device!=d||!r[i].memory->mapped||r[i].offset>r[i].memory->size)return VK_ERROR_MEMORY_MAP_FAILED;VkDeviceSize size=r[i].size==VK_WHOLE_SIZE?r[i].memory->size-r[i].offset:r[i].size;if(size>r[i].memory->size-r[i].offset||!size)return VK_ERROR_MEMORY_MAP_FAILED;}return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice d,uint32_t n,const VkMappedMemoryRange *ranges) { return vk_validate_mapped_ranges(d,n,ranges); }
__declspec(dllexport) VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice d,uint32_t n,const VkMappedMemoryRange *ranges) { return vk_validate_mapped_ranges(d,n,ranges); }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateCommandPool(VkDevice d,const VkCommandPoolCreateInfo *ci,const void *alloc,VkCommandPool *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc||ci->queueFamilyIndex!=0)return VK_ERROR_FEATURE_NOT_PRESENT;for(unsigned i=0;i<VK_MAX_POOLS;i++)if(!g_pools[i].magic){g_pools[i].magic=VK_OBJECT_MAGIC;g_pools[i].device=d;g_pools[i].family=0;*out=(VkCommandPool)&g_pools[i];return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) VkResult VKAPI_CALL vkResetCommandPool(VkDevice d,VkCommandPool p,VkCommandPoolResetFlags flags) { (void)flags;if(!dev_ok(d)||!pool_ok(p)||p->device!=d)return VK_ERROR_INITIALIZATION_FAILED;for(unsigned i=0;i<VK_MAX_CMDS;i++)if(g_cmds[i].magic==VK_OBJECT_MAGIC&&g_cmds[i].pool==p){g_cmds[i].count=0;g_cmds[i].state=0;}return VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkDestroyCommandPool(VkDevice d,VkCommandPool p,const void *alloc) { if(alloc||!dev_ok(d)||!pool_ok(p)||p->device!=d)return;for(unsigned i=0;i<VK_MAX_CMDS;i++)if(g_cmds[i].pool==p)g_cmds[i].magic=0;p->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice d,const VkCommandBufferAllocateInfo *ai,VkCommandBuffer *out) { if(!dev_ok(d)||!ai||!out||ai->sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO||!pool_ok(ai->commandPool)||ai->commandPool->device!=d||ai->level>1)return VK_ERROR_INITIALIZATION_FAILED;uint32_t made=0;for(unsigned i=0;i<VK_MAX_CMDS&&made<ai->commandBufferCount;i++)if(!g_cmds[i].magic){VkCmdObj *c=&g_cmds[i];c->magic=VK_OBJECT_MAGIC;c->device=d;c->pool=ai->commandPool;c->level=(uint8_t)ai->level;c->state=0;c->count=0;out[made++]=(VkCommandBuffer)c;}if(made<ai->commandBufferCount){for(uint32_t i=0;i<made;i++)((VkCmdObj*)out[i])->magic=0;return VK_ERROR_OUT_OF_HOST_MEMORY;}return VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkFreeCommandBuffers(VkDevice d,VkCommandPool p,uint32_t n,const VkCommandBuffer *list) { if(!dev_ok(d)||!pool_ok(p)||p->device!=d||!list)return;for(uint32_t j=0;j<n;j++)if(cmd_ok(list[j])&&list[j]->device==d&&list[j]->pool==p)list[j]->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer c,const VkCommandBufferBeginInfo *bi) { if(!cmd_ok(c)||!bi||bi->sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO)return VK_ERROR_INITIALIZATION_FAILED;c->count=0;c->state=1;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c) { if(!cmd_ok(c)||c->state!=1)return VK_ERROR_INITIALIZATION_FAILED;c->state=2;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer c,VkFlags flags) { (void)flags;if(!cmd_ok(c))return VK_ERROR_INITIALIZATION_FAILED;c->count=0;c->state=0;return VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer c,VkBuffer b,VkDeviceSize off,VkDeviceSize size,uint32_t value) { if(!cmd_ok(c)||c->state!=1||!buf_ok(b)||!b->memory||!(b->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||c->count>=128||off>b->size||(off&3u))return;if(size==VK_WHOLE_SIZE)size=b->size-off;if(size>b->size-off||(size!=VK_WHOLE_SIZE&&(size&3u)))return;unsigned i=c->count++;c->ops[i].op=1;c->ops[i].a=b;c->ops[i].offset=off;c->ops[i].size=size;c->ops[i].value=value; }
__declspec(dllexport) void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer c,VkBuffer b,VkDeviceSize off,VkDeviceSize size,const void *data) { if(!cmd_ok(c)||c->state!=1||!buf_ok(b)||!b->memory||b->device!=c->device||!(b->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||!data||!size||size>256u||(size&3u)||(off&3u)||off>b->size||size>b->size-off||c->count>=128)return;unsigned i=c->count++;c->ops[i].op=7;c->ops[i].a=b;c->ops[i].offset=off;c->ops[i].size=size;vk_copy(c->ops[i].data,data,(size_t)size); }
__declspec(dllexport) void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer c,VkBuffer src,VkBuffer dst,uint32_t n,const void *regions) {
 if(!cmd_ok(c)||c->state!=1||!buf_ok(src)||!buf_ok(dst)||src->device!=c->device||dst->device!=c->device||src==dst||!mem_ok(src->memory)||!mem_ok(dst->memory)||src->memory->device!=c->device||dst->memory->device!=c->device||!(src->usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)||!(dst->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||!n||!regions||n>128u-c->count)return;
 const uint64_t *r=(const uint64_t*)regions;
 for(uint32_t j=0;j<n;j++){
  uint64_t so=r[j*3],doff=r[j*3+1],sz=r[j*3+2];
  if(!sz||(so&3u)||(doff&3u)||(sz&3u)||so>src->size||sz>src->size-so||doff>dst->size||sz>dst->size-doff)return;
 }
 /* Different buffer handles can still alias the same allocation. Validate
  * every source region against every destination region before recording any
  * commands, not merely each region against its own destination. */
 if(src->memory==dst->memory){
  for(uint32_t s=0;s<n;s++)for(uint32_t d=0;d<n;d++){
   uint64_t sb=src->offset+r[s*3],se=sb+r[s*3+2];
   uint64_t db=dst->offset+r[d*3+1],de=db+r[d*3+2];
   if(sb<de&&db<se)return;
  }
 }
 for(uint32_t j=0;j<n;j++){unsigned i=c->count++;c->ops[i].op=2;c->ops[i].a=src;c->ops[i].b=dst;c->ops[i].offset=r[j*3];c->ops[i].size=r[j*3+2];c->ops[i].offset2=r[j*3+1];}
}



/* Full-image, single-mip transfer operations. This CPU backend deliberately
 * rejects partial regions and non-tightly-packed rows rather than corrupting memory. */
static int vk_copy_region_ok(const VkBufferImageCopy *r,const VkImageObj *im) {
 return r && r->bufferRowLength==0 && r->bufferImageHeight==0 &&
  r->imageSubresource.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT &&
  r->imageSubresource.mipLevel==0 && r->imageSubresource.baseArrayLayer==0 &&
  r->imageSubresource.layerCount==1 && r->imageOffset.x==0 &&
  r->imageOffset.y==0 && r->imageOffset.z==0 &&
  r->imageExtent.width==im->width && r->imageExtent.height==im->height &&
  r->imageExtent.depth==1 && !(r->bufferOffset&3u);
}
__declspec(dllexport) void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer c,VkBuffer b,VkImage im,uint32_t layout,uint32_t n,const VkBufferImageCopy *regions) {
 if(!cmd_ok(c)||c->state!=1||!buf_ok(b)||!image_ok(im)||b->device!=c->device||im->device!=c->device||!mem_ok(b->memory)||!mem_ok(im->memory)||b->memory->device!=c->device||im->memory->device!=c->device||!(b->usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)||!(im->usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT)||layout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL||!n||!regions||n>128u-c->count)return;
 for(uint32_t j=0;j<n;j++){const VkBufferImageCopy *r=&regions[j];if(!vk_copy_region_ok(r,im)||r->bufferOffset>b->size||im->size>b->size-r->bufferOffset)return;}
 for(uint32_t j=0;j<n;j++){const VkBufferImageCopy *r=&regions[j];unsigned i=c->count++;c->ops[i].op=5;c->ops[i].a=b;c->ops[i].image=im;c->ops[i].offset=r->bufferOffset;c->ops[i].size=im->size;c->ops[i].layout=layout;}
}
__declspec(dllexport) void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer c,VkImage im,uint32_t layout,VkBuffer b,uint32_t n,const VkBufferImageCopy *regions) {
 if(!cmd_ok(c)||c->state!=1||!buf_ok(b)||!image_ok(im)||b->device!=c->device||im->device!=c->device||!mem_ok(b->memory)||!mem_ok(im->memory)||b->memory->device!=c->device||im->memory->device!=c->device||!(im->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)||!(b->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||layout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL||!n||!regions||n>128u-c->count)return;
 for(uint32_t j=0;j<n;j++){const VkBufferImageCopy *r=&regions[j];if(!vk_copy_region_ok(r,im)||r->bufferOffset>b->size||im->size>b->size-r->bufferOffset)return;}
 for(uint32_t j=0;j<n;j++){const VkBufferImageCopy *r=&regions[j];unsigned i=c->count++;c->ops[i].op=6;c->ops[i].a=b;c->ops[i].image=im;c->ops[i].offset=r->bufferOffset;c->ops[i].size=im->size;c->ops[i].layout=layout;}
}
__declspec(dllexport) void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c,uint32_t srcStage,uint32_t dstStage,uint32_t dep,uint32_t memCount,const void *memBarriers,uint32_t bufferCount,const void *bufferBarriers,uint32_t imageCount,const VkImageMemoryBarrier *images) {
 (void)dep;
 if(!cmd_ok(c)||c->state!=1||!srcStage||!dstStage||memCount||memBarriers||bufferCount||bufferBarriers||(!images&&imageCount)||imageCount>128u-c->count)return;
 /* Validate the complete list before recording any operations. */
 for(uint32_t j=0;j<imageCount;j++){
  const VkImageMemoryBarrier *b=&images[j];
  if(b->sType!=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER||b->pNext||!image_ok(b->image)||b->image->device!=c->device)return;
  if(b->srcQueueFamilyIndex!=0xFFFFFFFFu||b->dstQueueFamilyIndex!=0xFFFFFFFFu)return;
  if(b->subresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||b->subresourceRange.baseMipLevel||b->subresourceRange.levelCount!=1||b->subresourceRange.baseArrayLayer||b->subresourceRange.layerCount!=1)return;
  if((b->oldLayout!=0&&b->oldLayout!=VK_IMAGE_LAYOUT_GENERAL&&b->oldLayout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&b->oldLayout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)||(b->newLayout!=VK_IMAGE_LAYOUT_GENERAL&&b->newLayout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&b->newLayout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))return;
  if(b->oldLayout==b->newLayout)return;
  if(b->newLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&!(b->image->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))return;
  if(b->newLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&!(b->image->usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT))return;
 }
 for(uint32_t j=0;j<imageCount;j++){
  const VkImageMemoryBarrier *b=&images[j];unsigned i=c->count++;
  c->ops[i].op=4;c->ops[i].image=b->image;c->ops[i].value=b->oldLayout;c->ops[i].layout=b->newLayout;
 }
}
__declspec(dllexport) void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer c,VkImage src,uint32_t srcLayout,VkImage dst,uint32_t dstLayout,uint32_t regionCount,const VkImageCopy *regions) { if(!cmd_ok(c)||c->state!=1||!image_ok(src)||!image_ok(dst)||src==dst||src->device!=c->device||dst->device!=c->device||!mem_ok(src->memory)||!mem_ok(dst->memory)||src->memory->device!=c->device||dst->memory->device!=c->device||!(src->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)||!(dst->usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT)||srcLayout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL||dstLayout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL||regionCount!=1||!regions||src->width!=dst->width||src->height!=dst->height||src->format!=dst->format||src->size!=dst->size||c->count>=128)return;/* Different image handles may alias one allocation. A full-image copy between overlapping bound ranges is invalid and must be rejected before recording. */if(src->memory==dst->memory&&src->offset<dst->offset+dst->size&&dst->offset<src->offset+src->size)return;const VkImageCopy *r=regions;if(r->srcSubresource.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||r->srcSubresource.mipLevel||r->srcSubresource.baseArrayLayer||r->srcSubresource.layerCount!=1||r->dstSubresource.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||r->dstSubresource.mipLevel||r->dstSubresource.baseArrayLayer||r->dstSubresource.layerCount!=1||r->srcOffset.x||r->srcOffset.y||r->srcOffset.z||r->dstOffset.x||r->dstOffset.y||r->dstOffset.z||r->extent.width!=src->width||r->extent.height!=src->height||r->extent.depth!=1)return;unsigned i=c->count++;c->ops[i].op=8;c->ops[i].image=src;c->ops[i].b=(VkBuffer)dst;c->ops[i].layout=srcLayout;c->ops[i].value=dstLayout;c->ops[i].size=src->size; }
__declspec(dllexport) void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer c,VkImage im,uint32_t layout,const VkClearColorValue *color,uint32_t rangeCount,const VkImageSubresourceRange *ranges) {
 if(!cmd_ok(c)||c->state!=1||!image_ok(im)||!im->memory||!color||!ranges||!rangeCount||c->count>=128||!(im->usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT))return;
 if(layout!=VK_IMAGE_LAYOUT_GENERAL&&layout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)return;
 for(uint32_t r=0;r<rangeCount;r++)if(ranges[r].aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||ranges[r].baseMipLevel||ranges[r].levelCount!=1||ranges[r].baseArrayLayer||ranges[r].layerCount!=1)return;
 unsigned i=c->count++;c->ops[i].op=3;c->ops[i].image=im;c->ops[i].layout=layout;
 for(unsigned k=0;k<4;k++)c->ops[i].color[k]=color->float32[k];
}

/* Locate a simple scalar result in a SPIR-V entry function. This deliberately
 * supports the current scalar interpreter subset, not arbitrary shader IO. */
static uint32_t vk_spirv_last_scalar_result(const uint32_t *w,size_t n,uint32_t entry) {
 size_t i=5;uint32_t active=0,last=0;
 while(i<n){uint32_t wc=w[i]>>16,op=w[i]&0xffffu;if(!wc||wc>n-i)return 0;
  if(op==54u&&wc>=5u)active=(w[i+2]==entry);
  else if(op==56u)active=0;
  else if(active&&wc>=3u){
   if((op>=109u&&op<=115u)||(op>=126u&&op<=141u)||(op>=156u&&op<=160u)||
      (op>=164u&&op<=205u)||op==83u||op==124u||op==169u)last=w[i+2];
  }i+=wc;
 }return last;
}
/* Integer-only pipeline-stage evaluator. Keeping this separate from the
 * general scalar helper avoids pulling host soft-float runtime symbols into
 * the freestanding kernel DLL. Unsupported shader instructions fail closed. */
static int vk_spirv_execute_pipeline_u32(const uint32_t *w,size_t n,uint32_t entry,uint32_t output,uint32_t *out) {
 uint32_t *ids=g_spirv_ids;uint8_t *def=g_spirv_defined;size_t i=5;uint32_t active=0,done=0,steps=0;
 if(!w||!out||output>=VK_SPIRV_MAX_IDS||!vk_spirv_validate(w,n)||!entry)return 0;
 vk_zero(g_spirv_ids,sizeof(g_spirv_ids));vk_zero(g_spirv_defined,sizeof(g_spirv_defined));
 while(i<n){uint32_t wc=w[i]>>16,op=w[i]&0xffffu;if(!wc||wc>n-i)return 0;
  if(op==43u&&wc==4u&&w[i+2]<VK_SPIRV_MAX_IDS){ids[w[i+2]]=w[i+3];def[w[i+2]]=1;}
  i+=wc;
 }
 i=5;
 while(i<n){uint32_t wc=w[i]>>16,op=w[i]&0xffffu;if(!wc||wc>n-i)return 0;
  if(op==54u&&wc>=5u)active=(w[i+2]==entry);
  else if(active&&op==56u){active=0;done=1;}
  else if(active&&op==248u&&wc==2u){}
  else if(active&&op==253u&&wc==1u){active=0;done=1;}
  else if(active&&op==128u&&wc==5u){uint32_t r=w[i+2],a=w[i+3],b=w[i+4];if(r>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||b>=VK_SPIRV_MAX_IDS||!def[a]||!def[b])return 0;ids[r]=ids[a]+ids[b];def[r]=1;}
  else if(active&&op==130u&&wc==5u){uint32_t r=w[i+2],a=w[i+3],b=w[i+4];if(r>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||b>=VK_SPIRV_MAX_IDS||!def[a]||!def[b])return 0;ids[r]=ids[a]-ids[b];def[r]=1;}
  else if(active&&op==132u&&wc==5u){uint32_t r=w[i+2],a=w[i+3],b=w[i+4];if(r>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||b>=VK_SPIRV_MAX_IDS||!def[a]||!def[b])return 0;ids[r]=ids[a]*ids[b];def[r]=1;}
  else if(active&&op==170u&&wc==5u){uint32_t r=w[i+2],a=w[i+3],b=w[i+4];if(r>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||b>=VK_SPIRV_MAX_IDS||!def[a]||!def[b])return 0;ids[r]=(ids[a]==ids[b]);def[r]=1;}
  else if(active&&op==169u&&wc==6u){uint32_t r=w[i+2],c=w[i+3],a=w[i+4],b=w[i+5];if(r>=VK_SPIRV_MAX_IDS||c>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||b>=VK_SPIRV_MAX_IDS||!def[c]||!def[a]||!def[b])return 0;ids[r]=ids[c]?ids[a]:ids[b];def[r]=1;}
  else if(active&&(op==83u||op==124u)&&wc==4u){uint32_t r=w[i+2],a=w[i+3];if(r>=VK_SPIRV_MAX_IDS||a>=VK_SPIRV_MAX_IDS||!def[a])return 0;ids[r]=ids[a];def[r]=1;}
  else if(active&&op!=14u&&op!=15u&&op!=17u&&op!=19u&&op!=33u&&op!=43u&&op!=54u&&op!=56u&&op!=248u&&op!=253u)return 0;
  if(++steps>2048u)return 0;i+=wc;
 }
 if(!done||!def[output])return 0;*out=ids[output];return 1;
}
static uint32_t vk_spirv_main_id(const uint32_t *w,size_t n) {
 size_t i=5;while(i<n){uint32_t wc=w[i]>>16,op=w[i]&0xffffu;if(!wc||wc>n-i)return 0;
  if(op==15u&&wc>=4u){const char *name=(const char*)&w[i+3];if(name[0]=='m'&&name[1]=='a'&&name[2]=='i'&&name[3]=='n')return w[i+2];}
  i+=wc;}return 0;
}

/* Bounded standard Vulkan single-color render-pass/framebuffer path. */
__declspec(dllexport) VkResult VKAPI_CALL vkCreateImageView(VkDevice d,const VkImageViewCreateInfo *ci,const void *alloc,VkImageView *out) {
 if(!dev_ok(d)||!ci||!out||alloc||ci->sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO||ci->pNext||ci->flags||ci->viewType!=VK_IMAGE_VIEW_TYPE_2D||!image_ok(ci->image)||ci->image->device!=d||ci->format!=ci->image->format||ci->subresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||ci->subresourceRange.baseMipLevel||ci->subresourceRange.levelCount!=1||ci->subresourceRange.baseArrayLayer||ci->subresourceRange.layerCount!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
 *out=0;for(unsigned i=0;i<32;i++)if(!g_image_views[i].magic){VkImageViewObj *v=&g_image_views[i];vk_zero(v,sizeof(*v));v->magic=VK_OBJECT_MAGIC;v->device=d;v->image=ci->image;v->format=ci->format;*out=(VkImageView)v;return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyImageView(VkDevice d,VkImageView v,const void *alloc) { if(alloc||!dev_ok(d)||!image_view_ok(v)||v->device!=d)return;for(unsigned i=0;i<32;i++)if(g_framebuffers[i].magic==VK_OBJECT_MAGIC&&g_framebuffers[i].view==v)return;v->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateRenderPass(VkDevice d,const VkRenderPassCreateInfo *ci,const void *alloc,VkRenderPass *out) {
 if(!dev_ok(d)||!ci||!out||alloc||ci->sType!=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO||ci->pNext||ci->flags||ci->attachmentCount!=1||!ci->pAttachments||ci->subpassCount!=1||!ci->pSubpasses||ci->dependencyCount||ci->pDependencies)return VK_ERROR_FEATURE_NOT_PRESENT;
 const VkAttachmentDescription *a=&ci->pAttachments[0];const VkSubpassDescription *sp=&ci->pSubpasses[0];
 if((a->format!=VK_FORMAT_R8G8B8A8_UNORM&&a->format!=VK_FORMAT_B8G8R8A8_UNORM)||a->samples!=1||a->loadOp>VK_ATTACHMENT_LOAD_OP_DONT_CARE||a->storeOp>VK_ATTACHMENT_STORE_OP_DONT_CARE||a->stencilLoadOp>VK_ATTACHMENT_LOAD_OP_DONT_CARE||a->stencilStoreOp>VK_ATTACHMENT_STORE_OP_DONT_CARE||sp->pipelineBindPoint!=0||sp->inputAttachmentCount||sp->pInputAttachments||sp->colorAttachmentCount!=1||!sp->pColorAttachments||sp->pColorAttachments[0].attachment!=0||sp->pColorAttachments[0].layout!=VK_IMAGE_LAYOUT_GENERAL||sp->pResolveAttachments||sp->pDepthStencilAttachment||sp->preserveAttachmentCount)return VK_ERROR_FEATURE_NOT_PRESENT;
 if((a->initialLayout!=0u&&a->initialLayout!=VK_IMAGE_LAYOUT_GENERAL)||(a->finalLayout!=VK_IMAGE_LAYOUT_GENERAL&&a->finalLayout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&a->finalLayout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))return VK_ERROR_FEATURE_NOT_PRESENT;
 *out=0;for(unsigned i=0;i<16;i++)if(!g_render_passes[i].magic){VkRenderPassObj *rp=&g_render_passes[i];vk_zero(rp,sizeof(*rp));rp->magic=VK_OBJECT_MAGIC;rp->device=d;rp->format=a->format;rp->loadOp=a->loadOp;rp->storeOp=a->storeOp;rp->initialLayout=a->initialLayout;rp->finalLayout=a->finalLayout;*out=(VkRenderPass)rp;return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyRenderPass(VkDevice d,VkRenderPass rp,const void *alloc) { if(alloc||!dev_ok(d)||!render_pass_ok(rp)||rp->device!=d)return;for(unsigned i=0;i<32;i++)if(g_framebuffers[i].magic==VK_OBJECT_MAGIC&&g_framebuffers[i].renderPass==rp)return;rp->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice d,const VkFramebufferCreateInfo *ci,const void *alloc,VkFramebuffer *out) {
 if(!dev_ok(d)||!ci||!out||alloc||ci->sType!=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO||ci->pNext||ci->flags||!render_pass_ok(ci->renderPass)||ci->renderPass->device!=d||ci->attachmentCount!=1||!ci->pAttachments||!image_view_ok(ci->pAttachments[0])||ci->pAttachments[0]->device!=d||!ci->width||!ci->height||ci->layers!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
 VkImage im=ci->pAttachments[0]->image;if(!image_ok(im)||im->width<ci->width||im->height<ci->height||im->format!=ci->renderPass->format||!(im->usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))return VK_ERROR_INITIALIZATION_FAILED;
 *out=0;for(unsigned i=0;i<32;i++)if(!g_framebuffers[i].magic){VkFramebufferObj *fb=&g_framebuffers[i];vk_zero(fb,sizeof(*fb));fb->magic=VK_OBJECT_MAGIC;fb->device=d;fb->renderPass=ci->renderPass;fb->view=ci->pAttachments[0];fb->width=ci->width;fb->height=ci->height;*out=(VkFramebuffer)fb;return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyFramebuffer(VkDevice d,VkFramebuffer fb,const void *alloc) { if(!alloc&&dev_ok(d)&&framebuffer_ok(fb)&&fb->device==d)fb->magic=0; }
__declspec(dllexport) void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c,const VkRenderPassBeginInfo *bi,uint32_t contents) {
 if(!cmd_ok(c)||c->state!=1||!bi||bi->sType!=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO||bi->pNext||contents>1||!render_pass_ok(bi->renderPass)||!framebuffer_ok(bi->framebuffer)||bi->framebuffer->device!=c->device||bi->framebuffer->renderPass!=bi->renderPass||bi->renderPass->device!=c->device||!bi->renderArea_width||!bi->renderArea_height||bi->renderArea_x||bi->renderArea_y||bi->renderArea_width>bi->framebuffer->width||bi->renderArea_height>bi->framebuffer->height||c->count>=128)return;
 VkImage im=bi->framebuffer->view->image;if(!image_ok(im)||!mem_ok(im->memory)||im->layout!=VK_IMAGE_LAYOUT_GENERAL)return;
 if(bi->renderPass->loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR&&(!bi->pClearValues||!bi->clearValueCount))return;
 c->bound_target=im;c->active_render_final_layout=bi->renderPass->finalLayout;unsigned i=c->count++;c->ops[i].op=14;c->ops[i].image=im;c->ops[i].layout=VK_IMAGE_LAYOUT_GENERAL;c->ops[i].value=bi->renderPass->loadOp;
 if(bi->pClearValues)for(unsigned k=0;k<4;k++)c->ops[i].color[k]=bi->pClearValues[0].float32[k];
}
__declspec(dllexport) void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) { if(!cmd_ok(c)||c->state!=1||!c->bound_target||c->count>=128)return;unsigned i=c->count++;c->ops[i].op=15;c->ops[i].image=c->bound_target;c->ops[i].layout=c->active_render_final_layout;c->bound_target=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d,void *cache,uint32_t count,const VkGraphicsPipelineCreateInfo *ci,const void *alloc,VkPipeline *out) {
 if(!dev_ok(d)||cache||!ci||!out||!count||count>VK_MAX_PIPELINES||alloc)return VK_ERROR_INITIALIZATION_FAILED;
 for(uint32_t k=0;k<count;k++)out[k]=0;
 for(uint32_t k=0;k<count;k++){
  const VkGraphicsPipelineCreateInfo *p=&ci[k];VkShaderModule vs=0,fs=0;uint32_t ve=0,fe=0;
  if(p->sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO||p->pNext||p->flags||p->stageCount!=2||!p->pStages||!render_pass_ok((VkRenderPass)p->renderPass)||((VkRenderPass)p->renderPass)->device!=d||p->subpass||p->basePipelineHandle||p->basePipelineIndex!=-1)return VK_ERROR_FEATURE_NOT_PRESENT;
  for(uint32_t j=0;j<p->stageCount;j++){
   const VkPipelineShaderStageCreateInfo *st=&p->pStages[j];
   if(st->sType!=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO||st->pNext||st->flags||!shader_ok(st->module)||st->module->device!=d||!st->pName||!vk_eq(st->pName,"main")||st->pSpecializationInfo)return VK_ERROR_INITIALIZATION_FAILED;
   if(st->stage==1u){if(vs)return VK_ERROR_FEATURE_NOT_PRESENT;vs=st->module;ve=vk_spirv_main_id(vs->words,vs->wordCount);}
   else if(st->stage==0x10u){if(fs)return VK_ERROR_FEATURE_NOT_PRESENT;fs=st->module;fe=vk_spirv_main_id(fs->words,fs->wordCount);}
   else return VK_ERROR_FEATURE_NOT_PRESENT;
  }
  if(!vs||!fs||!ve||!fe)return VK_ERROR_INITIALIZATION_FAILED;
  uint32_t vr=vk_spirv_last_scalar_result(vs->words,vs->wordCount,ve),fr=vk_spirv_last_scalar_result(fs->words,fs->wordCount,fe);
  if(!vr||!fr)return VK_ERROR_FEATURE_NOT_PRESENT;
  unsigned slot;for(slot=0;slot<VK_MAX_PIPELINES;slot++)if(!g_pipelines[slot].magic)break;
  if(slot==VK_MAX_PIPELINES)return VK_ERROR_TOO_MANY_OBJECTS;
  VkPipelineObj *o=&g_pipelines[slot];vk_zero(o,sizeof(*o));o->magic=VK_OBJECT_MAGIC;o->device=d;o->vertex=vs;o->fragment=fs;o->vertex_result=vr;o->fragment_result=fr;out[k]=o;
 }
 return VK_SUCCESS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyPipeline(VkDevice d,VkPipeline p,const void *alloc) {
 if(alloc||!dev_ok(d)||!pipeline_ok(p)||p->device!=d)return;
 for(unsigned i=0;i<VK_MAX_CMDS;i++)if(g_cmds[i].magic==VK_OBJECT_MAGIC&&g_cmds[i].bound_pipeline==p)g_cmds[i].bound_pipeline=0;
 p->magic=0;
}
__declspec(dllexport) void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer c,uint32_t point,VkPipeline p) {
 if(!cmd_ok(c)||c->state!=1||point!=VK_PIPELINE_BIND_POINT_GRAPHICS||!pipeline_ok(p)||p->device!=c->device||c->count>=128)return;
 c->bound_pipeline=p;unsigned i=c->count++;c->ops[i].op=10;c->ops[i].value=point;c->ops[i].size=(uintptr_t)p;
}
__declspec(dllexport) void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer c,uint32_t first,uint32_t count,const VkBuffer *buffers,const VkDeviceSize *offsets) {
 if(!cmd_ok(c)||c->state!=1||first||count!=1||!buffers||!offsets||!buf_ok(buffers[0])||buffers[0]->device!=c->device||!mem_ok(buffers[0]->memory)||!(buffers[0]->usage&VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)||offsets[0]>buffers[0]->size||c->count>=128)return;
 c->bound_vertex=buffers[0];c->bound_vertex_offset=offsets[0];unsigned i=c->count++;c->ops[i].op=11;c->ops[i].a=buffers[0];c->ops[i].offset=offsets[0];
}
/* SentinelOS software-target binding: ordinary Vulkan render-pass/framebuffer
 * objects are not yet implemented. vkCmdDraw itself uses the standard call
 * shape, but this target hook is an explicit backend extension. */
__declspec(dllexport) void VKAPI_CALL SentinelVulkanCmdBindColorTarget(VkCommandBuffer c,VkImage im) {
 if(!cmd_ok(c)||c->state!=1||!image_ok(im)||im->device!=c->device||!mem_ok(im->memory)||!(im->usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)||im->layout!=VK_IMAGE_LAYOUT_GENERAL||c->count>=128)return;
 c->bound_target=im;unsigned i=c->count++;c->ops[i].op=12;c->ops[i].image=im;
}
__declspec(dllexport) void VKAPI_CALL vkCmdDraw(VkCommandBuffer c,uint32_t vertexCount,uint32_t instanceCount,uint32_t firstVertex,uint32_t firstInstance) {
 if(!cmd_ok(c)||c->state!=1||!c->bound_pipeline||!c->bound_vertex||!c->bound_target||vertexCount!=3||instanceCount!=1||firstInstance||firstVertex>UINT32_MAX/sizeof(VkSoftVertex)||c->bound_vertex_offset>c->bound_vertex->size||((VkDeviceSize)firstVertex+3u)*sizeof(VkSoftVertex)>c->bound_vertex->size-c->bound_vertex_offset||c->count>=128)return;
 unsigned i=c->count++;c->ops[i].op=13;c->ops[i].value=firstVertex;c->ops[i].size=vertexCount;c->ops[i].a=c->bound_vertex;c->ops[i].image=c->bound_target;c->ops[i].offset=c->bound_vertex_offset;c->ops[i].offset2=(uintptr_t)c->bound_pipeline;
}

static VkResult execute_cmd(VkCmdObj *c) { if(c->state!=2)return VK_ERROR_INITIALIZATION_FAILED;for(unsigned i=0;i<c->count;i++){if(c->ops[i].op==1){VkBufferObj *b=c->ops[i].a;if(!buf_ok((VkBuffer)b)||!mem_ok(b->memory))return VK_ERROR_DEVICE_LOST;uint8_t *p=&((VkMemoryObj*)b->memory)->bytes[b->offset+c->ops[i].offset];for(uint64_t k=0;k+4<=c->ops[i].size;k+=4)vk_copy(p+k,&c->ops[i].value,4);}else if(c->ops[i].op==2){VkBufferObj *a=c->ops[i].a,*b=c->ops[i].b;if(!buf_ok((VkBuffer)a)||!buf_ok((VkBuffer)b)||!mem_ok(a->memory)||!mem_ok(b->memory))return VK_ERROR_DEVICE_LOST;uint8_t *sp=&((VkMemoryObj*)a->memory)->bytes[a->offset+c->ops[i].offset];uint8_t *dp=&((VkMemoryObj*)b->memory)->bytes[b->offset+c->ops[i].offset2];vk_copy(dp,sp,(size_t)c->ops[i].size);}else if(c->ops[i].op==4){VkImageObj *im=(VkImageObj*)c->ops[i].image;if(!image_ok((VkImage)im)||im->layout!=c->ops[i].value)return VK_ERROR_INITIALIZATION_FAILED;im->layout=c->ops[i].layout;}else if(c->ops[i].op==3){VkImageObj *im=(VkImageObj*)c->ops[i].image;if(!image_ok((VkImage)im)||!mem_ok(im->memory)||im->layout!=c->ops[i].layout)return VK_ERROR_DEVICE_LOST;uint8_t rgba[4];for(unsigned k=0;k<4;k++)rgba[k]=vk_unorm8(&c->ops[i].color[k]);if(im->format==VK_FORMAT_B8G8R8A8_UNORM){uint8_t t=rgba[0];rgba[0]=rgba[2];rgba[2]=t;}uint8_t *p=&((VkMemoryObj*)im->memory)->bytes[im->offset];for(uint64_t px=0;px<im->size;px+=4)vk_copy(p+px,rgba,4);im->layout=c->ops[i].layout;}else if(c->ops[i].op==5||c->ops[i].op==6){VkBufferObj *b=(VkBufferObj*)c->ops[i].a;VkImageObj *im=(VkImageObj*)c->ops[i].image;if(!buf_ok((VkBuffer)b)||!image_ok((VkImage)im)||!mem_ok(b->memory)||!mem_ok(im->memory)||im->layout!=c->ops[i].layout)return VK_ERROR_DEVICE_LOST;uint8_t *bp=&((VkMemoryObj*)b->memory)->bytes[b->offset+c->ops[i].offset];uint8_t *ip=&((VkMemoryObj*)im->memory)->bytes[im->offset];if(c->ops[i].op==5)vk_copy(ip,bp,(size_t)c->ops[i].size);else vk_copy(bp,ip,(size_t)c->ops[i].size);}else if(c->ops[i].op==7){VkBufferObj *b=(VkBufferObj*)c->ops[i].a;if(!buf_ok((VkBuffer)b)||!mem_ok(b->memory)||c->ops[i].offset>b->size||c->ops[i].size>b->size-c->ops[i].offset)return VK_ERROR_DEVICE_LOST;vk_copy(&((VkMemoryObj*)b->memory)->bytes[b->offset+c->ops[i].offset],c->ops[i].data,(size_t)c->ops[i].size);}else if(c->ops[i].op==8){VkImageObj *src=(VkImageObj*)c->ops[i].image,*dst=(VkImageObj*)c->ops[i].b;if(!image_ok((VkImage)src)||!image_ok((VkImage)dst)||!mem_ok(src->memory)||!mem_ok(dst->memory)||src->layout!=c->ops[i].layout||dst->layout!=c->ops[i].value)return VK_ERROR_DEVICE_LOST;vk_copy(&((VkMemoryObj*)dst->memory)->bytes[dst->offset],&((VkMemoryObj*)src->memory)->bytes[src->offset],(size_t)c->ops[i].size);}else if(c->ops[i].op==14){VkImageObj *im=(VkImageObj*)c->ops[i].image;if(!image_ok((VkImage)im)||!mem_ok(im->memory))return VK_ERROR_DEVICE_LOST;if(c->ops[i].value==VK_ATTACHMENT_LOAD_OP_CLEAR){uint8_t rgba[4];for(unsigned k=0;k<4;k++)rgba[k]=vk_unorm8(&c->ops[i].color[k]);if(im->format==VK_FORMAT_B8G8R8A8_UNORM){uint8_t t=rgba[0];rgba[0]=rgba[2];rgba[2]=t;}uint8_t *p=&((VkMemoryObj*)im->memory)->bytes[im->offset];for(uint64_t px=0;px<im->size;px+=4)vk_copy(p+px,rgba,4);}im->layout=c->ops[i].layout;}else if(c->ops[i].op==15){VkImageObj *im=(VkImageObj*)c->ops[i].image;if(!image_ok((VkImage)im))return VK_ERROR_DEVICE_LOST;im->layout=c->ops[i].layout;}else if(c->ops[i].op==9){VkImageObj *im=(VkImageObj*)c->ops[i].image;if(!image_ok((VkImage)im)||!mem_ok(im->memory)||im->layout!=VK_IMAGE_LAYOUT_GENERAL||!(im->usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))return VK_ERROR_DEVICE_LOST;VkSoftVertex v[3];vk_copy(v,c->ops[i].data,sizeof(v));uint8_t *bytes=&((VkMemoryObj*)im->memory)->bytes[im->offset];VkSoftTarget target={(uint32_t*)bytes,0,im->width,im->height,im->width,0,0,0,1};int pixels=vksoft_triangle(&target,&v[0],&v[1],&v[2],0);if(pixels<0)return VK_ERROR_INITIALIZATION_FAILED;if(im->format==VK_FORMAT_R8G8B8A8_UNORM){for(uint64_t px=0;px<im->size;px+=4){uint8_t tmp=bytes[px];bytes[px]=bytes[px+2];bytes[px+2]=tmp;}}}else if(c->ops[i].op==13){VkBufferObj *vb=(VkBufferObj*)c->ops[i].a;VkImageObj *im=(VkImageObj*)c->ops[i].image;VkPipelineObj *pl=(VkPipelineObj*)(uintptr_t)c->ops[i].offset2;if(!buf_ok((VkBuffer)vb)||!image_ok((VkImage)im)||!pipeline_ok(pl)||!mem_ok(vb->memory)||!mem_ok(im->memory)||pl->device!=c->device||im->device!=c->device||im->layout!=VK_IMAGE_LAYOUT_GENERAL||c->ops[i].offset>vb->size||((VkDeviceSize)c->ops[i].value+3u)*sizeof(VkSoftVertex)>vb->size-c->ops[i].offset)return VK_ERROR_DEVICE_LOST;uint32_t vmain=vk_spirv_main_id(pl->vertex->words,pl->vertex->wordCount),fmain=vk_spirv_main_id(pl->fragment->words,pl->fragment->wordCount),vbits=0,fbits=0;if(!vk_spirv_execute_pipeline_u32(pl->vertex->words,pl->vertex->wordCount,vmain,pl->vertex_result,&vbits)||!vk_spirv_execute_pipeline_u32(pl->fragment->words,pl->fragment->wordCount,fmain,pl->fragment_result,&fbits))return VK_ERROR_FEATURE_NOT_PRESENT;VkSoftVertex tri[3];vk_copy(tri,&((VkMemoryObj*)vb->memory)->bytes[vb->offset+c->ops[i].offset+(VkDeviceSize)c->ops[i].value*sizeof(VkSoftVertex)],sizeof(tri));static const float shades[4]={0.0f,0.33333334f,0.66666669f,1.0f};float shade=shades[(vbits?fbits:0u)&3u];for(unsigned k=0;k<3;k++)tri[k].r=tri[k].g=tri[k].b=shade,tri[k].a=1.0f;uint8_t *bytes=&((VkMemoryObj*)im->memory)->bytes[im->offset];VkSoftTarget target={(uint32_t*)bytes,0,im->width,im->height,im->width,0,0,0,1};if(vksoft_triangle(&target,&tri[0],&tri[1],&tri[2],0)<0)return VK_ERROR_INITIALIZATION_FAILED;if(im->format==VK_FORMAT_R8G8B8A8_UNORM){for(uint64_t px=0;px<im->size;px+=4){uint8_t tmp=bytes[px];bytes[px]=bytes[px+2];bytes[px+2]=tmp;}}}}return VK_SUCCESS;}

/* Experimental SentinelOS-only draw bridge; not core Vulkan vkCmdDraw.
 * Records a pre-transformed triangle for the CPU rasterizer in a color image. */
__declspec(dllexport) void VKAPI_CALL SentinelVulkanCmdDrawTriangle(VkCommandBuffer c,VkImage image,const VkSoftVertex *a,const VkSoftVertex *b,const VkSoftVertex *d) {
 if(!cmd_ok(c)||c->state!=1||!image_ok(image)||!a||!b||!d||c->count>=128)return;
 if(image->device!=c->device||!(image->usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)||
    (image->format!=VK_FORMAT_R8G8B8A8_UNORM&&image->format!=VK_FORMAT_B8G8R8A8_UNORM))return;
 if(image->width>32768u||image->height>32768u||image->size!=(uint64_t)image->width*image->height*4u)return;
 VkSoftVertex tri[3];tri[0]=*a;tri[1]=*b;tri[2]=*d;
 vk_zero(&c->ops[c->count],sizeof(c->ops[c->count]));c->ops[c->count].op=9;c->ops[c->count].image=image;c->ops[c->count].layout=VK_IMAGE_LAYOUT_GENERAL;vk_copy(c->ops[c->count].data,tri,sizeof(tri));c->count++;
}

__declspec(dllexport) VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t n,const VkSubmitInfo *sub,VkFence fence) {
 VkDevice d=0;
 for(unsigned i=0;i<VK_MAX_QUEUES;i++)if(q==(VkQueue)&g_queues[i]&&g_queues[i].magic==VK_OBJECT_MAGIC)d=g_queues[i].device;
 if(!d)return VK_ERROR_DEVICE_LOST;
 if(fence&&(!fence_ok(fence)||fence->device!=d||fence->signaled))return VK_ERROR_INITIALIZATION_FAILED;
 if(n&&!sub)return VK_ERROR_INITIALIZATION_FAILED;
 /* Validate the batch before consuming binary waits or executing work. */
 for(uint32_t i=0;i<n;i++) {
  if(sub[i].sType!=VK_STRUCTURE_TYPE_SUBMIT_INFO ||
     (sub[i].waitSemaphoreCount&&(!sub[i].pWaitSemaphores||!sub[i].pWaitDstStageMask)) ||
     (sub[i].signalSemaphoreCount&&!sub[i].pSignalSemaphores) ||
     (sub[i].commandBufferCount&&!sub[i].pCommandBuffers))return VK_ERROR_INITIALIZATION_FAILED;
  for(uint32_t j=0;j<sub[i].waitSemaphoreCount;j++) {
   VkSemaphore w=sub[i].pWaitSemaphores[j];
   if(!sem_ok(w)||w->device!=d||!sub[i].pWaitDstStageMask[j])
    return VK_ERROR_INITIALIZATION_FAILED;
   /* Binary semaphores may be waited only once in a submit. Validate the
    * complete wait list before consuming any semaphore or executing work. */
   for(uint32_t k=0;k<j;k++)
    if(sub[i].pWaitSemaphores[k]==w)return VK_ERROR_INITIALIZATION_FAILED;
   if(!w->signaled)return VK_NOT_READY;
  }
  for(uint32_t j=0;j<sub[i].signalSemaphoreCount;j++) {
   VkSemaphore z=sub[i].pSignalSemaphores[j];
   if(!sem_ok(z)||z->device!=d||z->signaled)return VK_ERROR_INITIALIZATION_FAILED;
   for(uint32_t k=0;k<j;k++)if(sub[i].pSignalSemaphores[k]==z)return VK_ERROR_INITIALIZATION_FAILED;
  }
  for(uint32_t j=0;j<sub[i].commandBufferCount;j++) {
   VkCommandBuffer c=sub[i].pCommandBuffers[j];
   if(!cmd_ok(c)||c->device!=d||c->state!=2)return VK_ERROR_INITIALIZATION_FAILED;
  }
 }
 for(uint32_t i=0;i<n;i++) {
  for(uint32_t j=0;j<sub[i].waitSemaphoreCount;j++)sub[i].pWaitSemaphores[j]->signaled=0;
  for(uint32_t j=0;j<sub[i].commandBufferCount;j++) {
   VkResult r=execute_cmd(sub[i].pCommandBuffers[j]);
   if(r)return r;
  }
  for(uint32_t j=0;j<sub[i].signalSemaphoreCount;j++)sub[i].pSignalSemaphores[j]->signaled=1;
 }
 if(fence)fence->signaled=1;
 return VK_SUCCESS;
}
__declspec(dllexport) VkResult VKAPI_CALL vkCreateFence(VkDevice d,const VkFenceCreateInfo *ci,const void *alloc,VkFence *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc)return VK_ERROR_INITIALIZATION_FAILED;for(unsigned i=0;i<VK_MAX_FENCES;i++)if(!g_fences[i].magic){g_fences[i].magic=VK_OBJECT_MAGIC;g_fences[i].device=d;g_fences[i].signaled=(ci->flags&VK_FENCE_CREATE_SIGNALED_BIT)!=0;*out=(VkFence)&g_fences[i];return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) void VKAPI_CALL vkDestroyFence(VkDevice d,VkFence f,const void *alloc) { if(!alloc&&dev_ok(d)&&fence_ok(f)&&f->device==d)f->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkGetFenceStatus(VkDevice d,VkFence f) { if(!dev_ok(d)||!fence_ok(f)||f->device!=d)return VK_ERROR_DEVICE_LOST;return f->signaled?VK_SUCCESS:VK_NOT_READY; }
__declspec(dllexport) VkResult VKAPI_CALL vkWaitForFences(VkDevice d,uint32_t n,const VkFence *fs,VkBool32 all,uint64_t timeout) { (void)timeout;if(!dev_ok(d)||!fs||!n)return VK_ERROR_INITIALIZATION_FAILED;uint32_t signaled=0;for(uint32_t i=0;i<n;i++){if(!fence_ok(fs[i])||fs[i]->device!=d)return VK_ERROR_DEVICE_LOST;if(fs[i]->signaled)signaled++;}return (all?(signaled==n):(signaled!=0))?VK_SUCCESS:VK_TIMEOUT; }
__declspec(dllexport) VkResult VKAPI_CALL vkResetFences(VkDevice d,uint32_t n,const VkFence *fs) { if(!dev_ok(d)||!fs)return VK_ERROR_INITIALIZATION_FAILED;for(uint32_t i=0;i<n;i++){if(!fence_ok(fs[i])||fs[i]->device!=d)return VK_ERROR_DEVICE_LOST;fs[i]->signaled=0;}return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d,const VkShaderModuleCreateInfo *ci,const void *alloc,VkShaderModule *out) {
 if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;
 *out=0;
 if(alloc||ci->flags||!ci->pCode||(ci->codeSize&3u)||!vk_spirv_validate(ci->pCode,ci->codeSize/4u))return VK_ERROR_INITIALIZATION_FAILED;
 for(unsigned i=0;i<VK_MAX_SHADER_MODULES;i++)if(!g_shader_modules[i].magic){
  VkShaderModuleObj *m=&g_shader_modules[i];m->magic=VK_OBJECT_MAGIC;m->device=d;
  m->wordCount=(uint32_t)(ci->codeSize/4u);vk_copy(m->words,ci->pCode,ci->codeSize);
  *out=(VkShaderModule)m;return VK_SUCCESS;
 }
 return VK_ERROR_TOO_MANY_OBJECTS;
}
__declspec(dllexport) void VKAPI_CALL vkDestroyShaderModule(VkDevice d,VkShaderModule m,const void *alloc) {
 if(alloc||!dev_ok(d)||!shader_ok(m)||m->device!=d)return;
 m->magic=0;m->wordCount=0;
}
__declspec(dllexport) VkResult VKAPI_CALL vkCreateSemaphore(VkDevice d,const VkSemaphoreCreateInfo *ci,const void *alloc,VkSemaphore *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc)return VK_ERROR_INITIALIZATION_FAILED;for(unsigned i=0;i<VK_MAX_SEMAPHORES;i++)if(!g_semaphores[i].magic){g_semaphores[i].magic=VK_OBJECT_MAGIC;g_semaphores[i].device=d;g_semaphores[i].signaled=0;*out=(VkSemaphore)&g_semaphores[i];return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) void VKAPI_CALL vkDestroySemaphore(VkDevice d,VkSemaphore s,const void *alloc) { if(!alloc&&dev_ok(d)&&sem_ok(s)&&s->device==d)s->magic=0; }
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d,const char *name) {
 if(!dev_ok(d)||!name)return (PFN_vkVoidFunction)0;
#define DPROC(n) if(vk_eq(name,#n))return (PFN_vkVoidFunction)&n
 DPROC(vkGetDeviceProcAddr);DPROC(vkCreateImageView);DPROC(vkDestroyImageView);DPROC(vkCreateRenderPass);DPROC(vkDestroyRenderPass);DPROC(vkCreateFramebuffer);DPROC(vkDestroyFramebuffer);DPROC(vkCmdBeginRenderPass);DPROC(vkCmdEndRenderPass);DPROC(vkResetCommandPool);DPROC(vkCmdUpdateBuffer);DPROC(vkCmdCopyImage);DPROC(vkCmdCopyImageToBuffer);DPROC(vkCmdCopyBufferToImage);DPROC(vkCreateImage);DPROC(vkDestroyImage);DPROC(vkGetImageMemoryRequirements);DPROC(vkBindImageMemory);DPROC(vkCmdClearColorImage);DPROC(vkCmdPipelineBarrier);DPROC(vkDestroyDevice);DPROC(vkGetDeviceQueue);DPROC(vkDeviceWaitIdle);DPROC(vkQueueWaitIdle);DPROC(vkCreateBuffer);DPROC(vkDestroyBuffer);DPROC(vkGetBufferMemoryRequirements);DPROC(vkAllocateMemory);DPROC(vkFreeMemory);DPROC(vkBindBufferMemory);DPROC(vkMapMemory);DPROC(vkUnmapMemory);DPROC(vkFlushMappedMemoryRanges);DPROC(vkInvalidateMappedMemoryRanges);DPROC(vkCreateCommandPool);DPROC(vkDestroyCommandPool);DPROC(vkAllocateCommandBuffers);DPROC(vkFreeCommandBuffers);DPROC(vkBeginCommandBuffer);DPROC(vkEndCommandBuffer);DPROC(vkResetCommandBuffer);DPROC(vkCmdFillBuffer);DPROC(vkCmdCopyBuffer);DPROC(vkQueueSubmit);DPROC(vkCreateFence);DPROC(vkDestroyFence);DPROC(vkGetFenceStatus);DPROC(vkWaitForFences);DPROC(vkResetFences);DPROC(vkCreateSemaphore);DPROC(vkDestroySemaphore);DPROC(vkCreateShaderModule);DPROC(vkDestroyShaderModule);DPROC(vkCreateGraphicsPipelines);DPROC(vkDestroyPipeline);DPROC(vkCmdBindPipeline);DPROC(vkCmdBindVertexBuffers);DPROC(vkCmdDraw);DPROC(SentinelVulkanCmdBindColorTarget);
#undef DPROC
 return (PFN_vkVoidFunction)0;
}
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst,const char *name) {
 if(!name)return (PFN_vkVoidFunction)0;
#define IPROC(n) if(vk_eq(name,#n))return (PFN_vkVoidFunction)&n
 IPROC(vkGetInstanceProcAddr);IPROC(vkCreateImageView);IPROC(vkDestroyImageView);IPROC(vkCreateRenderPass);IPROC(vkDestroyRenderPass);IPROC(vkCreateFramebuffer);IPROC(vkDestroyFramebuffer);IPROC(vkCmdBeginRenderPass);IPROC(vkCmdEndRenderPass);IPROC(vkGetDeviceProcAddr);IPROC(vkEnumerateInstanceVersion);IPROC(vkEnumerateInstanceExtensionProperties);IPROC(vkEnumerateInstanceLayerProperties);IPROC(vkCreateInstance);
 if(!inst_ok(inst))return (PFN_vkVoidFunction)0;
 IPROC(vkDestroyInstance);IPROC(vkEnumeratePhysicalDevices);IPROC(vkGetPhysicalDeviceProperties);IPROC(vkGetPhysicalDeviceFeatures);IPROC(vkGetPhysicalDeviceFormatProperties);IPROC(vkGetPhysicalDeviceImageFormatProperties);IPROC(vkGetPhysicalDeviceQueueFamilyProperties);IPROC(vkGetPhysicalDeviceMemoryProperties);IPROC(vkCreateDevice);IPROC(vkDestroyDevice);IPROC(vkGetDeviceQueue);IPROC(vkDeviceWaitIdle);IPROC(vkQueueWaitIdle);IPROC(vkCreateBuffer);IPROC(vkDestroyBuffer);IPROC(vkGetBufferMemoryRequirements);IPROC(vkCreateImage);IPROC(vkDestroyImage);IPROC(vkGetImageMemoryRequirements);IPROC(vkBindImageMemory);IPROC(vkAllocateMemory);IPROC(vkFreeMemory);IPROC(vkBindBufferMemory);IPROC(vkMapMemory);IPROC(vkUnmapMemory);IPROC(vkFlushMappedMemoryRanges);IPROC(vkInvalidateMappedMemoryRanges);IPROC(vkCreateCommandPool);IPROC(vkResetCommandPool);IPROC(vkDestroyCommandPool);IPROC(vkAllocateCommandBuffers);IPROC(vkFreeCommandBuffers);IPROC(vkBeginCommandBuffer);IPROC(vkEndCommandBuffer);IPROC(vkResetCommandBuffer);IPROC(vkCmdFillBuffer);IPROC(vkCmdUpdateBuffer);IPROC(vkCmdCopyImage);IPROC(vkCmdCopyBuffer);IPROC(vkCmdCopyBufferToImage);IPROC(vkCmdCopyImageToBuffer);IPROC(vkCmdClearColorImage);IPROC(vkCmdPipelineBarrier);IPROC(vkQueueSubmit);IPROC(vkCreateFence);IPROC(vkDestroyFence);IPROC(vkGetFenceStatus);IPROC(vkWaitForFences);IPROC(vkResetFences);IPROC(vkCreateSemaphore);IPROC(vkDestroySemaphore);IPROC(vkCreateShaderModule);IPROC(vkDestroyShaderModule);IPROC(vkCreateGraphicsPipelines);IPROC(vkDestroyPipeline);IPROC(vkCmdBindPipeline);IPROC(vkCmdBindVertexBuffers);IPROC(vkCmdDraw);IPROC(SentinelVulkanCmdBindColorTarget);
#undef IPROC
 return (PFN_vkVoidFunction)0;
}
__declspec(dllexport) uint32_t VKAPI_CALL SentinelVulkanLoaderStatus(void) { return 0x7u; }
