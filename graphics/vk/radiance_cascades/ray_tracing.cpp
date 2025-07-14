#include "ray_tracing.h"
#include "common/file.h"
#include "engine/camera.h"
#include "graphics/vk_renderer.h"

constexpr size_t blasAlignment = 256;

struct RaygenPushConstants
{
    glm::mat4 viewInverse;
    glm::mat4 projectionInverse;
};

struct MeshData
{
    VkDeviceAddress vertexBufferAddress;
    VkDeviceAddress indexBufferAddress;
    uint32_t textureIndex;
    uint32_t firstIndex;
};

struct HitPushConstants
{
    glm::vec4 lightPosition;
    glm::vec4 lightColor;
};

static VkBufferDeviceAddressInfo bufferDeviceAddressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};

inline VkDeviceAddress GetBufferAddress(VkDevice device, VkBuffer buffer)
{
    bufferDeviceAddressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &bufferDeviceAddressInfo);
}

void RayTracing::Init(const VkRenderer *renderer, VkDevice device, VkPhysicalDevice physicalDevice,
                      VkMemoryManager &memoryManager, const VkExtent2D &swapChainExtent)
{
#pragma region Function Pointer Initialization
    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(
        device, "vkCreateAccelerationStructureKHR"));
    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(
        device, "vkCmdBuildAccelerationStructuresKHR"));
    vkCmdWriteAccelerationStructuresPropertiesKHR = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(
        vkGetDeviceProcAddr(device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
    vkCmdCopyAccelerationStructureKHR = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(vkGetDeviceProcAddr(
        device, "vkCmdCopyAccelerationStructureKHR"));
    vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));
    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(
        device, "vkDestroyAccelerationStructureKHR"));
#pragma endregion
#pragma region Descriptor Set Layout Creation
    {
        static constexpr DescriptorAllocator::PoolSizeRatio sizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100}
        };

        allocator.InitPool(device, 1, sizes);

        DescriptorLayoutBuilder builder;
        // builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                           VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        builder.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 100);
        rayTracingDescriptorSetLayout = builder.Build(device, VK_NULL_HANDLE, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);;

        const VkDescriptorSetLayout layouts[] = {rayTracingDescriptorSetLayout};
        rayTracingDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            rayTracingDescriptorSets[i] = allocator.Allocate(device, layouts);
    }
#pragma endregion
#pragma region Pipeline Layout
    {
        constexpr VkPushConstantRange pushConstantRanges[2]
        {
            {
                VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                0,
                sizeof(RaygenPushConstants)
            },
            {
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                sizeof(RaygenPushConstants),
                sizeof(HitPushConstants)
            }
        };

        const VkPipelineLayoutCreateInfo layoutCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            1,
            &rayTracingDescriptorSetLayout,
            2,
            pushConstantRanges
        };

        VK_CHECK(vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &rayTracingPipelineLayout));
    }
