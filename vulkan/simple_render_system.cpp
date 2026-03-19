#include "simple_render_system.hpp"
#include <iostream>

namespace burnhope {

    struct GeometryPushConstants {
        // В Deferred нам часто хватает только ID объекта, 
        // но давай оставим матрицы для совместимости
        glm::mat4 modelMatrix{ 1.f };
        glm::mat4 normalMatrix{ 1.f };
    };

    GeometryRenderSystem::GeometryRenderSystem(
        BurnhopeDevice& device, VkRenderPass gBufferRenderPass, VkDescriptorSetLayout globalSetLayout)
        : lveDevice{ device } {

        // Сеты теперь выглядят так:
        // Set 0: Global (Camera)
        // Set 1: Storage (Objects, Materials)
        // Set 2: Текстуры (Массив sampler2D)
        createPipelineLayout(globalSetLayout);
        createPipeline(gBufferRenderPass);
    }

    void GeometryRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
        // 1. Создаем Layout для SSBO (Binding 0: Objects, Binding 1: Materials)
        renderSystemLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        // 2. Layout для Текстур (Массив)
        // ВАЖНО: Тут мы используем дескриптор с переменным размером (Bindless)
        textureLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1000) // Массив до 1000 текстур
            .build();

        std::vector<VkDescriptorSetLayout> layouts = {
            globalSetLayout,
            renderSystemLayout->getDescriptorSetLayout(),
            textureLayout->getDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        pipelineLayoutInfo.pSetLayouts = layouts.data();

        // Push constants для быстрой передачи данных (например, индекса объекта)
        VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeometryPushConstants) };
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }
    void GeometryRenderSystem::createPipeline(VkRenderPass renderPass) {
        assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

        PipelineConfigInfo pipelineConfig{};
        BurnhopePipeline::defaultPipelineConfigInfo(pipelineConfig);

        // =====================================================================
        // КРИТИЧЕСКИ ВАЖНО: Настраиваем MRT (Multiple Render Targets)
        // По умолчанию движок настроен на 1 выход (экран). Нам нужно 3 выхода!
        // =====================================================================

        // Создаем настройки для 3-х текстур (Normal, Albedo, Height/AO)
        // Мы отключаем блендинг (прозрачность), потому что в G-Buffer данные пишутся "как есть"
        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(3);
        for (int i = 0; i < 3; i++) {
            blendAttachments[i].colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachments[i].blendEnable = VK_FALSE;
        }

        pipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        pipelineConfig.colorBlendInfo.pAttachments = blendAttachments.data();
        // =====================================================================

        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;

        // ВАЖНО: Указываем твои новые шейдеры!
        lvePipeline = std::make_unique<BurnhopePipeline>(
            lveDevice,
            "shaders/gbuffer.vert.spv",
            "shaders/gbuffer.frag.spv",
            pipelineConfig);
    }
    void GeometryRenderSystem::renderEntities(
        FrameInfo& frameInfo,
        entt::registry& registry,
        VkDescriptorSet storageSet,
        VkDescriptorSet textureSet) {

        lvePipeline->bind(frameInfo.commandBuffer);

        // СОБИРАЕМ ВСЕ 3 СЕТА
        // 0: Global (UBO камеры)
        // 1: Storage (Матрицы объектов и параметры материалов)
        // 2: Textures (Массив текстур)
        std::vector<VkDescriptorSet> sets = {
            frameInfo.globalDescriptorSet,
            storageSet,
            textureSet
        };

        // ВАЖНО: Если какой-то сет не создался, мы не можем рисовать!
        // Лучше выдать ошибку здесь, чем крашнуться внутри драйвера.
        if (storageSet == VK_NULL_HANDLE || textureSet == VK_NULL_HANDLE) {
            std::cerr << "Warning: Cannot render entities, descriptor sets are not initialized!" << std::endl;
            return;
        }

        // БИНДИМ СРАЗУ ВСЕ 3 СЕТА (indexCount = 3)
        vkCmdBindDescriptorSets(
            frameInfo.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,                                   // firstSet
            static_cast<uint32_t>(sets.size()),  // descriptorSetCount (тут будет 3)
            sets.data(),
            0,
            nullptr);

        auto view = registry.view<TransformComponent, MeshComponent>();
        uint32_t instanceIndex = 0; // Наш индекс в SSBO

        for (auto [entity, transformComp, meshComp] : view.each()) {
            if (!meshComp.model || !meshComp.isVisible) continue;

            // Передаем матрицы через Push Constants (или берем из SSBO в шейдере по instanceIndex)
            GeometryPushConstants push{};
            push.modelMatrix = transformComp.transform.matrix;
            push.normalMatrix = glm::transpose(glm::inverse(glm::mat3(transformComp.transform.matrix)));

            vkCmdPushConstants(
                frameInfo.commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(GeometryPushConstants),
                &push);

            meshComp.model->bind(frameInfo.commandBuffer);

            const auto& subMeshes = meshComp.model->getSubMeshes();
            for (const auto& sub : subMeshes) {
                // ВАЖНО: В Deferred мы НЕ переключаем дескрипторы текстур здесь!
                // Шейдер сам возьмет нужную текстуру из массива по ID материала.

                // Если ты хочешь рисовать через gl_InstanceIndex, 
                // передавай instanceIndex как baseInstance
                vkCmdDrawIndexed(frameInfo.commandBuffer, sub.indexCount, 1, sub.firstIndex, 0, instanceIndex);
            }
            instanceIndex++;
        }
    }

} // namespace burnhope