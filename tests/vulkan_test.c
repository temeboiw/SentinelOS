/* Host-side conformance smoke test for SentinelOS' implemented Vulkan subset. */
#include "../user/vulkan.c"
#include "../user/vk_softpipe.c"
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
    CHECK(qprop[0].queueFlags == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT));
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

    /* Sampler handles are real device-owned objects. This first bounded
       slice accepts nearest/linear filtering and repeat/clamp-to-edge only. */
    VkSamplerCreateInfo samplerCi = {0};
    samplerCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCi.magFilter = VK_FILTER_LINEAR;
    samplerCi.minFilter = VK_FILTER_NEAREST;
    samplerCi.mipmapMode = 0;
    samplerCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.maxAnisotropy = 1.0f;
    VkSampler sampler = 0;
    CHECK(vkCreateSampler(device, &samplerCi, 0, &sampler) == VK_SUCCESS);
    CHECK(sampler != 0 && sampler->magic == VK_OBJECT_MAGIC && sampler->device == device);
    VkSamplerCreateInfo unsupportedSamplerCi = samplerCi;
    unsupportedSamplerCi.anisotropyEnable = 1;
    VkSampler rejectedSampler = 0;
    CHECK(vkCreateSampler(device, &unsupportedSamplerCi, 0, &rejectedSampler) == VK_ERROR_FEATURE_NOT_PRESENT);
    CHECK(rejectedSampler == 0);
    vkDestroySampler(device, sampler, 0);
    CHECK(sampler->magic == 0);

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
    CHECK(vk_spirv_execute_pipeline_u32(branch_words, sizeof(branch_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 42u);
    /* Change the true condition constant to zero and verify the other Phi input. */
    branch_words[22] = 0u;
    CHECK(vk_spirv_execute_u32(branch_words, sizeof(branch_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 99u);
    CHECK(vk_spirv_execute_pipeline_u32(branch_words, sizeof(branch_words)/4u, 3u, 9u, &executed));
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

    /* SPIR-V scalar bit reverse and population count. */
    const uint32_t bit_words[] = {
        0x07230203u, 0x00010000u, 0u, 10u, 0u,
        (2u<<16)|17u, 1u,
        (3u<<16)|14u, 0u, 1u,
        (4u<<16)|15u, 0u, 3u, 0x6e69616du,
        (2u<<16)|19u, 2u,
        (3u<<16)|33u, 4u, 2u,
        (4u<<16)|43u, 2u, 5u, 0x0000000Bu,
        (5u<<16)|54u, 2u, 3u, 0u, 4u,
        (2u<<16)|248u, 8u,
        (4u<<16)|204u, 2u, 7u, 5u,
        (4u<<16)|205u, 2u, 9u, 5u,
        (1u<<16)|253u,
        (1u<<16)|56u
    };
    CHECK(vk_spirv_validate(bit_words, sizeof(bit_words)/4u));
    CHECK(vk_spirv_execute_u32(bit_words, sizeof(bit_words)/4u, 3u, 7u, &executed));
    CHECK(executed == 0xD0000000u);
    CHECK(vk_spirv_execute_u32(bit_words, sizeof(bit_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 3u);

    /* SPIR-V bitfield insert and signed/unsigned extraction. */
    const uint32_t bitfield_words[] = {
        0x07230203u, 0x00010000u, 0u, 20u, 0u,
        (2u<<16)|17u, 1u,
        (3u<<16)|14u, 0u, 1u,
        (4u<<16)|15u, 0u, 3u, 0x6e69616du,
        (2u<<16)|19u, 2u,
        (3u<<16)|33u, 4u, 2u,
        (4u<<16)|43u, 2u, 5u, 0x000000F0u,
        (4u<<16)|43u, 2u, 6u, 0x00000005u,
        (4u<<16)|43u, 2u, 7u, 4u,
        (4u<<16)|43u, 2u, 8u, 4u,
        (4u<<16)|43u, 2u, 9u, 0x00000080u,
        (5u<<16)|54u, 2u, 3u, 0u, 4u,
        (2u<<16)|248u, 10u,
        (7u<<16)|201u, 2u, 11u, 5u, 6u, 4u, 4u,
        (6u<<16)|203u, 2u, 12u, 11u, 4u, 4u,
        (6u<<16)|202u, 2u, 13u, 9u, 0u, 8u,
        (1u<<16)|253u,
        (1u<<16)|56u
    };
    CHECK(vk_spirv_validate(bitfield_words, sizeof(bitfield_words)/4u));
    CHECK(vk_spirv_execute_u32(bitfield_words, sizeof(bitfield_words)/4u, 3u, 11u, &executed));
    CHECK(executed == 0x00000050u);
    CHECK(vk_spirv_execute_u32(bitfield_words, sizeof(bitfield_words)/4u, 3u, 12u, &executed));
    CHECK(executed == 5u);
    CHECK(vk_spirv_execute_u32(bitfield_words, sizeof(bitfield_words)/4u, 3u, 13u, &executed));
    CHECK(executed == 0xFFFFFF80u);

    /* Scalar IEEE-754 shader arithmetic and ordered/unordered comparisons. */
    const uint32_t float_words[] = {
        0x07230203u, 0x00010000u, 0u, 28u, 0u,
        (2u<<16)|17u, 1u,
        (3u<<16)|14u, 0u, 1u,
        (4u<<16)|15u, 0u, 3u, 0x6e69616du,
        (2u<<16)|19u, 2u,
        (3u<<16)|33u, 4u, 2u,
        (4u<<16)|43u, 2u, 5u, 0x3fc00000u,
        (4u<<16)|43u, 2u, 6u, 0x40000000u,
        (4u<<16)|43u, 2u, 11u, 0xC0000000u,
        (4u<<16)|43u, 2u, 12u, 3u,
        (4u<<16)|43u, 2u, 19u, 0x40B00000u,
        (4u<<16)|43u, 2u, 20u, 0x40000000u,
        (4u<<16)|43u, 2u, 21u, 0xC0B00000u,
        (4u<<16)|43u, 2u, 22u, 0x40000000u,
        (5u<<16)|54u, 2u, 3u, 0u, 4u,
        (2u<<16)|248u, 17u,
        (5u<<16)|129u, 2u, 7u, 5u, 6u,
        (5u<<16)|133u, 2u, 8u, 7u, 6u,
        (4u<<16)|127u, 2u, 9u, 8u,
        (5u<<16)|186u, 2u, 10u, 8u, 7u,
        (4u<<16)|109u, 2u, 13u, 5u,
        (4u<<16)|111u, 2u, 14u, 12u,
        (4u<<16)|158u, 2u, 15u, 5u,
        (4u<<16)|160u, 2u, 16u, 11u,
        (5u<<16)|140u, 2u, 18u, 19u, 20u,
        (5u<<16)|141u, 2u, 23u, 21u, 22u,
        (4u<<16)|113u, 2u, 24u, 12u,
        (4u<<16)|114u, 2u, 25u, 12u,
        (1u<<16)|253u,
        (1u<<16)|56u
    };
    CHECK(vk_spirv_validate(float_words, sizeof(float_words)/4u));
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 7u, &executed));
    CHECK(executed == 0x40600000u); /* 1.5 + 2.0 = 3.5 */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 8u, &executed));
    CHECK(executed == 0x40E00000u); /* 3.5 * 2.0 = 7.0 */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 9u, &executed));
    CHECK(executed == 0xC0E00000u); /* FNegate */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 10u, &executed));
    CHECK(executed == 1u); /* ordered greater-than */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 13u, &executed));
    CHECK(executed == 1u); /* float-to-unsigned conversion truncates toward zero */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 14u, &executed));
    CHECK(executed == 0x40400000u); /* signed integer to float */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 15u, &executed));
    CHECK(executed == 1u); /* finite predicate */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 16u, &executed));
    CHECK(executed == 1u); /* sign-bit predicate */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 18u, &executed));
    CHECK(executed == 0x3FC00000u); /* 5.5 rem 2.0 = 1.5 */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 23u, &executed));
    CHECK(executed == 0x3F000000u); /* -5.5 mod 2.0 = 0.5 */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 24u, &executed));
    CHECK(executed == 3u); /* UConvert 32-bit bit-preserving form */
    CHECK(vk_spirv_execute_u32(float_words, sizeof(float_words)/4u, 3u, 25u, &executed));
    CHECK(executed == 3u); /* SConvert 32-bit bit-preserving form */

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
    VkSemaphore duplicateWaits[2] = {sem, sem};
    VkFlags duplicateStages[2] = {VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT};
    semSubmit.waitSemaphoreCount = 2;
    semSubmit.pWaitSemaphores = duplicateWaits;
    semSubmit.pWaitDstStageMask = duplicateStages;
    semSubmit.signalSemaphoreCount = 0;
    semSubmit.pSignalSemaphores = 0;
    CHECK(vkQueueSubmit(queue, 1, &semSubmit, 0) == VK_ERROR_INITIALIZATION_FAILED);
    /* Invalid submissions must not consume the semaphore. */
    semSubmit.waitSemaphoreCount = 1;
    semSubmit.pWaitSemaphores = &sem;
    semSubmit.pWaitDstStageMask = &waitStage;
    waitStage = 0;
    CHECK(vkQueueSubmit(queue, 1, &semSubmit, 0) == VK_ERROR_INITIALIZATION_FAILED);
    waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
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
    /* Bound allocations cannot be freed out from under live resources. */
    vkFreeMemory(device, smem, 0);
    CHECK(mem_ok(smem));
    CHECK(vkBindBufferMemory(device, src, smem, 0) == VK_ERROR_INITIALIZATION_FAILED);

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
    /* Cross-region overlap is tested after the original fill/update submission,
       so resetting the command buffer cannot discard that test's operations. */
    VkBuffer alias = 0;
    VkBufferCreateInfo aliasCi = bci;
    aliasCi.size = 32;
    CHECK(vkCreateBuffer(device, &aliasCi, 0, &alias) == VK_SUCCESS);
    CHECK(vkBindBufferMemory(device, alias, smem, 32) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    struct { VkDeviceSize srcOffset, dstOffset, size; } crossOverlap[2] =
        {{0, 0, 16}, {32, 16, 16}};
    vkCmdCopyBuffer(cmd, src, alias, 2, crossOverlap);
    CHECK(((VkCmdObj *)cmd)->count == 0);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    vkDestroyBuffer(device, alias, 0);

    /* Vulkan forbids vkCmdCopyBuffer with identical source/destination handles. */
    uint64_t selfCopyRegion[3] = {0, 16, 16};
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    vkCmdCopyBuffer(cmd, src, src, 1, selfCopyRegion);
    CHECK(((VkCmdObj *)cmd)->count == 0);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);

    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    struct { VkDeviceSize srcOffset, dstOffset, size; } regions[2] = {{0, 0, 16}, {16, 32, 16}};
    vkCmdCopyBuffer(cmd, src, dst, 2, regions);
    CHECK(((VkCmdObj *)cmd)->count == 2);
    struct { VkDeviceSize srcOffset, dstOffset, size; } invalidRegion = {60, 0, 8};
    vkCmdCopyBuffer(cmd, src, dst, 1, &invalidRegion);
    CHECK(((VkCmdObj *)cmd)->count == 2); /* invalid region rejected atomically */
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkResetFences(device, 1, &fence) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, fence) == VK_SUCCESS);
    CHECK(vkMapMemory(device, dmem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(*(uint32_t *)mapped == 0xA1B2C3D4u);
    CHECK(((uint32_t *)mapped)[8] == 0x11223344u); /* second discontiguous region */
    CHECK(((uint32_t *)mapped)[4] == 0u); /* gap was not overwritten */
    vkUnmapMemory(device, dmem);

    uint32_t formatProps[3] = {0};
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, formatProps);
    CHECK(formatProps[0] == (VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT));
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
    /* Storage remains unsupported; sampled images now have real backing memory. */
    VkImage rejectedImage = 0;
    imci.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    CHECK(vkCreateImage(device, &imci, 0, &rejectedImage) == VK_ERROR_FEATURE_NOT_PRESENT);
    imci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    CHECK(vkCreateImage(device, &imci, 0, &rejectedImage) == VK_SUCCESS);
    vkDestroyImage(device, rejectedImage, 0); rejectedImage = 0;
    imci.tiling = 0; /* color attachments are currently supported with optimal tiling */
    imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    CHECK(vkCreateImage(device, &imci, 0, &rejectedImage) == VK_SUCCESS);
    vkDestroyImage(device, rejectedImage, 0);
    rejectedImage = 0;
    imci.tiling = 1;
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
    /* Invalid second image barrier must not partially record the first. */
    VkImageMemoryBarrier barrierBatch[2] = {0};
    for (unsigned bi2 = 0; bi2 < 2; ++bi2) {
        barrierBatch[bi2].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierBatch[bi2].srcQueueFamilyIndex = 0xFFFFFFFFu;
        barrierBatch[bi2].dstQueueFamilyIndex = 0xFFFFFFFFu;
        barrierBatch[bi2].image = image;
        barrierBatch[bi2].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrierBatch[bi2].subresourceRange.levelCount = 1;
        barrierBatch[bi2].subresourceRange.layerCount = 1;
        barrierBatch[bi2].oldLayout = 0;
        barrierBatch[bi2].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    barrierBatch[1].pNext = (const void *)1;
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 2, barrierBatch);
    CHECK(((VkCmdObj *)cmd)->count == 0); /* atomic rejection */
    barrierBatch[1].pNext = 0;
    barrierBatch[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierBatch[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 2, barrierBatch);
    CHECK(((VkCmdObj *)cmd)->count == 2);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    vkFreeMemory(device, imem, 0);
    CHECK(mem_ok(imem));
    CHECK(vkBindImageMemory(device, image, imem, 0) == VK_ERROR_INITIALIZATION_FAILED);
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
    uint32_t recordedCopies = ((VkCmdObj *)cmd)->count;
    VkBufferImageCopy invalidCopy = copy;
    invalidCopy.bufferOffset = 60; /* 64-byte image does not fit at this offset */
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &invalidCopy);
    CHECK(((VkCmdObj *)cmd)->count == recordedCopies); /* invalid copy is rejected atomically */
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
    CHECK(vkMapMemory(device, rbmem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(((uint8_t *)mapped)[0] == 255 && ((uint8_t *)mapped)[1] == 0 &&
          ((uint8_t *)mapped)[2] == 0 && ((uint8_t *)mapped)[3] == 255);
    vkUnmapMemory(device, rbmem);
    vkDestroyBuffer(device, readback, 0);
    vkFreeMemory(device, rbmem, 0);

    /* Exercise CPU triangle rasterization through command-buffer submission. */
    VkImageCreateInfo drawCi = {0};
    drawCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    drawCi.imageType = 1; drawCi.format = VK_FORMAT_B8G8R8A8_UNORM;
    drawCi.extent.width = 8; drawCi.extent.height = 8; drawCi.extent.depth = 1;
    drawCi.mipLevels = 1; drawCi.arrayLayers = 1; drawCi.samples = 1;
    drawCi.tiling = 0; drawCi.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    drawCi.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkImage drawImage = 0;
    CHECK(vkCreateImage(device, &drawCi, 0, &drawImage) == VK_SUCCESS);
    VkMemoryRequirements drawReq;
    vkGetImageMemoryRequirements(device, drawImage, &drawReq);
    VkMemoryAllocateInfo drawAi = {0};
    drawAi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; drawAi.allocationSize = drawReq.size;
    VkDeviceMemory drawMem = 0;
    CHECK(vkAllocateMemory(device, &drawAi, 0, &drawMem) == VK_SUCCESS);
    CHECK(vkBindImageMemory(device, drawImage, drawMem, 0) == VK_SUCCESS);
    VkSoftVertex va = {1,1,0.5f,1,1,0,0,1,0,0};
    VkSoftVertex vb = {7,1,0.5f,1,1,0,0,1,0,0};
    VkSoftVertex vc = {1,7,0.5f,1,1,0,0,1,0,0};
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    SentinelVulkanCmdDrawTriangle(cmd, drawImage, &va, &vb, &vc);
    CHECK(((VkCmdObj *)cmd)->count == 1);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
    CHECK(vkMapMemory(device, drawMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(((uint8_t *)mapped)[(2u*8u+2u)*4u+2u] == 255);
    CHECK(((uint8_t *)mapped)[(2u*8u+2u)*4u+3u] == 255);
    CHECK(((uint8_t *)mapped)[(7u*8u+7u)*4u+3u] == 0);
    vkUnmapMemory(device, drawMem);

    /* Standard-shaped single-color image view, render pass, and framebuffer. */
    VkImageViewCreateInfo viewCi = {0};
    viewCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCi.image = drawImage; viewCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCi.format = drawCi.format;
    viewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCi.subresourceRange.levelCount = 1; viewCi.subresourceRange.layerCount = 1;
    VkImageView drawView = 0;
    CHECK(vkCreateImageView(device, &viewCi, 0, &drawView) == VK_SUCCESS);
    VkAttachmentDescription attachment = {0};
    attachment.format = drawCi.format; attachment.samples = 1;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = 0; subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    VkRenderPassCreateInfo rpCi = {0};
    rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = 1; rpCi.pAttachments = &attachment;
    rpCi.subpassCount = 1; rpCi.pSubpasses = &subpass;
    VkRenderPass drawRenderPass = 0;
    CHECK(vkCreateRenderPass(device, &rpCi, 0, &drawRenderPass) == VK_SUCCESS);
    VkFramebufferCreateInfo fbCi = {0};
    fbCi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCi.renderPass = drawRenderPass; fbCi.attachmentCount = 1;
    fbCi.pAttachments = &drawView; fbCi.width = 8; fbCi.height = 8; fbCi.layers = 1;
    VkFramebuffer drawFramebuffer = 0;
    CHECK(vkCreateFramebuffer(device, &fbCi, 0, &drawFramebuffer) == VK_SUCCESS);

    /* End-to-end bounded graphics pipeline: shader modules execute during
     * vkCmdDraw and their scalar fragment result shades the CPU rasterizer. */
    smci.codeSize = sizeof(exec_words);
    smci.pCode = exec_words;
    VkShaderModule vertexShader = 0, fragmentShader = 0;
    CHECK(vkCreateShaderModule(device, &smci, 0, &vertexShader) == VK_SUCCESS);
    /* Add OpImageSampleImplicitLod to the fragment entry. The bounded software
       pipeline resolves this instruction against the bound image-sampler descriptor. */
    uint32_t texture_shader_words[sizeof(exec_words)/sizeof(exec_words[0]) + 5u];
    uint32_t exec_word_count = (uint32_t)(sizeof(exec_words)/sizeof(exec_words[0]));
    for (uint32_t wi = 0; wi < exec_word_count - 2u; ++wi) texture_shader_words[wi] = exec_words[wi];
    texture_shader_words[3] = 14u;
    uint32_t tex_sample_at = exec_word_count - 2u;
    texture_shader_words[tex_sample_at++] = (5u<<16)|87u;
    texture_shader_words[tex_sample_at++] = 2u; texture_shader_words[tex_sample_at++] = 13u;
    texture_shader_words[tex_sample_at++] = 11u; texture_shader_words[tex_sample_at++] = 10u;
    texture_shader_words[tex_sample_at++] = exec_words[exec_word_count - 2u];
    texture_shader_words[tex_sample_at++] = exec_words[exec_word_count - 1u];
    smci.codeSize = sizeof(texture_shader_words); smci.pCode = texture_shader_words;
    CHECK(vkCreateShaderModule(device, &smci, 0, &fragmentShader) == VK_SUCCESS);
    VkPipelineShaderStageCreateInfo stages[2] = {{0}};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = 1u; stages[0].module = vertexShader; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = 0x10u; stages[1].module = fragmentShader; stages[1].pName = "main";
    VkImageCreateInfo texCi = {0};
    texCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; texCi.imageType = 1;
    texCi.format = VK_FORMAT_R8G8B8A8_UNORM; texCi.extent.width = 2; texCi.extent.height = 2; texCi.extent.depth = 1;
    texCi.mipLevels = 1; texCi.arrayLayers = 1; texCi.samples = 1; texCi.tiling = 1;
    texCi.usage = VK_IMAGE_USAGE_SAMPLED_BIT; texCi.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkImage texImage = 0; CHECK(vkCreateImage(device, &texCi, 0, &texImage) == VK_SUCCESS);
    VkMemoryRequirements texReq; vkGetImageMemoryRequirements(device, texImage, &texReq);
    VkMemoryAllocateInfo texAi = {0}; texAi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; texAi.allocationSize = texReq.size; texAi.memoryTypeIndex = 0;
    VkDeviceMemory texMem = 0; CHECK(vkAllocateMemory(device, &texAi, 0, &texMem) == VK_SUCCESS);
    CHECK(vkBindImageMemory(device, texImage, texMem, 0) == VK_SUCCESS);
    CHECK(vkMapMemory(device, texMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    for (unsigned ti = 0; ti < 4; ++ti) { ((uint8_t *)mapped)[ti*4] = 0; ((uint8_t *)mapped)[ti*4+1] = 0; ((uint8_t *)mapped)[ti*4+2] = 255; ((uint8_t *)mapped)[ti*4+3] = 255; }
    vkUnmapMemory(device, texMem);
    VkImageViewCreateInfo texViewCi = {0}; texViewCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; texViewCi.image = texImage;
    texViewCi.viewType = VK_IMAGE_VIEW_TYPE_2D; texViewCi.format = texCi.format;
    texViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; texViewCi.subresourceRange.levelCount = 1; texViewCi.subresourceRange.layerCount = 1;
    VkImageView texView = 0; CHECK(vkCreateImageView(device, &texViewCi, 0, &texView) == VK_SUCCESS);
    VkSamplerCreateInfo texSamplerCi = {0}; texSamplerCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    texSamplerCi.magFilter = VK_FILTER_NEAREST; texSamplerCi.minFilter = VK_FILTER_NEAREST;
    texSamplerCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; texSamplerCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; texSamplerCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texSamplerCi.maxAnisotropy = 1.0f; VkSampler texSampler = 0;
    CHECK(vkCreateSampler(device, &texSamplerCi, 0, &texSampler) == VK_SUCCESS);
    VkDescriptorSetLayoutBinding descBindings[2] = {{0}};
    descBindings[0].binding = 0; descBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descBindings[0].descriptorCount = 1; descBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    descBindings[1].binding = 1; descBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descBindings[1].descriptorCount = 1; descBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslCi = {0};
    dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslCi.bindingCount = 2; dslCi.pBindings = descBindings;
    VkDescriptorSetLayout descriptorLayout = 0;
    CHECK(vkCreateDescriptorSetLayout(device, &dslCi, 0, &descriptorLayout) == VK_SUCCESS);
    VkPipelineLayoutCreateInfo plCi = {0};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1; plCi.pSetLayouts = &descriptorLayout;
    VkPipelineLayout pipelineLayout = 0;
    CHECK(vkCreatePipelineLayout(device, &plCi, 0, &pipelineLayout) == VK_SUCCESS);
    VkDescriptorPoolSize poolSizes[2] = {{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }};
    VkDescriptorPoolCreateInfo dpCi = {0};
    dpCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCi.maxSets = 1; dpCi.poolSizeCount = 2; dpCi.pPoolSizes = poolSizes;
    VkDescriptorPool descriptorPool = 0;
    CHECK(vkCreateDescriptorPool(device, &dpCi, 0, &descriptorPool) == VK_SUCCESS);
    VkDescriptorSetAllocateInfo dsAi = {0};
    dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAi.descriptorPool = descriptorPool; dsAi.descriptorSetCount = 1; dsAi.pSetLayouts = &descriptorLayout;
    VkDescriptorSet descriptorSet = 0;
    CHECK(vkAllocateDescriptorSets(device, &dsAi, &descriptorSet) == VK_SUCCESS);

    VkGraphicsPipelineCreateInfo gpci = {0};
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2; gpci.pStages = stages; gpci.basePipelineIndex = -1;
    gpci.renderPass = drawRenderPass; gpci.layout = pipelineLayout;
    VkPipeline pipeline = 0;
    CHECK(vkCreateGraphicsPipelines(device, 0, 1, &gpci, 0, &pipeline) == VK_SUCCESS);
    VkBufferCreateInfo vertexCi = {0};
    vertexCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexCi.size = 3u * sizeof(VkSoftVertex);
    vertexCi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkBuffer vertexBuffer = 0;
    CHECK(vkCreateBuffer(device, &vertexCi, 0, &vertexBuffer) == VK_SUCCESS);
    VkMemoryRequirements vertexReq;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &vertexReq);
    VkMemoryAllocateInfo vertexAi = {0};
    vertexAi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vertexAi.allocationSize = vertexReq.size;
    VkDeviceMemory vertexMem = 0;
    CHECK(vkAllocateMemory(device, &vertexAi, 0, &vertexMem) == VK_SUCCESS);
    CHECK(vkBindBufferMemory(device, vertexBuffer, vertexMem, 0) == VK_SUCCESS);
    VkDescriptorBufferInfo dbi = { vertexBuffer, 0, vertexCi.size };
    VkWriteDescriptorSet write = {0};
    write.dstSet = descriptorSet; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(device, 1, &write, 0, 0);
    CHECK(descriptorSet->values[0][0].buffer == vertexBuffer);
    CHECK(descriptorSet->values[0][0].range == vertexCi.size);
    VkDescriptorImageInfo texInfo = { texSampler, texView, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet texWrite = {0}; texWrite.dstSet = descriptorSet; texWrite.dstBinding = 1;
    texWrite.descriptorCount = 1; texWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; texWrite.pImageInfo = &texInfo;
    vkUpdateDescriptorSets(device, 1, &texWrite, 0, 0);
    CHECK(descriptorSet->values[1][0].imageView == texView && descriptorSet->values[1][0].sampler == texSampler);
    CHECK(vkMapMemory(device, vertexMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    VkSoftVertex shaderTri[3] = {va, vb, vc};
    vk_copy(mapped, shaderTri, sizeof(shaderTri));
    vkUnmapMemory(device, vertexMem);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
    CHECK(cmd->bound_sets[0] == descriptorSet);
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vertexOffset);
    VkClearColorValue drawClear = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo renderBegin = {0};
    renderBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderBegin.renderPass = drawRenderPass; renderBegin.framebuffer = drawFramebuffer;
    renderBegin.renderArea_width = 8; renderBegin.renderArea_height = 8;
    renderBegin.clearValueCount = 1; renderBegin.pClearValues = &drawClear;
    vkCmdBeginRenderPass(cmd, &renderBegin, 0);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
    CHECK(vkMapMemory(device, drawMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    /* The shader-driven draw must shade an interior pixel, preserve opaque
     * alpha, and leave pixels outside the triangle untouched. */
    {
        const uint8_t *pixels = (const uint8_t *)mapped;
        const unsigned inside = (2u*8u+2u)*4u;
        const unsigned outside = (7u*8u+7u)*4u;
        CHECK(pixels[inside] >= 40u);
        CHECK(pixels[inside+1u] == 0u && pixels[inside+2u] == 0u);
        CHECK(pixels[inside+3u] == 255u);
        CHECK(pixels[outside] == 0u && pixels[outside+1u] == 0u &&
              pixels[outside+2u] == 0u && pixels[outside+3u] == 0u);
    }
    vkUnmapMemory(device, drawMem);

    /* Depth attachment integration: create a D32 image, clear it to 1,
     * draw at z=0.5 with LESS/write enabled, and verify stored depth. */
    {
        VkImageCreateInfo depthCi = drawCi;
        depthCi.format = VK_FORMAT_D32_SFLOAT;
        depthCi.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthCi.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkImage depthImage = 0;
        CHECK(vkCreateImage(device, &depthCi, 0, &depthImage) == VK_SUCCESS);
        VkMemoryRequirements depthReq;
        vkGetImageMemoryRequirements(device, depthImage, &depthReq);
        VkMemoryAllocateInfo depthAi = {0};
        depthAi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depthAi.allocationSize = depthReq.size;
        VkDeviceMemory depthMem = 0;
        CHECK(vkAllocateMemory(device, &depthAi, 0, &depthMem) == VK_SUCCESS);
        CHECK(vkBindImageMemory(device, depthImage, depthMem, 0) == VK_SUCCESS);

        VkImageViewCreateInfo depthViewCi = {0};
        depthViewCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewCi.image = depthImage;
        depthViewCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewCi.format = VK_FORMAT_D32_SFLOAT;
        depthViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewCi.subresourceRange.levelCount = 1;
        depthViewCi.subresourceRange.layerCount = 1;
        VkImageView depthView = 0;
        CHECK(vkCreateImageView(device, &depthViewCi, 0, &depthView) == VK_SUCCESS);

        VkAttachmentDescription depthAttachments[2] = {attachment, {0}};
        depthAttachments[1].format = VK_FORMAT_D32_SFLOAT;
        depthAttachments[1].samples = 1;
        depthAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachments[1].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
        depthAttachments[1].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_GENERAL};
        VkSubpassDescription depthSubpass = subpass;
        depthSubpass.pDepthStencilAttachment = &depthRef;
        VkRenderPassCreateInfo depthRpCi = rpCi;
        depthRpCi.attachmentCount = 2;
        depthRpCi.pAttachments = depthAttachments;
        depthRpCi.pSubpasses = &depthSubpass;
        VkRenderPass depthRenderPass = 0;
        CHECK(vkCreateRenderPass(device, &depthRpCi, 0, &depthRenderPass) == VK_SUCCESS);

        VkImageView depthViews[2] = {drawView, depthView};
        VkFramebufferCreateInfo depthFbCi = fbCi;
        depthFbCi.renderPass = depthRenderPass;
        depthFbCi.attachmentCount = 2;
        depthFbCi.pAttachments = depthViews;
        VkFramebuffer depthFramebuffer = 0;
        CHECK(vkCreateFramebuffer(device, &depthFbCi, 0, &depthFramebuffer) == VK_SUCCESS);

        VkPipelineDepthStencilStateCreateInfo depthState = {0};
        depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthState.depthTestEnable = 1;
        depthState.depthWriteEnable = 1;
        depthState.depthCompareOp = 1; /* VK_COMPARE_OP_LESS */
        VkGraphicsPipelineCreateInfo depthGpci = gpci;
        depthGpci.renderPass = depthRenderPass;
        depthGpci.pDepthStencilState = &depthState;
        VkPipeline depthPipeline = 0;
        CHECK(vkCreateGraphicsPipelines(device, 0, 1, &depthGpci, 0, &depthPipeline) == VK_SUCCESS);

        CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
        CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vertexOffset);
        VkClearColorValue depthClears[2] = {{{0.0f, 0.0f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f, 0.0f}}};
        renderBegin.renderPass = depthRenderPass;
        renderBegin.framebuffer = depthFramebuffer;
        renderBegin.clearValueCount = 2;
        renderBegin.pClearValues = depthClears;
        vkCmdBeginRenderPass(cmd, &renderBegin, 0);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
        CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
        CHECK(vkMapMemory(device, depthMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
        CHECK(((float *)mapped)[2u * 8u + 2u] == 0.5f);
        vkUnmapMemory(device, depthMem);

        vkDestroyPipeline(device, depthPipeline, 0);
        vkDestroyFramebuffer(device, depthFramebuffer, 0);
        vkDestroyRenderPass(device, depthRenderPass, 0);
        vkDestroyImageView(device, depthView, 0);
        vkDestroyImage(device, depthImage, 0);
        vkFreeMemory(device, depthMem, 0);
    }
    vkDestroyPipeline(device, pipeline, 0);
    vkDestroyDescriptorPool(device, descriptorPool, 0);
    vkDestroyPipelineLayout(device, pipelineLayout, 0);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, 0);
    vkDestroyShaderModule(device, vertexShader, 0);
    vkDestroyShaderModule(device, fragmentShader, 0);
    vkDestroyBuffer(device, vertexBuffer, 0);
    vkFreeMemory(device, vertexMem, 0);
    vkDestroyFramebuffer(device, drawFramebuffer, 0);
    vkDestroyRenderPass(device, drawRenderPass, 0);
    vkDestroyImageView(device, drawView, 0);
    vkDestroyImage(device, drawImage, 0);
    vkFreeMemory(device, drawMem, 0);

    /* Whole-image Vulkan image-to-image copy stays inside transfer layouts. */
    VkImageCreateInfo dstci = imci;
    dstci.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkImage dstImage = 0;
    CHECK(vkCreateImage(device, &dstci, 0, &dstImage) == VK_SUCCESS);
    VkMemoryRequirements dstReq;
    vkGetImageMemoryRequirements(device, dstImage, &dstReq);
    VkMemoryAllocateInfo dstAi = {0};
    dstAi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    dstAi.allocationSize = dstReq.size; dstAi.memoryTypeIndex = 0;
    VkDeviceMemory dstMem = 0;
    CHECK(vkAllocateMemory(device, &dstAi, 0, &dstMem) == VK_SUCCESS);
    CHECK(vkBindImageMemory(device, dstImage, dstMem, 0) == VK_SUCCESS);
    VkImageCopy imageCopy = {0};
    imageCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCopy.srcSubresource.layerCount = 1;
    imageCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCopy.dstSubresource.layerCount = 1;
    imageCopy.extent.width = 4; imageCopy.extent.height = 4; imageCopy.extent.depth = 1;
    /* Distinct VkImage handles can alias the same allocation. Reject an
     * overlapping image copy atomically instead of recording a self-alias. */
    VkImage aliasImage = 0;
    CHECK(vkCreateImage(device, &dstci, 0, &aliasImage) == VK_SUCCESS);
    /* The earlier readback leaves imem mapped; Vulkan binding requires it unmapped. */
    vkUnmapMemory(device, imem);
    CHECK(vkBindImageMemory(device, aliasImage, imem, 0) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    vkCmdCopyImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   aliasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
    CHECK(((VkCmdObj *)cmd)->count == 0);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
    vkDestroyImage(device, aliasImage, 0);

    CHECK(vkBeginCommandBuffer(cmd, &bi) == VK_SUCCESS);
    vkCmdCopyImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
    CHECK(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    CHECK(vkQueueSubmit(queue, 1, &si, 0) == VK_SUCCESS);
    CHECK(vkMapMemory(device, dstMem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
    CHECK(((uint8_t *)mapped)[0] == 255 && ((uint8_t *)mapped)[1] == 0 &&
          ((uint8_t *)mapped)[2] == 0 && ((uint8_t *)mapped)[3] == 255);
    vkUnmapMemory(device, dstMem);
    vkDestroyImage(device, dstImage, 0);
    vkFreeMemory(device, dstMem, 0);
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