#pragma endregion
#pragma region Shader Modules and Pipeline Creation
    {
        const auto raygenShaderCode = ReadFile<uint32_t>("shaders/raygen.rgen.spv");
        const auto missShaderCode = ReadFile<uint32_t>("shaders/miss.rmiss.spv");
        const auto shadowMissShaderCode = ReadFile<uint32_t>("shaders/rtShadow.rmiss.spv");
        const auto closestHitShaderCode = ReadFile<uint32_t>("shaders/closesthit.rchit.spv");

        VkShaderModule shaderModules[4]{};
        VkShaderModuleCreateInfo createInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            VK_NULL_HANDLE,
            0,
            raygenShaderCode.size(),
            raygenShaderCode.data()
        };

        VK_CHECK(vkCreateShaderModule(device, &createInfo, VK_NULL_HANDLE, &shaderModules[0]));

        createInfo.codeSize = missShaderCode.size();
        createInfo.pCode = missShaderCode.data();

        VK_CHECK(vkCreateShaderModule(device, &createInfo, VK_NULL_HANDLE, &shaderModules[1]));

        createInfo.codeSize = shadowMissShaderCode.size();
        createInfo.pCode = shadowMissShaderCode.data();

        VK_CHECK(vkCreateShaderModule(device, &createInfo, VK_NULL_HANDLE, &shaderModules[2]));

        createInfo.codeSize = closestHitShaderCode.size();
        createInfo.pCode = closestHitShaderCode.data();

        VK_CHECK(vkCreateShaderModule(device, &createInfo, VK_NULL_HANDLE, &shaderModules[3]));

        VkPipelineShaderStageCreateInfo shaderStages[4]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        shaderStages[0].module = shaderModules[0];
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        shaderStages[1].module = shaderModules[1];
        shaderStages[1].pName = "main";

        shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        shaderStages[2].module = shaderModules[2];
        shaderStages[2].pName = "main";

        shaderStages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[3].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        shaderStages[3].module = shaderModules[3];
        shaderStages[3].pName = "main";

        VkRayTracingShaderGroupCreateInfoKHR groups[4]{};
        groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0;
        groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
        groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
        groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

        groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1;
        groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
        groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
        groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

        groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[2].generalShader = 2;
        groups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
        groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
        groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

        groups[3].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[3].generalShader = VK_SHADER_UNUSED_KHR;
        groups[3].closestHitShader = 3;
        groups[3].anyHitShader = VK_SHADER_UNUSED_KHR;
        groups[3].intersectionShader = VK_SHADER_UNUSED_KHR;

        VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo{
            VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
            VK_NULL_HANDLE,
            0,
            4,
            shaderStages,
            4,
            groups,
            2,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            rayTracingPipelineLayout
        };

        const auto vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
            vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));
        VK_CHECK(
            vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &
                rayTracingPipeline));

        vkDestroyShaderModule(device, shaderModules[0], nullptr);
        vkDestroyShaderModule(device, shaderModules[1], nullptr);
        vkDestroyShaderModule(device, shaderModules[2], nullptr);
        vkDestroyShaderModule(device, shaderModules[3], nullptr);
    }
#pragma endregion
#pragma region Shader Binding Table
    {
        rayTracingPipelineProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 properties2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &rayTracingPipelineProperties
        };
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

        printf("Ray Tracing Pipeline Properties:\n"
               "\tShader Group Handle Size: %u bytes\n"
               "\tMax Recursion Depth: %u\n"
               "\tShader Group Base Alignment: %u bytes\n"
               "\tShader Group Handle Alignment: %u bytes\n",
               rayTracingPipelineProperties.shaderGroupHandleSize,
               rayTracingPipelineProperties.maxRayRecursionDepth,
               rayTracingPipelineProperties.shaderGroupBaseAlignment,
               rayTracingPipelineProperties.shaderGroupHandleAlignment);

        const auto handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
        const auto baseAlignment = rayTracingPipelineProperties.shaderGroupBaseAlignment;
        const auto handleAlignment = rayTracingPipelineProperties.shaderGroupHandleAlignment;
        // const uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

        // TODO: Change this accordingly
        constexpr uint32_t raygenCount = 1;
        constexpr uint32_t missCount = 2; // One for the main miss shader and one for the shadow miss shader
        constexpr uint32_t hitCount = 1;
        const uint32_t totalGroupBytes = (raygenCount + missCount + hitCount + 2) * handleSize;

        const auto vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
            vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));
        std::vector<uint8_t> shaderHandles(totalGroupBytes);
        VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(device,
            rayTracingPipeline,
            0,
            raygenCount + missCount + hitCount,
            totalGroupBytes,
            shaderHandles.data()));

        sbtBuffer = memoryManager.createUnmanagedBuffer({
            totalGroupBytes,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        });

        const auto alignUp = [&](const VkDeviceAddress address, const uint32_t alignment)
        {
            return (address + alignment - 1) & ~(alignment - 1);
        };

        const auto handleSizeAligned = alignUp(handleSize, handleAlignment);

        rgenSBT.deviceAddress = GetBufferAddress(device, sbtBuffer.buffer);
        rgenSBT.stride = rgenSBT.size = alignUp(handleSizeAligned, baseAlignment);

        missSBT.deviceAddress = rgenSBT.deviceAddress + rgenSBT.size;
        missSBT.stride = handleSizeAligned;
        missSBT.size = alignUp(missCount * handleSizeAligned, baseAlignment);

        hitSBT.deviceAddress = missSBT.deviceAddress + missSBT.size;
        hitSBT.stride = handleSizeAligned;
        hitSBT.size = alignUp(hitCount * handleSizeAligned, baseAlignment);

        {
            void *data;
            memoryManager.mapBuffer(sbtBuffer, &data);
            auto *sbtData = static_cast<uint8_t *>(data);

            auto getHandle = [shaderHandles, handleSize](const uint32_t handleIndex)
            {
                return shaderHandles.data() + handleIndex * handleSize;
            };

            uint32_t handleIndex = 0;
            // Raygen
            memcpy(sbtData, getHandle(handleIndex++), handleSize);
            sbtData += rgenSBT.size;
            // Miss
            for (uint32_t i = 0; i < missCount; i++)
            {
                memcpy(sbtData, getHandle(handleIndex++), handleSize);
                sbtData += missSBT.stride;
            }
            // Hit
            memcpy(sbtData, getHandle(handleIndex++), handleSize);

            memoryManager.unmapBuffer(sbtBuffer);
        }
    }
