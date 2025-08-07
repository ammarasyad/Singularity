#ifndef RAY_TRACING_H
#define RAY_TRACING_H

#include "graphics/vk/vk_common.h"
#include <vector>
#include "radiance_cascades.h"
#include "engine/objects/render_object.h"
#include "graphics/vk/memory/vk_memory.h"

class VkMemoryManager;
class VkRenderer;

class RayTracing
{
public:
    enum LightType : uint32_t
    {
        Directional,
        Point
    };

    RayTracing() = default;
    void Init(const VkRenderer *renderer, VkDevice device, VkPhysicalDevice physicalDevice, VkMemoryManager &memoryManager, const VkExtent2D &swapChainExtent);
    void Destroy(VkDevice device, VkMemoryManager &memoryManager);
    void UpdateDescriptorSets(VkDevice device, uint32_t frameIndex);
    void AddLight(glm::vec4 lightPosition, glm::vec4 lightColor, LightType lightType);
    void ResetSceneData();
    void UpdateBuffers(const VkMemoryManager &memoryManager);
    void TraceRay(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        const glm::mat4 & viewInverse,
        const glm::mat4 & projectionInverse,
        const glm::vec3 & cameraPosition,
        const VkExtent2D &swapChainExtent
    );
    void TransitionAccumulatedImages(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 oldStage, VkPipelineStageFlags2 newStage);
    void AccumulateRadiance(VkCommandBuffer commandBuffer, uint32_t updateFrameIndex, const VkExtent2D &swapChainExtent);
    void BuildBLAS(VkRenderer *, const std::vector<VkRenderObject> &);
    void BuildTLAS(VkRenderer *, const std::vector<VkRenderObject> &);

    VulkanImage radianceImage;
    std::array<VulkanImage, 2> accumulatedImages;
    std::vector<VkDescriptorImageInfo> textureInfo;
private:
    inline void CompactBLAS(VkRenderer *, const std::vector<VkDeviceSize> &compactedSizes, const std::vector<VkDeviceSize> &compactedOffsets);
    static constexpr size_t maxLights = 4;
    struct LightData
    {
        uint32_t lightCount;
        alignas(16) struct
        {
            glm::vec4 lightPosition;
            glm::vec4 lightColor;
        } lights[maxLights];
    } lightData;

    VkDeviceAddress getBlasDeviceAddress(VkDevice, uint32_t);

    DescriptorAllocator allocator;
    DescriptorWriter writer;
    VkDescriptorSetLayout rayTracingDescriptorSetLayout;
    std::array<VkDescriptorSet, 2> rayTracingDescriptorSets;
    VkPipelineLayout rayTracingPipelineLayout;
    VkPipeline rayTracingPipeline;

    VkDescriptorSetLayout accumulatedDescriptorSetLayout;
    std::array<VkDescriptorSet, 2> accumulatedDescriptorSets;
    VkPipelineLayout accumulatedPipelineLayout;
    VkPipeline accumulatedPipeline;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;

    VkStridedDeviceAddressRegionKHR rgenSBT{};
    VkStridedDeviceAddressRegionKHR missSBT{};
    VkStridedDeviceAddressRegionKHR hitSBT{};
    // VkStridedDeviceAddressRegionKHR shadowSBT{};

    uint32_t totalPrimitiveCount;
    struct AccumulatedComputePushConstants
    {
        uint32_t frameIndex;
        uint32_t resetAccumulation{0};
    } computePushConstants;
    // bool radianceCascadesEnabled{false};

    VulkanBuffer sbtBuffer;
    VulkanBuffer blasBuffer;
    VulkanBuffer tlasBuffer;
    // VulkanBuffer tlasScratchBuffer;
    // VulkanBuffer tlasInstanceBuffer;

    VulkanBuffer meshAddressesBuffer;
    VulkanBuffer hitUniformBuffer;

    VkAccelerationStructureKHR tlas;
    std::vector<VkAccelerationStructureKHR> blas;
    std::vector<VulkanBuffer> blasScratchBuffers;

    RadianceCascades radianceCascades;

    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
};

#endif
