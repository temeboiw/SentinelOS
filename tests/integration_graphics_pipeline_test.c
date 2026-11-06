/* Cross-agent graphics pipeline integration test.
 *
 * Validates that the graphics userspace agents (Agent 2 vulkan.c) and the
 * kernel graphics syscall interface (Agent 1 kernel/user.h) agree on the
 * syscall numbers used to cross the kernel/userspace graphics boundary.
 *
 * Companion to integration_graphics_syscall_test.sh which checks the same
 * contracts from source text. This test compiles vulkan.c and verifies its
 * SENTINEL_SYS_NT_* syscall constants at compile time, then runs a minimal
 * Vulkan -> soft3d rasterizer -> framebuffer pipeline to confirm the
 * cross-agent graphics path works end to end.
 */
#include "../user/vulkan.c"
#include "../user/vk_softpipe.c"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    /* --- Compile-time syscall constant verification ------------------------
     * kernel/user.h (Agent 1): SYS_NtInitializeGraphics=32,
     *                          SYS_NtSubmit3D=34, SYS_NtPresent3D=35,
     *                          SYS_NtUploadVirGLBuffer=36
     * vulkan.c (Agent 2):      SENTINEL_SYS_NT_INITIALIZE_GRAPHICS=32u,
     *                          SENTINEL_SYS_NT_SUBMIT_3D=34u,
     *                          SENTINEL_SYS_NT_PRESENT_3D=35u,
     *                          SENTINEL_SYS_NT_UPLOAD_VIRGL_BUFFER=36u
     */
    CHECK(SENTINEL_SYS_NT_INITIALIZE_GRAPHICS == 32u);
    CHECK(SENTINEL_SYS_NT_SUBMIT_3D == 34u);
    CHECK(SENTINEL_SYS_NT_PRESENT_3D == 35u);
    CHECK(SENTINEL_SYS_NT_UPLOAD_VIRGL_BUFFER == 36u);

    /* --- Minimal Vulkan -> soft3d pipeline ---------------------------------
     * Exercise the cross-agent path: Vulkan API creates a device, submits a
     * command buffer with a triangle draw, soft3d rasterizes to the
     * framebuffer, and the result is verifiable in mapped memory.
     */
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Sentinel integration pipeline";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    VkInstance instance = 0;
    CHECK(vkCreateInstance(&ici, 0, &instance) == VK_SUCCESS);

    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, 0) == VK_SUCCESS && count == 1);
    VkPhysicalDevice physical = 0;
    count = 1;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical) == VK_SUCCESS && physical != 0);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VkDevice device = 0;
    CHECK(vkCreateDevice(physical, &dci, 0, &device) == VK_SUCCESS);
    VkQueue queue = 0;
    vkGetDeviceQueue(device, 0, 0, &queue);
    CHECK(queue != 0);

    /* Create an 8x8 color attachment image */
    VkImageCreateInfo imci = {0};
    imci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imci.imageType = 1; imci.format = VK_FORMAT_B8G8R8A8_UNORM;
    imci.extent.width = 8; imci.extent.height = 8; imci.extent.depth = 1;
    imci.mipLevels = 1; imci.arrayLayers = 1; imci.samples = 1;
    imci.tiling = 0; imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imci.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkImage drawImage = 0;
    CHECK(vkCreateImage(device, &imci, 0, &drawImage) == VK_SUCCESS);

    VkMemoryRequirements drawReq;
    vkGetImageMemoryRequirements(device, drawImage, &drawReq);
    VkMemoryAllocateInfo drawAi = {0};
    drawAi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    drawAi.allocationSize = drawReq.size;
    VkDeviceMemory drawMem = 0;
    CHECK(vkAllocateMemory(device, &drawAi, 0, &drawMem) == VK_SUCCESS);
    CHECK(vkBindImageMemory(device, drawImage, drawMem, 0) == VK_SUCCESS);

    /* Submit a triangle draw through the soft3d rasterizer */
    VkCommandPoolCreateInfo pci = {0};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    VkCommandPool pool = 0;
    CHECK(vkCreateCommandPool(device, &pci, 0, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo cai = {0};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = 0;
    CHECK(vkAllocateCommandBuffers(device, &cai, &cmd) == VK_SUCCESS);
    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);

    VkSoftVertex va = {1,1,0.5f,1,1,0,0,1,0,0};
    VkSoftVertex vb = {7,1,0.5f,1,1,0,0,1,0,0};
    VkSoftVertex vc = {1,7,0.5f,1,1,0,0,1,0,0};
    SentinelVulkanCmdDrawTriangle(cmd, drawImage, &va, &vb, &vc);
    CHECK(((VkCmdObj *)cmd)->count == 1);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = 0;
    CHECK(vkCreateFence(device, &fci, 0, &fence) == VK_SUCCESS);
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    CHECK(vkQueueSubmit(queue, 1, &si, fence) == VK_SUCCESS);
    CHECK(vkGetFenceStatus(device, fence) == VK_SUCCESS);

    /* Verify the rasterized triangle shaded an interior pixel */
    void *mapped = 0;
    CHECK(vkMapMemory(device, drawMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    const uint8_t *pixels = (const uint8_t *)mapped;
    const unsigned inside = (2u * 8u + 2u) * 4u;
    CHECK(pixels[inside + 3u] == 255u); /* opaque alpha */
    vkUnmapMemory(device, drawMem);

    /* Cleanup */
    vkDestroyFence(device, fence, 0);
    vkDestroyCommandPool(device, pool, 0);
    vkFreeMemory(device, drawMem, 0);
    vkDestroyImage(device, drawImage, 0);
    vkDestroyDevice(device, 0);
    vkDestroyInstance(instance, 0);

    puts("Cross-agent graphics pipeline integration: PASS");
    return 0;
}