#pragma endregion
#pragma region Miscellaneous
    ImageViewCreateInfo radianceImageCreateInfo{
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    radianceImage = memoryManager.createUnmanagedImage({
        0,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        {swapChainExtent.width, swapChainExtent.height, 1},
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        0,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        false,
        &radianceImageCreateInfo
    });

    renderer->ImmediateSubmit([&](auto commandBuffer)
    {
        TransitionImage(commandBuffer, radianceImage, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_NONE,
                        VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL);
    });

    constexpr VkSamplerCreateInfo samplerCreateInfo{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_FILTER_NEAREST,
        VK_FILTER_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        0.0f,
        VK_FALSE,
        1.0f,
        VK_FALSE,
        VK_COMPARE_OP_NEVER,
        0.0f,
        1.0f,
        VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        VK_FALSE
    };

    vkCreateSampler(device, &samplerCreateInfo, VK_NULL_HANDLE, &radianceImage.sampler);
#pragma endregion
}

void RayTracing::Destroy(VkDevice device, VkMemoryManager &memoryManager)
{
    memoryManager.destroyBuffer(blasBuffer, false);
    for (const auto &buffer: blasScratchBuffers)
    {
        memoryManager.destroyBuffer(buffer, false);
    }

    for (const auto &b: blas)
    {
        vkDestroyAccelerationStructureKHR(device, b, nullptr);
    }

    vkDestroyAccelerationStructureKHR(device, tlas, nullptr);

    memoryManager.destroyBuffer(sbtBuffer, false);
    memoryManager.destroyBuffer(tlasBuffer, false);
    // memoryManager.destroyBuffer(tlasScratchBuffer, false);
    // memoryManager.destroyBuffer(tlasInstanceBuffer, false);

    memoryManager.destroyImage(radianceImage, false);
    memoryManager.destroyBuffer(meshAddressesBuffer, false);

    allocator.Destroy(device);
    vkDestroyDescriptorSetLayout(device, rayTracingDescriptorSetLayout, nullptr);
    vkDestroyPipelineLayout(device, rayTracingPipelineLayout, nullptr);
    vkDestroyPipeline(device, rayTracingPipeline, nullptr);
}

void RayTracing::UpdateDescriptorSets(VkDevice device, const uint32_t frameIndex)
{
    writer.Clear();
    writer.WriteAccelerationStructure(0, &tlas);
    // TODO: Replace this with radiance cascades
    writer.WriteImage(1, radianceImage.imageView, radianceImage.sampler, VK_IMAGE_LAYOUT_GENERAL,
                      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.WriteBuffer(2, meshAddressesBuffer.buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.WriteImages(3, textureInfo);
    writer.UpdateSet(device, rayTracingDescriptorSets[frameIndex]);
}

void RayTracing::TraceRay(VkCommandBuffer commandBuffer, const uint32_t frameIndex, const glm::mat4 & viewInverse,
                          const glm::mat4 & projectionInverse, const glm::vec4 & lightPosition,
                          const glm::vec4 & lightColor,
                          const VkExtent2D &swapChainExtent)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipelineLayout, 0, 1,
                            &rayTracingDescriptorSets[frameIndex], 0, nullptr);

    const RaygenPushConstants raygenPushConstants{
        viewInverse,
        projectionInverse
    };

    const HitPushConstants hitPushConstants{
        lightPosition,
        lightColor,
    };

    vkCmdPushConstants(commandBuffer, rayTracingPipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0,
                       sizeof(RaygenPushConstants), &raygenPushConstants);
    vkCmdPushConstants(commandBuffer, rayTracingPipelineLayout, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                       sizeof(RaygenPushConstants), sizeof(HitPushConstants), &hitPushConstants);
    static constexpr VkStridedDeviceAddressRegionKHR callableSBT{};
    vkCmdTraceRaysKHR(commandBuffer,
                      &rgenSBT,
                      &missSBT,
                      &hitSBT,
                      &callableSBT, // Callable SBT
                      swapChainExtent.width, swapChainExtent.height, 1); // Width, Height, Depth
}

