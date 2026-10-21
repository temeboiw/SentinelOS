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

    /* Structural SPIR-V validation: a minimal vertex entry point. */
    const uint32_t shader_words[] = {
        0x07230203u, 0x00010000u, 0u, 6u, 0u,
        (2u<<16)|17u, 1u,
        (3u<<16)|14u, 0u, 1u,
        (4u<<16)|15u, 0u, 3u, 0x6e69616du,
        (2u<<16)|19u, 2u,
        (3u<<16)|33u, 4u, 2u,
        (5u<<16)|54u, 2u, 3u, 0u, 4u,
        (2u<<16)|248u, 5u,
        (1u<<16)|253u,
        (1u<<16)|56u
    };
    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(shader_words);
    smci.pCode = shader_words;
    VkShaderModule shader = 0;
    CHECK(vkCreateShaderModule(device, &smci, 0, &shader) == VK_SUCCESS);
    CHECK(shader != 0 && shader->wordCount == sizeof(shader_words)/4u);
    vkDestroyShaderModule(device, shader, 0);
    uint32_t bad_spirv[sizeof(shader_words)/4u];
    for (unsigned si=0; si<sizeof(shader_words)/4u; ++si) bad_spirv[si]=shader_words[si];
    bad_spirv[0]=0;
    smci.pCode=bad_spirv;
    CHECK(vkCreateShaderModule(device, &smci, 0, &shader) == VK_ERROR_INITIALIZATION_FAILED);

    /* Execute real SPIR-V scalar instructions: 40 + 2 must produce 42. */
    const uint32_t exec_words[] = {
        0x07230203u, 0x00010000u, 0u, 13u, 0u,
        (2u<<16)|17u, 1u,
        (3u<<16)|14u, 0u, 1u,
        (4u<<16)|15u, 0u, 3u, 0x6e69616du,
        (2u<<16)|19u, 2u,
        (3u<<16)|33u, 4u, 2u,
        (4u<<16)|43u, 2u, 5u, 40u,
        (4u<<16)|43u, 2u, 6u, 2u,
        (4u<<16)|43u, 2u, 11u, 42u,
        (5u<<16)|54u, 2u, 3u, 0u, 4u,
        (2u<<16)|248u, 8u,
        (5u<<16)|128u, 2u, 7u, 5u, 6u,
        (5u<<16)|132u, 2u, 9u, 7u, 6u,
        (5u<<16)|170u, 2u, 10u, 7u, 11u,
        (6u<<16)|169u, 2u, 12u, 10u, 7u, 5u,
        (1u<<16)|253u,
        (1u<<16)|56u
    };
    uint32_t executed = 0;
    CHECK(vk_spirv_execute_u32(exec_words, sizeof(exec_words)/4u, 3u, 7u, &executed));
    CHECK(executed == 42u);
    CHECK(vk_spirv_execute_u32(exec_words, sizeof(exec_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 84u);
    CHECK(vk_spirv_execute_u32(exec_words, sizeof(exec_words)/4u, 3u, 12u, &executed));
    CHECK(executed == 42u);
    CHECK(!vk_spirv_execute_u32(exec_words, sizeof(exec_words)/4u, 3u, 13u, &executed));

    /* Branching SPIR-V with Phi: choose 42 for true and 99 for false. */
    uint32_t branch_words[] = {
        0x07230203u, 0x00010000u, 0u, 15u, 0u,
        (2u<<16)|17u, 1u,
        (3u<<16)|14u, 0u, 1u,
        (4u<<16)|15u, 0u, 3u, 0x6e69616du,
        (2u<<16)|19u, 2u,
        (3u<<16)|33u, 4u, 2u,
        (4u<<16)|43u, 2u, 5u, 1u,
        (4u<<16)|43u, 2u, 6u, 42u,
        (4u<<16)|43u, 2u, 7u, 99u,
        (5u<<16)|54u, 2u, 3u, 0u, 4u,
        (2u<<16)|248u, 8u,
        (4u<<16)|250u, 5u, 10u, 11u,
        (2u<<16)|248u, 10u,
        (2u<<16)|249u, 12u,
        (2u<<16)|248u, 11u,
        (2u<<16)|249u, 13u,
        (2u<<16)|248u, 12u,
        (2u<<16)|249u, 14u,
        (2u<<16)|248u, 13u,
        (2u<<16)|249u, 14u,
        (2u<<16)|248u, 14u,
        (7u<<16)|245u, 2u, 9u, 6u, 12u, 7u, 13u,
        (1u<<16)|253u,
        (1u<<16)|56u
    };
    CHECK(vk_spirv_validate(branch_words, sizeof(branch_words)/4u));
    CHECK(vk_spirv_execute_u32(branch_words, sizeof(branch_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 42u);
    /* Change the true condition constant to zero and verify the other Phi input. */
    branch_words[22] = 0u;
    CHECK(vk_spirv_execute_u32(branch_words, sizeof(branch_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 99u);

    /* Signed negate, logical-not and bitwise-not scalar instructions. */
    const uint32_t unary_words[] = {
        0x07230203u, 0x00010000u, 0u, 10u, 0u,
        (2u<<16)|17u, 1u,
        (3u<<16)|14u, 0u, 1u,
        (4u<<16)|15u, 0u, 3u, 0x6e69616du,
        (2u<<16)|19u, 2u,
        (3u<<16)|33u, 4u, 2u,
        (4u<<16)|43u, 2u, 5u, 40u,
        (4u<<16)|43u, 2u, 6u, 1u,
        (5u<<16)|54u, 2u, 3u, 0u, 4u,
        (2u<<16)|248u, 8u,
        (4u<<16)|126u, 2u, 7u, 5u,
        (4u<<16)|200u, 2u, 8u, 5u,
        (4u<<16)|168u, 2u, 9u, 6u,
        (1u<<16)|253u,
        (1u<<16)|56u
    };
    CHECK(vk_spirv_validate(unary_words, sizeof(unary_words)/4u));
    CHECK(vk_spirv_execute_u32(unary_words, sizeof(unary_words)/4u, 3u, 7u, &executed));
    CHECK(executed == 0xFFFFFFD8u);
    CHECK(vk_spirv_execute_u32(unary_words, sizeof(unary_words)/4u, 3u, 8u, &executed));
    CHECK(executed == 0xFFFFFFD7u);
    CHECK(vk_spirv_execute_u32(unary_words, sizeof(unary_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 0u);

    /* Synchronous binary semaphore signal/wait behavior. */
    VkSemaphoreCreateInfo semci = {0};
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore sem = 0;
    CHECK(vkCreateSemaphore(device, &semci, 0, &sem) == VK_SUCCESS);
    VkSubmitInfo semSubmit = {0};
    semSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    semSubmit.signalSemaphoreCount = 1;
    semSubmit.pSignalSemaphores = &sem;
    CHECK(vkQueueSubmit(queue, 1, &semSubmit, 0) == VK_SUCCESS);
    VkFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    semSubmit.waitSemaphoreCount = 1;
    semSubmit.pWaitSemaphores = &sem;
    semSubmit.pWaitDstStageMask = &waitStage;
    semSubmit.signalSemaphoreCount = 0;
    semSubmit.pSignalSemaphores = 0;
    CHECK(vkQueueSubmit(queue, 1, &semSubmit, 0) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &semSubmit, 0) == VK_NOT_READY);
    vkDestroySemaphore(device, sem, 0);

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
    uint32_t update_words[2] = {0x11223344u, 0x55667788u};
    vkCmdUpdateBuffer(cmd, src, 16, sizeof(update_words), update_words);
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
    CHECK(((uint32_t *)mapped)[4] == 0x11223344u && ((uint32_t *)mapped)[5] == 0x55667788u);
    VkMappedMemoryRange mappedRange = {0};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mappedRange.memory = smem; mappedRange.offset = 0; mappedRange.size = 64;
    CHECK(vkFlushMappedMemoryRanges(device, 1, &mappedRange) == VK_SUCCESS);
    CHECK(vkInvalidateMappedMemoryRanges(device, 1, &mappedRange) == VK_SUCCESS);
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

    uint32_t formatProps[3] = {0};
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, formatProps);
    CHECK(formatProps[0] == (VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT));
    VkImageFormatProperties ifp;
    CHECK(vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM,
          1, 1, VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &ifp) == VK_SUCCESS);
    CHECK(ifp.maxExtent.width == 512 && ifp.maxExtent.height == 512 && ifp.sampleCounts == 1);
    VkImageCreateInfo imci = {0};
    imci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imci.imageType = 1;
    imci.format = VK_FORMAT_R8G8B8A8_UNORM;
    imci.extent.width = 4; imci.extent.height = 4; imci.extent.depth = 1;
    imci.mipLevels = 1; imci.arrayLayers = 1; imci.samples = 1;
    imci.tiling = 1;
    /* Unsupported storage/sampling/render-target usages must be rejected. */
    VkImage rejectedImage = 0;
    imci.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    CHECK(vkCreateImage(device, &imci, 0, &rejectedImage) == VK_ERROR_FEATURE_NOT_PRESENT);
    imci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    CHECK(vkCreateImage(device, &imci, 0, &rejectedImage) == VK_ERROR_FEATURE_NOT_PRESENT);
    imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    CHECK(vkCreateImage(device, &imci, 0, &rejectedImage) == VK_ERROR_FEATURE_NOT_PRESENT);
    imci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

    /* Read the cleared image back through the Vulkan transfer command path. */
    VkBufferCreateInfo rbci = {0};
    rbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    rbci.size = 64;
    rbci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readback = 0;
    CHECK(vkCreateBuffer(device, &rbci, 0, &readback) == VK_SUCCESS);
    VkMemoryRequirements rbreq;
    vkGetBufferMemoryRequirements(device, readback, &rbreq);
    VkMemoryAllocateInfo rbai = {0};
    rbai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    rbai.allocationSize = rbreq.size;
    VkDeviceMemory rbmem = 0;
    CHECK(vkAllocateMemory(device, &rbai, 0, &rbmem) == VK_SUCCESS);
    CHECK(vkBindBufferMemory(device, readback, rbmem, 0) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    VkImageMemoryBarrier toSrc = {0};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = 0xFFFFFFFFu; toSrc.dstQueueFamilyIndex = 0xFFFFFFFFu;
    toSrc.image = image; toSrc.subresourceRange = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &toSrc);
    VkBufferImageCopy copy = {0};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = 4; copy.imageExtent.height = 4; copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
    CHECK(vkMapMemory(device, rbmem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(((uint8_t *)mapped)[0] == 255 && ((uint8_t *)mapped)[1] == 0 &&
          ((uint8_t *)mapped)[2] == 0 && ((uint8_t *)mapped)[3] == 255);
    vkUnmapMemory(device, rbmem);
    vkDestroyBuffer(device, readback, 0);
    vkFreeMemory(device, rbmem, 0);

    vkUnmapMemory(device, imem);
    vkDestroyImage(device, image, 0);
    vkFreeMemory(device, imem, 0);
    CHECK(vkDeviceWaitIdle(device) == VK_SUCCESS);
    CHECK(vkResetCommandPool(device, pool, 0) == VK_SUCCESS);
    CHECK(cmd->state == 0 && cmd->count == 0);

    vkDestroyFence(device, fence, 0);
    vkDestroyCommandPool(device, pool, 0);
    vkDestroyBuffer(device, src, 0);
    vkDestroyBuffer(device, dst, 0);
    vkFreeMemory(device, smem, 0);
    vkFreeMemory(device, dmem, 0);
    vkDestroyDevice(device, 0);
    vkDestroyInstance(instance, 0);
    puts("SentinelOS Vulkan CPU buffer/image transfer subset: PASS");
    return 0;
}
