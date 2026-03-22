#include "ShadowRender.hpp"
#include <stdexcept>
#include <iostream>

namespace burnhope {

ShadowRenderSystem::ShadowRenderSystem(BurnhopeDevice& device,
    VkRenderPass shadowRenderPass,
    VkDescriptorSetLayout globalSetLayout)
    : lveDevice(device)
{
    createPipelineLayout(globalSetLayout);
    createPipeline(shadowRenderPass);
}

ShadowRenderSystem::~ShadowRenderSystem() {
    vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}

void ShadowRenderSystem::createPipelineLayout(VkDescriptorSetLayout objectSetLayout) {
    // Push constant: только lightSpaceMatrix (64 байта)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(ShadowPushConstant);

    // Set 0: globalUbo (нужен для доступа к objectBuffer если он там)
    // Set 1: objectStorageSet (objectBuffer с modelMatrix)
    std::vector<VkDescriptorSetLayout> layouts = {
        objectSetLayout
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = (uint32_t)layouts.size();
    layoutInfo.pSetLayouts            = layouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;

    if (vkCreatePipelineLayout(lveDevice.device(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow pipeline layout!");
}

void ShadowRenderSystem::createPipeline(VkRenderPass renderPass) {
    PipelineConfigInfo config{};
    BurnhopePipeline::defaultPipelineConfigInfo(config);

    // Shadow pipeline: только depth, без цвета
    config.renderPass     = renderPass;
    config.pipelineLayout = pipelineLayout;

    // Нет color attachments
    config.colorBlendInfo.attachmentCount = 0;
    config.colorBlendInfo.pAttachments    = nullptr;

    // Depth bias для борьбы с shadow acne
    config.rasterizationInfo.depthBiasEnable         = VK_TRUE;
    config.rasterizationInfo.depthBiasConstantFactor = 1.25f;
    config.rasterizationInfo.depthBiasSlopeFactor    = 1.75f;

    // Culling: front face culling убирает peter-panning
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    pipeline = std::make_unique<BurnhopePipeline>(
        lveDevice,
        "shaders/shadow.vert.spv",
        "shaders/shadow.frag.spv",
        config);
}
void ShadowRenderSystem::renderShadow(
    VkCommandBuffer commandBuffer,
    const glm::mat4& lightSpaceMatrix,
    entt::registry& registry,
    VkDescriptorSet objectStorageSet)
{
    pipeline->bind(commandBuffer);

    // 1. Проверяем сет, как мы делали это в геометрии, чтобы избежать крашей
    if (objectStorageSet == VK_NULL_HANDLE) {
        std::cerr << "Warning: Cannot render shadows, storage set is not initialized!" << std::endl;
        return;
    }

    // 2. Биндим сет с нашими матрицами объектов (SSBO).
    // ВАЖНО: В GeometryRenderSystem твой storageSet шел под индексом 1 (вторым в списке).
    // Если в шейдере теней у тебя тоже написано layout(set = 1, binding = ...), 
    // то здесь нужно передать 1 вместо 0! Я поставил 1 для безопасности.
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0, // <--- ИЗМЕНИ ЭТО НА 0 (firstSet)
        1, // descriptorSetCount
        &objectStorageSet,
        0, nullptr);

    // 3. Отправляем матрицу света. 
    // Она общая для всех объектов в кадре, поэтому делаем это один раз ДО цикла.
    ShadowPushConstant push{};
    push.lightSpaceMatrix = lightSpaceMatrix;
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(ShadowPushConstant),
        &push);

    // 4. Проходимся по всем объектам сцены
    auto view = registry.view<TransformComponent, MeshComponent>();
    uint32_t instanceIndex = 0; // Тот самый индекс для поиска в SSBO

    for (auto [entity, transformComp, meshComp] : view.each()) {
        if (!meshComp.model || !meshComp.isVisible) continue;

        meshComp.model->bind(commandBuffer);

        const auto& subMeshes = meshComp.model->getSubMeshes();
        for (const auto& sub : subMeshes) {
            // Передаем instanceIndex! 
            // Теперь шейдер теней вытащит правильную modelMatrix из массива.
            vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, 0, instanceIndex);
            instanceIndex++;
        }

    }
}

} // namespace burnhope