void RayTracing::BuildBLAS(VkRenderer *renderer, const std::vector<VkRenderObject> &renderObjects)
{
    const auto device = renderer->device;
    auto &memoryManager = renderer->memoryManager;

    std::vector<uint32_t> primitiveCounts(renderObjects.size());
    std::vector<VkAccelerationStructureGeometryKHR> geometries(renderObjects.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeometryInfos(renderObjects.size());

    std::vector<size_t> asOffsets(renderObjects.size());
    std::vector<size_t> asSizes(renderObjects.size());
    std::vector<size_t> scratchSizes(renderObjects.size());

    constexpr size_t defaultScratchSize = 32 * 1024 * 1024;

    size_t totalBlasSize = 0;
    size_t maxScratchSize = 0;

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    for (uint32_t i = 0; i < renderObjects.size(); ++i)
    {
        auto &geo = geometries[i];
        auto &buildInfo = buildGeometryInfos[i];
        const auto &mesh = renderObjects[i];

        primitiveCounts[i] = renderObjects[i].indexCount / 3;

        geo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        geo.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geo.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geo.geometry.triangles.vertexData.deviceAddress = mesh.vertexBufferAddress;
        geo.geometry.triangles.vertexStride = sizeof(VkVertex);
        geo.geometry.triangles.maxVertex = mesh.vertexCount - 1;
        geo.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geo.geometry.triangles.indexData.deviceAddress = mesh.indexBufferAddress + mesh.firstIndex * sizeof(uint32_t);

        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                          VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geo;

        vkGetAccelerationStructureBuildSizesKHR(device,
                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &buildInfo,
                                                &primitiveCounts[i],
                                                &buildSizesInfo);

        asOffsets[i] = totalBlasSize;
        asSizes[i] = buildSizesInfo.accelerationStructureSize;
        scratchSizes[i] = buildSizesInfo.buildScratchSize;

        totalBlasSize = (totalBlasSize + buildSizesInfo.accelerationStructureSize + blasAlignment - 1) & ~(
                            blasAlignment - 1);
        totalPrimitiveCount += primitiveCounts[i];
        maxScratchSize = std::max(maxScratchSize, scratchSizes[i]);
    }

    blasBuffer = memoryManager.createUnmanagedBuffer({
        totalBlasSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    });

    const size_t scratchBufferSize = std::max(maxScratchSize, defaultScratchSize);
    const auto scratchBuffer = memoryManager.createUnmanagedBuffer({
        scratchBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    });

    printf("BLAS Build: \n"
           "\tTotal BLAS Size: %zu bytes\n"
           "\tTotal Primitive Count: %u\n"
           "\tScratch Buffer Size: %zu bytes\n",
           totalBlasSize, totalPrimitiveCount, scratchBufferSize);

    auto scratchAddress = GetBufferAddress(device, scratchBuffer.buffer);

    blas.resize(renderObjects.size());

    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges(renderObjects.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> buildRangesPtrs(renderObjects.size());

    VkAccelerationStructureCreateInfoKHR buildInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = blasBuffer.buffer,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };

    for (uint32_t i = 0; i < renderObjects.size(); ++i)
    {
        buildInfo.offset = asOffsets[i];
        buildInfo.size = asSizes[i];

        VK_CHECK(vkCreateAccelerationStructureKHR(device, &buildInfo, VK_NULL_HANDLE, &blas[i]));
    }

    VkQueryPoolCreateInfo queryPoolInfo{
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        VK_NULL_HANDLE,
        0,
        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
        static_cast<uint32_t>(renderObjects.size())
    };

    VkQueryPool queryPool{};
    VK_CHECK(vkCreateQueryPool(device, &queryPoolInfo, VK_NULL_HANDLE, &queryPool));
    vkResetQueryPool(device, queryPool, 0, blas.size());

    size_t offset = 0;
    size_t scratchSize = 0;
    for (size_t i = 0; i < renderObjects.size(); i++)
    {
        scratchSize = scratchSizes[i];
        buildGeometryInfos[i].scratchData.deviceAddress = scratchAddress + offset;
        buildGeometryInfos[i].dstAccelerationStructure = blas[i];
        buildRanges[i].primitiveCount = primitiveCounts[i];
        buildRangesPtrs[i] = &buildRanges[i];

        offset = (offset + scratchSize + blasAlignment - 1) & ~(blasAlignment - 1);
    }

    renderer->ImmediateSubmit([&](const auto &commandBuffer)
    {
        for (size_t i = 0; i < renderObjects.size(); ++i)
        {
            VkBufferMemoryBarrier2 scratchBarrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                VK_NULL_HANDLE,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                scratchBuffer.buffer,
                0,
                scratchBufferSize
            };

            VkDependencyInfo dependencyInfo{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &scratchBarrier
            };

            vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeometryInfos[i], &buildRangesPtrs[i]);
            vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        }

        vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, blas.size(), blas.data(),
                                                      VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                      queryPool, 0);
    });

    compactedSizes.resize(blas.size());
    VK_CHECK(vkGetQueryPoolResults(device, queryPool, 0,
        static_cast<uint32_t>(blas.size()),
        compactedSizes.size() * sizeof(VkDeviceSize),
        compactedSizes.data(),
        sizeof(VkDeviceSize), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

    vkDestroyQueryPool(device, queryPool, nullptr);

    memoryManager.destroyBuffer(scratchBuffer, false);

    std::vector<MeshData> meshAddresses(renderObjects.size());
    for (size_t i = 0; i < renderObjects.size(); ++i)
    {
        meshAddresses[i].vertexBufferAddress = renderObjects[i].vertexBufferAddress;
        meshAddresses[i].indexBufferAddress = renderObjects[i].indexBufferAddress;
        meshAddresses[i].textureIndex = renderObjects[i].materialInstance->textureIndex;
        meshAddresses[i].firstIndex = renderObjects[i].firstIndex;
    }

    meshAddressesBuffer = memoryManager.createUnmanagedBuffer({renderObjects.size() * sizeof(MeshData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO});

    void *data;
    memoryManager.mapBuffer(meshAddressesBuffer, &data);
    memcpy(data, meshAddresses.data(), meshAddresses.size() * sizeof(MeshData));
    memoryManager.unmapBuffer(meshAddressesBuffer);
}

void RayTracing::CompactBLAS(VkRenderer *renderer)
{
    const auto device = renderer->device;
    VkAccelerationStructureCreateInfoKHR createInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        nullptr,
        0,
        VK_NULL_HANDLE,
        0,
        0,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };

    size_t totalCompactedSize = 0;
    std::vector<VkDeviceSize> compactedOffsets(blas.size());

    for (auto &size: compactedOffsets)
    {
        size = (totalCompactedSize + blasAlignment - 1) & ~(blasAlignment - 1);
        totalCompactedSize += size;
    }

    printf("BLAS Compaction: \n"
           "\tTotal Compacted Size: %zu bytes\n", totalCompactedSize);

    const auto compactedBuffer = renderer->memoryManager.createUnmanagedBuffer({
        totalCompactedSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    });

    std::vector<VkAccelerationStructureKHR> compactedBlas(blas.size());

    renderer->ImmediateSubmit([&](const auto &commandBuffer)
    {
        for (uint32_t i = 0; i < blas.size(); ++i)
        {
            createInfo.buffer = compactedBuffer.buffer;
            createInfo.offset = compactedOffsets[i];
            createInfo.size = compactedSizes[i];

            VK_CHECK(vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &compactedBlas[i]));

            VkCopyAccelerationStructureInfoKHR copyInfo{
                VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
                VK_NULL_HANDLE,
                blas[i],
                compactedBlas[i],
                VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR
            };

            vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
        }
    });

    for (uint32_t i = 0; i < blas.size(); ++i)
    {
        vkDestroyAccelerationStructureKHR(device, blas[i], nullptr);
        blas[i] = compactedBlas[i];
    }

    renderer->memoryManager.destroyBuffer(blasBuffer);
    blasBuffer = compactedBuffer;
}

