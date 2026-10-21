/*
 * SentinelOS CPU Vulkan runtime (native PE vulkan.dll).
 *
 * This implements a real, bounded Vulkan 1.0 software-device core:
 * instance/device/queue discovery, CPU memory, buffers, command pools,
 * command buffers, buffer fill/copy, fences and queue-idle semantics.
 * Graphics pipelines, shader execution, images/swapchains and extensions
 * are intentionally not advertised until implemented.
 */
#include <stdint.h>
#include <stddef.h>

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
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 8
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 9
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12
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
typedef struct VkPoolObj *VkCommandPool;
typedef struct VkCmdObj *VkCommandBuffer;
typedef struct VkFenceObj *VkFence;
typedef struct VkSemaphoreObj *VkSemaphore;
typedef void (VKAPI_CALL *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetInstanceProcAddr)(VkInstance,const char *);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetDeviceProcAddr)(VkDevice,const char *);

typedef struct { VkStructureType sType; const void *pNext; const char *pApplicationName; uint32_t applicationVersion; const char *pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo;
typedef struct { VkStructureType sType; const void *pNext; VkInstanceCreateFlags flags; const VkApplicationInfo *pApplicationInfo; uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames; } VkInstanceCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceQueueCreateFlags flags; uint32_t queueFamilyIndex; uint32_t queueCount; const float *pQueuePriorities; } VkDeviceQueueCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceCreateFlags flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo *pQueueCreateInfos; uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames; const void *pEnabledFeatures; } VkDeviceCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkBufferCreateFlags flags; VkDeviceSize size; VkBufferUsageFlags usage; VkSharingMode sharingMode; uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices; } VkBufferCreateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
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
typedef struct VkMemoryObj { uint32_t magic; VkDevice device; VkDeviceSize size; uint8_t used,mapped; uint8_t bytes[VK_MEMORY_BYTES]; } VkMemoryObj;
typedef struct VkPoolObj { uint32_t magic; VkDevice device; uint32_t family; } VkPoolObj;
typedef struct VkCmdObj { uint32_t magic; VkDevice device; VkCommandPool pool; uint8_t level,state; uint16_t count; struct { uint8_t op; uint8_t pad[7]; VkBuffer a,b; VkDeviceSize offset,size,offset2; uint32_t value; } ops[128]; } VkCmdObj;
typedef struct VkFenceObj { uint32_t magic; VkDevice device; uint8_t signaled; } VkFenceObj;
typedef struct VkSemaphoreObj { uint32_t magic; VkDevice device; uint8_t signaled; } VkSemaphoreObj;
static VkInstanceObj g_instances[VK_MAX_INSTANCES];
static VkPhysicalObj g_phys[VK_MAX_DEVICES];
static VkDeviceObj g_devices[VK_MAX_DEVICES];
static VkQueueObj g_queues[VK_MAX_QUEUES];
static VkBufferObj g_buffers[VK_MAX_BUFFERS];
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
static int mem_ok(VkDeviceMemory h) { for(unsigned i=0;i<VK_MAX_MEMORY;i++)if(h==(VkDeviceMemory)&g_memories[i]&&g_memories[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int pool_ok(VkCommandPool h) { for(unsigned i=0;i<VK_MAX_POOLS;i++)if(h==(VkCommandPool)&g_pools[i]&&g_pools[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int cmd_ok(VkCommandBuffer h) { for(unsigned i=0;i<VK_MAX_CMDS;i++)if(h==(VkCommandBuffer)&g_cmds[i]&&g_cmds[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int fence_ok(VkFence h) { for(unsigned i=0;i<VK_MAX_FENCES;i++)if(h==(VkFence)&g_fences[i]&&g_fences[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
static int sem_ok(VkSemaphore h) { for(unsigned i=0;i<VK_MAX_SEMAPHORES;i++)if(h==(VkSemaphore)&g_semaphores[i]&&g_semaphores[i].magic==VK_OBJECT_MAGIC)return 1;return 0; }
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
__declspec(dllexport) void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p,uint32_t *count,VkQueueFamilyProperties *out) { if(!count)return;if(!phys_ok(p)){*count=0;return;}if(!out){*count=1;return;}if(*count){vk_zero(out,sizeof(*out));out->queueFlags=VK_QUEUE_TRANSFER_BIT;out->queueCount=1;out->timestampValidBits=64;out->minImageTransferGranularity.width=1;out->minImageTransferGranularity.height=1;out->minImageTransferGranularity.depth=1;*count=1;}else *count=0; }
__declspec(dllexport) void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice p,void *out) {
 if(!out)return;
 /* Standard Vulkan 1.0 properties prefix; limits remain conservatively zero. */
 uint8_t *b=(uint8_t*)out;
 vk_zero(b,292);
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
 if(out)vk_zero(out,12);
 (void)p;(void)format;
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
__declspec(dllexport) void VKAPI_CALL vkDestroyDevice(VkDevice d,const void *alloc) { if(alloc||!dev_ok(d))return;for(unsigned i=0;i<VK_MAX_QUEUES;i++)if(g_queues[i].device==d)g_queues[i].magic=0;for(unsigned i=0;i<VK_MAX_BUFFERS;i++)if(g_buffers[i].device==d)g_buffers[i].magic=0;for(unsigned i=0;i<VK_MAX_MEMORY;i++)if(g_memories[i].device==d)g_memories[i].magic=0;for(unsigned i=0;i<VK_MAX_POOLS;i++)if(g_pools[i].device==d)g_pools[i].magic=0;for(unsigned i=0;i<VK_MAX_CMDS;i++)if(g_cmds[i].device==d)g_cmds[i].magic=0;for(unsigned i=0;i<VK_MAX_FENCES;i++)if(g_fences[i].device==d)g_fences[i].magic=0;for(unsigned i=0;i<VK_MAX_SEMAPHORES;i++)if(g_semaphores[i].device==d)g_semaphores[i].magic=0;d->magic=0; }
__declspec(dllexport) void VKAPI_CALL vkGetDeviceQueue(VkDevice d,uint32_t family,uint32_t index,VkQueue *out) { if(!out)return;*out=0;if(!dev_ok(d)||family)return;for(uint32_t i=0;i<d->queueCount;i++)if(((VkQueueObj*)d->queues[i])->index==index){*out=d->queues[i];return;} }
__declspec(dllexport) VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d) { return dev_ok(d)?VK_SUCCESS:VK_ERROR_DEVICE_LOST; }
__declspec(dllexport) VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue q) { for(unsigned i=0;i<VK_MAX_QUEUES;i++)if(q==(VkQueue)&g_queues[i]&&g_queues[i].magic==VK_OBJECT_MAGIC)return VK_SUCCESS;return VK_ERROR_DEVICE_LOST; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateBuffer(VkDevice d,const VkBufferCreateInfo *ci,const void *alloc,VkBuffer *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc||!ci->size||ci->size>UINT32_MAX)return VK_ERROR_OUT_OF_HOST_MEMORY;if(ci->sharingMode==1&&ci->queueFamilyIndexCount>1)return VK_ERROR_FEATURE_NOT_PRESENT;for(unsigned i=0;i<VK_MAX_BUFFERS;i++)if(!g_buffers[i].magic){VkBufferObj *b=&g_buffers[i];b->magic=VK_OBJECT_MAGIC;b->device=d;b->size=ci->size;b->usage=ci->usage;*out=(VkBuffer)b;return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) void VKAPI_CALL vkDestroyBuffer(VkDevice d,VkBuffer b,const void *alloc) { if(alloc||!dev_ok(d)||!buf_ok(b)||b->device!=d)return;b->magic=0; }
__declspec(dllexport) void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d,VkBuffer b,VkMemoryRequirements *out) { if(!out)return;vk_zero(out,sizeof(*out));if(!dev_ok(d)||!buf_ok(b)||b->device!=d)return;out->size=(b->size+15u)&~(VkDeviceSize)15u;out->alignment=16;out->memoryTypeBits=1; }
__declspec(dllexport) VkResult VKAPI_CALL vkAllocateMemory(VkDevice d,const VkMemoryAllocateInfo *ai,const void *alloc,VkDeviceMemory *out) { if(!dev_ok(d)||!ai||!out||ai->sType!=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc||!ai->allocationSize||ai->allocationSize>VK_MEMORY_BYTES||ai->memoryTypeIndex!=0)return VK_ERROR_OUT_OF_DEVICE_MEMORY;for(unsigned i=0;i<VK_MAX_MEMORY;i++)if(!g_memories[i].magic){VkMemoryObj *m=&g_memories[i];m->magic=VK_OBJECT_MAGIC;m->device=d;m->size=ai->allocationSize;m->used=1;m->mapped=0;vk_zero(m->bytes,VK_MEMORY_BYTES);*out=(VkDeviceMemory)m;return VK_SUCCESS;}return VK_ERROR_OUT_OF_HOST_MEMORY; }
__declspec(dllexport) void VKAPI_CALL vkFreeMemory(VkDevice d,VkDeviceMemory m,const void *alloc) { if(alloc||!dev_ok(d)||!mem_ok(m)||m->device!=d||m->mapped)return;for(unsigned i=0;i<VK_MAX_BUFFERS;i++)if(g_buffers[i].magic&&g_buffers[i].memory==m)g_buffers[i].memory=0;m->magic=0;m->used=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize off) { if(!dev_ok(d)||!buf_ok(b)||!mem_ok(m)||b->device!=d||m->device!=d)return VK_ERROR_INITIALIZATION_FAILED;if((off&15u)||off>m->size||b->size>m->size-off)return VK_ERROR_OUT_OF_DEVICE_MEMORY;b->memory=m;b->offset=off;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkMapMemory(VkDevice d,VkDeviceMemory m,VkDeviceSize off,VkDeviceSize size,VkFlags flags,void **out) { (void)flags;if(!dev_ok(d)||!mem_ok(m)||m->device!=d||!out||off>m->size)return VK_ERROR_MEMORY_MAP_FAILED;if(size==VK_WHOLE_SIZE)size=m->size-off;if(size>m->size-off||m->mapped)return VK_ERROR_MEMORY_MAP_FAILED;m->mapped=1;*out=&m->bytes[off];return VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkUnmapMemory(VkDevice d,VkDeviceMemory m) { if(dev_ok(d)&&mem_ok(m)&&m->device==d)m->mapped=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice d,uint32_t n,const void *ranges) { (void)n;(void)ranges;return dev_ok(d)?VK_SUCCESS:VK_ERROR_DEVICE_LOST; }
__declspec(dllexport) VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice d,uint32_t n,const void *ranges) { (void)n;(void)ranges;return dev_ok(d)?VK_SUCCESS:VK_ERROR_DEVICE_LOST; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateCommandPool(VkDevice d,const VkCommandPoolCreateInfo *ci,const void *alloc,VkCommandPool *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc||ci->queueFamilyIndex!=0)return VK_ERROR_FEATURE_NOT_PRESENT;for(unsigned i=0;i<VK_MAX_POOLS;i++)if(!g_pools[i].magic){g_pools[i].magic=VK_OBJECT_MAGIC;g_pools[i].device=d;g_pools[i].family=0;*out=(VkCommandPool)&g_pools[i];return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) void VKAPI_CALL vkDestroyCommandPool(VkDevice d,VkCommandPool p,const void *alloc) { if(alloc||!dev_ok(d)||!pool_ok(p)||p->device!=d)return;for(unsigned i=0;i<VK_MAX_CMDS;i++)if(g_cmds[i].pool==p)g_cmds[i].magic=0;p->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice d,const VkCommandBufferAllocateInfo *ai,VkCommandBuffer *out) { if(!dev_ok(d)||!ai||!out||ai->sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO||!pool_ok(ai->commandPool)||ai->commandPool->device!=d||ai->level>1)return VK_ERROR_INITIALIZATION_FAILED;uint32_t made=0;for(unsigned i=0;i<VK_MAX_CMDS&&made<ai->commandBufferCount;i++)if(!g_cmds[i].magic){VkCmdObj *c=&g_cmds[i];c->magic=VK_OBJECT_MAGIC;c->device=d;c->pool=ai->commandPool;c->level=(uint8_t)ai->level;c->state=0;c->count=0;out[made++]=(VkCommandBuffer)c;}if(made<ai->commandBufferCount){for(uint32_t i=0;i<made;i++)((VkCmdObj*)out[i])->magic=0;return VK_ERROR_OUT_OF_HOST_MEMORY;}return VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkFreeCommandBuffers(VkDevice d,VkCommandPool p,uint32_t n,const VkCommandBuffer *list) { if(!dev_ok(d)||!pool_ok(p)||p->device!=d||!list)return;for(uint32_t j=0;j<n;j++)if(cmd_ok(list[j])&&list[j]->device==d&&list[j]->pool==p)list[j]->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer c,const VkCommandBufferBeginInfo *bi) { if(!cmd_ok(c)||!bi||bi->sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO)return VK_ERROR_INITIALIZATION_FAILED;c->count=0;c->state=1;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c) { if(!cmd_ok(c)||c->state!=1)return VK_ERROR_INITIALIZATION_FAILED;c->state=2;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer c,VkFlags flags) { (void)flags;if(!cmd_ok(c))return VK_ERROR_INITIALIZATION_FAILED;c->count=0;c->state=0;return VK_SUCCESS; }
__declspec(dllexport) void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer c,VkBuffer b,VkDeviceSize off,VkDeviceSize size,uint32_t value) { if(!cmd_ok(c)||c->state!=1||!buf_ok(b)||!b->memory||!(b->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||c->count>=128||off>b->size||(off&3u))return;if(size==VK_WHOLE_SIZE)size=b->size-off;if(size>b->size-off||(size!=VK_WHOLE_SIZE&&(size&3u)))return;unsigned i=c->count++;c->ops[i].op=1;c->ops[i].a=b;c->ops[i].offset=off;c->ops[i].size=size;c->ops[i].value=value; }
__declspec(dllexport) void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer c,VkBuffer src,VkBuffer dst,uint32_t n,const void *regions) { if(!cmd_ok(c)||c->state!=1||!buf_ok(src)||!buf_ok(dst)||!src->memory||!dst->memory||!(src->usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)||!(dst->usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)||!regions)return;const uint64_t *r=(const uint64_t*)regions;for(uint32_t j=0;j<n&&c->count<128;j++){uint64_t so=r[j*3],doff=r[j*3+1],sz=r[j*3+2];if(so>src->size||sz>src->size-so||doff>dst->size||sz>dst->size-doff)continue;unsigned i=c->count++;c->ops[i].op=2;c->ops[i].a=src;c->ops[i].b=dst;c->ops[i].offset=so;c->ops[i].size=sz;c->ops[i].offset2=doff;} }
static VkResult execute_cmd(VkCmdObj *c) { if(c->state!=2)return VK_ERROR_INITIALIZATION_FAILED;for(unsigned i=0;i<c->count;i++){if(c->ops[i].op==1){VkBufferObj *b=c->ops[i].a;if(!buf_ok((VkBuffer)b)||!mem_ok(b->memory))return VK_ERROR_DEVICE_LOST;uint8_t *p=&((VkMemoryObj*)b->memory)->bytes[b->offset+c->ops[i].offset];for(uint64_t k=0;k+4<=c->ops[i].size;k+=4)vk_copy(p+k,&c->ops[i].value,4);}else if(c->ops[i].op==2){VkBufferObj *a=c->ops[i].a,*b=c->ops[i].b;if(!buf_ok((VkBuffer)a)||!buf_ok((VkBuffer)b)||!mem_ok(a->memory)||!mem_ok(b->memory))return VK_ERROR_DEVICE_LOST;uint8_t *sp=&((VkMemoryObj*)a->memory)->bytes[a->offset+c->ops[i].offset];uint8_t *dp=&((VkMemoryObj*)b->memory)->bytes[b->offset+c->ops[i].offset2];vk_copy(dp,sp,(size_t)c->ops[i].size);}}return VK_SUCCESS;}
__declspec(dllexport) VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t n,const VkSubmitInfo *sub,VkFence fence) { VkDevice d=0;for(unsigned i=0;i<VK_MAX_QUEUES;i++)if(q==(VkQueue)&g_queues[i]&&g_queues[i].magic==VK_OBJECT_MAGIC)d=g_queues[i].device;if(!d)return VK_ERROR_DEVICE_LOST;if(fence&&(!fence_ok(fence)||fence->device!=d))return VK_ERROR_INITIALIZATION_FAILED;for(uint32_t i=0;i<n;i++){if(!sub||sub[i].sType!=VK_STRUCTURE_TYPE_SUBMIT_INFO)return VK_ERROR_INITIALIZATION_FAILED;if(sub[i].waitSemaphoreCount||sub[i].signalSemaphoreCount)return VK_ERROR_FEATURE_NOT_PRESENT;for(uint32_t j=0;j<sub[i].commandBufferCount;j++){VkCommandBuffer c=sub[i].pCommandBuffers[j];if(!cmd_ok(c)||c->device!=d)return VK_ERROR_INITIALIZATION_FAILED;VkResult r=execute_cmd(c);if(r)return r;}}if(fence)fence->signaled=1;return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateFence(VkDevice d,const VkFenceCreateInfo *ci,const void *alloc,VkFence *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc)return VK_ERROR_INITIALIZATION_FAILED;for(unsigned i=0;i<VK_MAX_FENCES;i++)if(!g_fences[i].magic){g_fences[i].magic=VK_OBJECT_MAGIC;g_fences[i].device=d;g_fences[i].signaled=(ci->flags&VK_FENCE_CREATE_SIGNALED_BIT)!=0;*out=(VkFence)&g_fences[i];return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) void VKAPI_CALL vkDestroyFence(VkDevice d,VkFence f,const void *alloc) { if(!alloc&&dev_ok(d)&&fence_ok(f)&&f->device==d)f->magic=0; }
__declspec(dllexport) VkResult VKAPI_CALL vkGetFenceStatus(VkDevice d,VkFence f) { if(!dev_ok(d)||!fence_ok(f)||f->device!=d)return VK_ERROR_DEVICE_LOST;return f->signaled?VK_SUCCESS:VK_NOT_READY; }
__declspec(dllexport) VkResult VKAPI_CALL vkWaitForFences(VkDevice d,uint32_t n,const VkFence *fs,VkBool32 all,uint64_t timeout) { (void)timeout;if(!dev_ok(d)||!fs||!n)return VK_ERROR_INITIALIZATION_FAILED;uint32_t signaled=0;for(uint32_t i=0;i<n;i++){if(!fence_ok(fs[i])||fs[i]->device!=d)return VK_ERROR_DEVICE_LOST;if(fs[i]->signaled)signaled++;}return (all?(signaled==n):(signaled!=0))?VK_SUCCESS:VK_TIMEOUT; }
__declspec(dllexport) VkResult VKAPI_CALL vkResetFences(VkDevice d,uint32_t n,const VkFence *fs) { if(!dev_ok(d)||!fs)return VK_ERROR_INITIALIZATION_FAILED;for(uint32_t i=0;i<n;i++){if(!fence_ok(fs[i])||fs[i]->device!=d)return VK_ERROR_DEVICE_LOST;fs[i]->signaled=0;}return VK_SUCCESS; }
__declspec(dllexport) VkResult VKAPI_CALL vkCreateSemaphore(VkDevice d,const VkSemaphoreCreateInfo *ci,const void *alloc,VkSemaphore *out) { if(!dev_ok(d)||!ci||!out||ci->sType!=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO)return VK_ERROR_INITIALIZATION_FAILED;*out=0;if(alloc)return VK_ERROR_INITIALIZATION_FAILED;for(unsigned i=0;i<VK_MAX_SEMAPHORES;i++)if(!g_semaphores[i].magic){g_semaphores[i].magic=VK_OBJECT_MAGIC;g_semaphores[i].device=d;g_semaphores[i].signaled=0;*out=(VkSemaphore)&g_semaphores[i];return VK_SUCCESS;}return VK_ERROR_TOO_MANY_OBJECTS; }
__declspec(dllexport) void VKAPI_CALL vkDestroySemaphore(VkDevice d,VkSemaphore s,const void *alloc) { if(!alloc&&dev_ok(d)&&sem_ok(s)&&s->device==d)s->magic=0; }
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d,const char *name) {
 if(!dev_ok(d)||!name)return (PFN_vkVoidFunction)0;
#define DPROC(n) if(vk_eq(name,#n))return (PFN_vkVoidFunction)&n
 DPROC(vkGetDeviceProcAddr);DPROC(vkDestroyDevice);DPROC(vkGetDeviceQueue);DPROC(vkDeviceWaitIdle);DPROC(vkQueueWaitIdle);DPROC(vkCreateBuffer);DPROC(vkDestroyBuffer);DPROC(vkGetBufferMemoryRequirements);DPROC(vkAllocateMemory);DPROC(vkFreeMemory);DPROC(vkBindBufferMemory);DPROC(vkMapMemory);DPROC(vkUnmapMemory);DPROC(vkFlushMappedMemoryRanges);DPROC(vkInvalidateMappedMemoryRanges);DPROC(vkCreateCommandPool);DPROC(vkDestroyCommandPool);DPROC(vkAllocateCommandBuffers);DPROC(vkFreeCommandBuffers);DPROC(vkBeginCommandBuffer);DPROC(vkEndCommandBuffer);DPROC(vkResetCommandBuffer);DPROC(vkCmdFillBuffer);DPROC(vkCmdCopyBuffer);DPROC(vkQueueSubmit);DPROC(vkCreateFence);DPROC(vkDestroyFence);DPROC(vkGetFenceStatus);DPROC(vkWaitForFences);DPROC(vkResetFences);DPROC(vkCreateSemaphore);DPROC(vkDestroySemaphore);
#undef DPROC
 return (PFN_vkVoidFunction)0;
}
__declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance inst,const char *name) {
 if(!name)return (PFN_vkVoidFunction)0;
#define IPROC(n) if(vk_eq(name,#n))return (PFN_vkVoidFunction)&n
 IPROC(vkGetInstanceProcAddr);IPROC(vkGetDeviceProcAddr);IPROC(vkEnumerateInstanceVersion);IPROC(vkEnumerateInstanceExtensionProperties);IPROC(vkEnumerateInstanceLayerProperties);IPROC(vkCreateInstance);
 if(!inst_ok(inst))return (PFN_vkVoidFunction)0;
 IPROC(vkDestroyInstance);IPROC(vkEnumeratePhysicalDevices);IPROC(vkGetPhysicalDeviceProperties);IPROC(vkGetPhysicalDeviceFeatures);IPROC(vkGetPhysicalDeviceFormatProperties);IPROC(vkGetPhysicalDeviceQueueFamilyProperties);IPROC(vkGetPhysicalDeviceMemoryProperties);IPROC(vkCreateDevice);
#undef IPROC
 return (PFN_vkVoidFunction)0;
}
__declspec(dllexport) uint32_t VKAPI_CALL SentinelVulkanLoaderStatus(void) { return 0x7u; }
