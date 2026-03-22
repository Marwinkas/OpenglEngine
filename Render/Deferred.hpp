#pragma once

#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/FrameInfo.hpp"

// std
#include <memory>
#include <vector>

namespace burnhope {

    class DeferredLightingSystem {
    public:
        // Передаем массив лэйаутов для всех сетов, которые ждет шейдер (Global, GBuffer, Shadows и т.д.)
        DeferredLightingSystem(BurnhopeDevice& device, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts);
        ~DeferredLightingSystem();

        DeferredLightingSystem(const DeferredLightingSystem&) = delete;
        DeferredLightingSystem& operator=(const DeferredLightingSystem&) = delete;

        // Метод, который запустит вычисления
        void computeLighting(VkCommandBuffer commandBuffer, const std::vector<VkDescriptorSet>& descriptorSets, uint32_t width, uint32_t height);

    private:
        void createPipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts);
        void createPipeline();

        BurnhopeDevice& lveDevice;
        std::unique_ptr<BurnhopePipeline> lvePipeline;
        VkPipelineLayout pipelineLayout;
    };

} // namespace burnhope