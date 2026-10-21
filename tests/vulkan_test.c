/* Host-side conformance smoke test for SentinelOS' implemented Vulkan subset. */
#include "../user/vulkan.c"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Sentinel Vulkan smoke";
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
    VkQueueFamilyProperties qprop[1];
    count = 1;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, qprop);
    CHECK(count == 1 && qprop[0].queueCount == 1);
    CHECK(qprop[0].queueFlags == VK_QUEUE_TRANSFER_BIT);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    CHECK(mp.memoryTypeCount == 1 && mp.memoryHeapCount == 1);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device = 0;
    CHECK(vkCreateDevice(physical, &dci, 0, &device) == VK_SUCCESS);
    VkQueue queue = 0;
    vkGetDeviceQueue(device, 0, 0, &queue);
    CHECK(queue != 0);

    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = 64;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer src = 0, dst = 0;
    CHECK(vkCreateBuffer(device, &bci, 0, &src) == VK_SUCCESS);
    CHECK(vkCreateBuffer(device, &bci, 0, &dst) == VK_SUCCESS);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, src, &req);
    CHECK(req.size >= 64 && req.memoryTypeBits == 1);
    VkMemoryAllocateInfo mai = {0};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = 0;
    VkDeviceMemory smem = 0, dmem = 0;
    CHECK(vkAllocateMemory(device, &mai, 0, &smem) == VK_SUCCESS);
    CHECK(vkAllocateMemory(device, &mai, 0, &dmem) == VK_SUCCESS);
    CHECK(vkBindBufferMemory(device, src, smem, 0) == VK_SUCCESS);
    CHECK(vkBindBufferMemory(device, dst, dmem, 0) == VK_SUCCESS);

    VkCommandPoolCreateInfo pci = {0};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    VkCommandPool pool = 0;
    CHECK(vkCreateCommandPool(device, &pci, 0, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo cai = {0};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = 0;
    CHECK(vkAllocateCommandBuffers(device, &cai, &cmd) == VK_SUCCESS);
    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    vkCmdFillBuffer(cmd, src, 0, 64, 0xA1B2C3D4u);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = 0;
    CHECK(vkCreateFence(device, &fci, 0, &fence) == VK_SUCCESS);
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    CHECK(vkQueueSubmit(queue, 1, &si, fence) == VK_SUCCESS);
    CHECK(vkGetFenceStatus(device, fence) == VK_SUCCESS);
    void *mapped = 0;
    CHECK(vkMapMemory(device, smem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(*(uint32_t *)mapped == 0xA1B2C3D4u);
    vkUnmapMemory(device, smem);

    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    struct { VkDeviceSize srcOffset, dstOffset, size; } region = {0, 0, 64};
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkResetFences(device, 1, &fence) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, fence) == VK_SUCCESS);
    CHECK(vkMapMemory(device, dmem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(*(uint32_t *)mapped == 0xA1B2C3D4u);
    vkUnmapMemory(device, dmem);

    VkImageCreateInfo imci = {0};
    imci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imci.imageType = 1;
    imci.format = VK_FORMAT_R8G8B8A8_UNORM;
    imci.extent.width = 4; imci.extent.height = 4; imci.extent.depth = 1;
    imci.mipLevels = 1; imci.arrayLayers = 1; imci.samples = 1;
    imci.tiling = 1; imci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage image = 0;
    CHECK(vkCreateImage(device, &imci, 0, &image) == VK_SUCCESS);
    VkMemoryRequirements imreq;
    vkGetImageMemoryRequirements(device, image, &imreq);
    VkMemoryAllocateInfo imai = {0};
    imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imai.allocationSize = imreq.size; imai.memoryTypeIndex = 0;
    VkDeviceMemory imem = 0;
    CHECK(vkAllocateMemory(device, &imai, 0, &imem) == VK_SUCCESS);
    CHECK(vkBindImageMemory(device, image, imem, 0) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    VkClearColorValue clear = {{1.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageMemoryBarrier imb = {0};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.oldLayout = 0; imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imb.srcQueueFamilyIndex = 0xFFFFFFFFu; imb.dstQueueFamilyIndex = 0xFFFFFFFFu;
    imb.image = image; imb.subresourceRange = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, 0, 0, 0, 1, &imb);
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
    CHECK(vkMapMemory(device, imem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(((uint8_t *)mapped)[0] == 255 && ((uint8_t *)mapped)[1] == 0 &&
          ((uint8_t *)mapped)[2] == 0 && ((uint8_t *)mapped)[3] == 255);
    vkUnmapMemory(device, imem);
    vkDestroyImage(device, image, 0);
    vkFreeMemory(device, imem, 0);
    CHECK(vkDeviceWaitIdle(device) == VK_SUCCESS);

    vkDestroyFence(device, fence, 0);
    vkDestroyCommandPool(device, pool, 0);
    vkDestroyBuffer(device, src, 0);
    vkDestroyBuffer(device, dst, 0);
    vkFreeMemory(device, smem, 0);
    vkFreeMemory(device, dmem, 0);
    vkDestroyDevice(device, 0);
    vkDestroyInstance(instance, 0);
    puts("SentinelOS Vulkan CPU buffer/image subset: PASS");
    return 0;
}
