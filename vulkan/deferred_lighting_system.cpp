#include "deferred_lighting_system.hpp"

// std
#include <stdexcept>
#include <cassert>

namespace burnhope {

    DeferredLightingSystem::DeferredLightingSystem(
        BurnhopeDevice& device, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts)
        : lveDevice{ device } {
        createPipelineLayout(descriptorSetLayouts);
        createPipeline();
    }

    DeferredLightingSystem::~DeferredLightingSystem() {
        vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
    }

    void DeferredLightingSystem::createPipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts) {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 0; // Для света push constants пока не нужны
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline layout!");
        }
    }

    void DeferredLightingSystem::createPipeline() {
        assert(pipelineLayout != nullptr && "Cannot create compute pipeline before pipeline layout");

        // Указываем путь к твоему собранному Compute-шейдеру
        lvePipeline = std::make_unique<BurnhopePipeline>(
            lveDevice,
            "shaders/lighting.comp.spv",
            pipelineLayout);
    }

    void DeferredLightingSystem::computeLighting(
        VkCommandBuffer commandBuffer, 
        const std::vector<VkDescriptorSet>& descriptorSets, 
        uint32_t width, uint32_t height) {

        // 1. Привязываем конвейер вычислений
        lvePipeline->bindCompute(commandBuffer);

        // 2. Привязываем все наши Сеты (UBO, GBuffer, Output Image и т.д.)
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0,
            nullptr);

        // 3. Считаем количество рабочих групп (Workgroups)
        // В шейдере мы задали layout(local_size_x = 16, local_size_y = 16)
        uint32_t groupCountX = (width + 15) / 16;
        uint32_t groupCountY = (height + 15) / 16;

        // 4. ЗАПУСК! (Магия начинается здесь)
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
    }

} // namespace burnhope