inline VkTransformMatrixKHR toTransformMatrix(const glm::mat4 &transform)
{
    // VkTransformMatrixKHR uses a row-major memory layout, while glm::mat4 uses a column-major memory layout.
    const auto transpose = glm::transpose(transform);
    VkTransformMatrixKHR result;
    memcpy(&result, &transpose, sizeof(VkTransformMatrixKHR));
    return result;
}

inline VkDeviceAddress RayTracing::getBlasDeviceAddress(VkDevice device, const uint32_t id)
{
    static VkAccelerationStructureDeviceAddressInfoKHR deviceAddressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR
    };

    deviceAddressInfo.accelerationStructure = blas[id];
    return vkGetAccelerationStructureDeviceAddressKHR(device, &deviceAddressInfo);
}

void RayTracing::BuildTLAS(VkRenderer *renderer,
                            const std::vector<VkRenderObject> &renderObjects)
{
    const auto device = renderer->device;
    auto &memoryManager = renderer->memoryManager;
    const auto tlasInstanceBuffer = memoryManager.createUnmanagedBuffer({
        sizeof(VkAccelerationStructureInstanceKHR) * renderObjects.size(),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    });

    VkAccelerationStructureGeometryKHR geo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        VK_NULL_HANDLE,
        VK_GEOMETRY_TYPE_INSTANCES_KHR,
        {
            .instances = {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                .data = {
                    .deviceAddress = GetBufferAddress(device, tlasInstanceBuffer.buffer)
                }
            }
        }
    };

    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfos{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        VK_NULL_HANDLE,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        1,
        &geo,
    };

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    vkGetAccelerationStructureBuildSizesKHR(device,
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &buildGeometryInfos,
                                            &totalPrimitiveCount,
                                            &buildSizesInfo);

    printf("TLAS Creation: \n"
           "\tTotal TLAS Size: %zu bytes\n"
           "\tTotal Scratch Size: %zu bytes\n"
           "\tTotal Update Scratch Size: %zu bytes\n",
           buildSizesInfo.accelerationStructureSize, buildSizesInfo.buildScratchSize, buildSizesInfo.updateScratchSize);

    tlasBuffer = memoryManager.createUnmanagedBuffer({
        buildSizesInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        0,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    });

    const auto tlasScratchBuffer = memoryManager.createUnmanagedBuffer({
        buildSizesInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        0,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    });

    VkAccelerationStructureCreateInfoKHR accelInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        VK_NULL_HANDLE,
        0,
        tlasBuffer.buffer,
        0,
        buildSizesInfo.accelerationStructureSize,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
    };

    VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelInfo, VK_NULL_HANDLE, &tlas));

    buildGeometryInfos.srcAccelerationStructure = tlas;
    buildGeometryInfos.dstAccelerationStructure = tlas;
    buildGeometryInfos.scratchData.deviceAddress = GetBufferAddress(device, tlasScratchBuffer.buffer);

    std::vector<VkAccelerationStructureInstanceKHR> instances(renderObjects.size());
    for (uint32_t i = 0; i < renderObjects.size(); ++i)
    {
        instances[i] = {
            toTransformMatrix(renderObjects[i].transform),
            i,
            0xFF,
            0,
            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            getBlasDeviceAddress(device, i)
        };
    }

    {
        void *data;
        memoryManager.mapBuffer(tlasInstanceBuffer, &data);
        memcpy(data, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
        memoryManager.unmapBuffer(tlasInstanceBuffer);
    }

    const VkAccelerationStructureBuildRangeInfoKHR buildRangeInfos{
        static_cast<uint32_t>(renderObjects.size())
    };
    const VkAccelerationStructureBuildRangeInfoKHR *buildRangeInfosPtr = &buildRangeInfos;

    renderer->ImmediateSubmit([&](const auto &commandBuffer)
    {
        VkBufferMemoryBarrier2 barrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            VK_NULL_HANDLE,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            tlasBuffer.buffer,
            0,
            VK_WHOLE_SIZE
        };

        VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeometryInfos, &buildRangeInfosPtr);
    });

    memoryManager.destroyBuffer(tlasInstanceBuffer, false);
    memoryManager.destroyBuffer(tlasScratchBuffer, false);
}