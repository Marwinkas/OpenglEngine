#include "first_app.hpp"
#include "lve_camera.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <chrono>
#include <stdexcept>
#include <array>

namespace burnhope {

    FirstApp::FirstApp() {
        // 1. Создаем большой пул, куда влезет всё: и буферы, и огромный массив текстур!
        globalPool = BurnhopeDescriptorPool::Builder(lveDevice)
            .setMaxSets(100)
            .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) // ← ДОБАВЬ
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 50)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 50)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2000)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 50)
            .build();

        // 2. Загружаем дефолтные текстуры, чтобы они всегда были под рукой
        // Замени пути на свои, если они лежат в другом месте!
        defaultWhiteTex = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/white.png");
        defaultNormalTex = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/normal2.png");

        defaultWhiteMaterial = std::make_shared<Material>();
        defaultWhiteMaterial->setAlbedo(defaultWhiteTex);
        defaultWhiteMaterial->setNormal(defaultNormalTex);
        gBuffer = std::make_unique<BurnhopeGBuffer>(lveDevice, lveWindow.getExtent());
        // 3. Загружаем сцену
        loadGameObjects(registry);

        
    }

    FirstApp::~FirstApp() {
        vkDeviceWaitIdle(lveDevice.device());

        // 1. Сначала всё что использует device
        lightingSystem.reset();
        gBuffer.reset();
        hdrOutputTexture.reset();
        objectBuffer.reset();
        materialBuffer.reset();
        dummyBuffer.reset();

        // 2. Потом пулы и layouts
        globalPool.reset();
        gBufferLayoutPtr.reset();
        outputLayoutPtr.reset();
        dummyShadowPtr.reset();
        dummyLightPtr.reset();

        // 3. lveDevice уничтожается последним (автоматически как член класса)
    }

    void FirstApp::RebuildBatches(entt::registry& registry, GeometryRenderSystem& renderSystem) {
        std::vector<ObjectData> objDataList;
        std::vector<MaterialData> matDataList;
        std::vector<VkDescriptorImageInfo> textureInfos;

        std::map<Material*, uint32_t> matToIndex;
        std::map<BurnhopeTexture*, uint32_t> texToIndex;

        uint32_t globalMatIndex = 0;
        uint32_t globalTexIndex = 0;

        // Кладем дефолтные текстуры в самое начало (они будут под индексами 0 и 1)
        textureInfos.push_back(defaultWhiteTex->getImageInfo());
        uint32_t defaultWhiteIdx = globalTexIndex++;

        textureInfos.push_back(defaultNormalTex->getImageInfo());
        uint32_t defaultNormalIdx = globalTexIndex++;

        // Помощник для поиска и добавления текстур
        auto getTexIndex = [&](std::shared_ptr<BurnhopeTexture> tex, uint32_t defaultIdx) -> uint32_t {
            if (!tex) return defaultIdx;
            if (texToIndex.find(tex.get()) == texToIndex.end()) {
                texToIndex[tex.get()] = globalTexIndex;
                textureInfos.push_back(tex->getImageInfo());
                return globalTexIndex++;
            }
            return texToIndex[tex.get()];
            };

        auto view = registry.view<TransformComponent, MeshComponent>();
        for (auto [entity, transformComp, meshComp] : view.each()) {
            if (!meshComp.model || !meshComp.isVisible) continue;

            const auto& subMeshes = meshComp.model->getSubMeshes();
            for (uint32_t i = 0; i < subMeshes.size(); i++) {
                std::shared_ptr<Material> currentMat = (i < meshComp.materials.size())
                    ? meshComp.materials[i]
                    : defaultWhiteMaterial;

                uint32_t currentMatID = 0;

                // Упаковываем материал, если видим его впервые
                if (matToIndex.find(currentMat.get()) == matToIndex.end()) {
                    currentMatID = globalMatIndex++;
                    matToIndex[currentMat.get()] = currentMatID;

                    MaterialData matData{};
                    matData.uvScale = currentMat->uvScale;
                    matData.hasAlbedo = currentMat->hasAlbedo ? 1 : 0;
                    matData.hasNormal = currentMat->hasNormal ? 1 : 0;
                    matData.hasRoughness = currentMat->hasRoughness ? 1 : 0;
                    matData.hasMetallic = currentMat->hasMetallic ? 1 : 0;
                    matData.hasAO = currentMat->hasAO ? 1 : 0;

                    matData.albedoIdx = getTexIndex(currentMat->albedoMap, defaultWhiteIdx);
                    matData.normalIdx = getTexIndex(currentMat->normalMap, defaultNormalIdx);
                    matData.roughnessIdx = getTexIndex(currentMat->roughnessMap, defaultWhiteIdx);
                    matData.metallicIdx = getTexIndex(currentMat->metallicMap, defaultWhiteIdx);
                    matData.aoIdx = getTexIndex(currentMat->aoMap, defaultWhiteIdx);

                    matDataList.push_back(matData);
                }
                else {
                    currentMatID = matToIndex[currentMat.get()];
                }

                ObjectData obj{};
                obj.modelMatrix = transformComp.transform.matrix;
                obj.materialID = currentMatID;

                objDataList.push_back(obj);
            }
        }

        // Заливаем данные в буферы видеокарты
        if (!objDataList.empty()) {
            objectBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice, sizeof(ObjectData), objDataList.size(),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
            objectBuffer->map();
            objectBuffer->writeToBuffer(objDataList.data());
        }

        if (!matDataList.empty()) {
            materialBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice, sizeof(MaterialData), matDataList.size(),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
            materialBuffer->map();
            materialBuffer->writeToBuffer(matDataList.data());
        }

        // Собираем дескрипторы
        if (objectBuffer && materialBuffer) {
            auto objInfo = objectBuffer->descriptorInfo();
            auto matInfo = materialBuffer->descriptorInfo();

            // Set 1: Буферы
            BurnhopeDescriptorWriter(*renderSystem.getRenderSystemLayout(), *globalPool)
                .writeBuffer(0, &objInfo)
                .writeBuffer(1, &matInfo)
                .build(storageSet);
        }

        if (!textureInfos.empty()) {
            // Set 2: Массив текстур
            BurnhopeDescriptorWriter(*renderSystem.getTextureLayout(), *globalPool)
                .writeImageArray(0, textureInfos) // ВНИМАНИЕ: Здесь должен быть writeImageArray, а не writeImage
                .build(textureSet);
        }
        else {
            // На всякий случай, если массив пуст (чего быть не должно)
            std::cerr << "Error: No textures found for RebuildBatches!" << std::endl;
        }
    }

    void FirstApp::run() {
        // Создаем буферы для камеры
        std::vector<std::unique_ptr<BurnhopeBuffer>> uboBuffers(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < uboBuffers.size(); i++) {
            uboBuffers[i] = std::make_unique<BurnhopeBuffer>(
                lveDevice, sizeof(GlobalUbo), 1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            uboBuffers[i]->map();
        }

        auto globalSetLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::vector<VkDescriptorSet> globalDescriptorSets(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < globalDescriptorSets.size(); i++) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            BurnhopeDescriptorWriter(*globalSetLayout, *globalPool)
                .writeBuffer(0, &bufferInfo)
                .build(globalDescriptorSets[i]);
        }
        initCompute(globalSetLayout->getDescriptorSetLayout());
        // Наша система геометрии
        GeometryRenderSystem simpleRenderSystem{
            lveDevice,
            gBuffer->getRenderPass(),
            globalSetLayout->getDescriptorSetLayout()
        };

        // СБОРКА СЦЕНЫ ПЕРЕД ПЕРВЫМ КАДРОМ!
        RebuildBatches(registry, simpleRenderSystem);

        Camera camera(WIDTH, HEIGHT, glm::vec3(0.0f, 0.0f, 0.0f));

        auto currentTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        auto fpsTimer = currentTime;
        VkExtent2D lastExtent = lveWindow.getExtent();
        while (!lveWindow.shouldClose()) {
            glfwPollEvents();
            // В run() — замени блок resize:
                // Ждём пока окно не нулевого размера
            auto extent = lveWindow.getExtent();
            while (extent.width == 0 || extent.height == 0) {
                extent = lveWindow.getExtent();
                glfwWaitEvents();
            }

            // ← Проверяем ресайз ДО beginFrame
            VkExtent2D swapExtent = lveRenderer.getSwapChainExtent();
            if (extent.width != swapExtent.width || extent.height != swapExtent.height) {
                vkDeviceWaitIdle(lveDevice.device());

                // Swapchain пересоздаём вручную
                lveRenderer.recreateSwapChain(); // ← сделай этот метод публичным!

                VkExtent2D newExtent = lveRenderer.getSwapChainExtent();

                gBuffer = std::make_unique<BurnhopeGBuffer>(lveDevice, newExtent);

                hdrOutputTexture = std::make_unique<BurnhopeTexture>(
                    lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VkExtent3D{ newExtent.width, newExtent.height, 1 },
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_SAMPLE_COUNT_1_BIT);

                rebuildGBufferDescriptorSets();
                continue; // пропускаем кадр — всё пересоздано
            }

            // Дальше обычный render loop
            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            currentTime = newTime;

            frameCount++;
            if (std::chrono::duration<float>(newTime - fpsTimer).count() >= 1.0f) {
                std::cout << "FPS: " << frameCount << std::endl;
                frameCount = 0;
                fpsTimer = newTime;
            }

            camera.Inputs(lveWindow.getGLFWwindow(), frameTime);

            if (auto commandBuffer = lveRenderer.beginFrame()) {

                int frameIndex = lveRenderer.getFrameIndex();

                FrameInfo frameInfo{
                    frameIndex, frameTime, commandBuffer,
                    camera, globalDescriptorSets[frameIndex], *globalPool
                };

                // --- Обновляем UBO ---
                GlobalUbo ubo{};
                ubo.view = camera.GetViewMatrix();
                float aspect = (float)lveWindow.getExtent().width / (float)lveWindow.getExtent().height;

                ubo.projection = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f);
                ubo.view = camera.GetViewMatrix();
                ubo.invViewProj = glm::inverse(ubo.view);
                ubo.camPos = camera.Position;
                ubo.screenSize = glm::vec4(lveWindow.getExtent().width, lveWindow.getExtent().height, 0.f, 0.f);
                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();

                // ================================================
                // ФАЗА 1: G-BUFFER PASS
                // ================================================
                {
                    VkRenderPassBeginInfo renderPassInfo{};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    renderPassInfo.renderPass = gBuffer->getRenderPass();
                    renderPassInfo.framebuffer = gBuffer->getFramebuffer();
                    renderPassInfo.renderArea.offset = { 0, 0 };
                    renderPassInfo.renderArea.extent = lveWindow.getExtent();

                    std::array<VkClearValue, 4> clearValues{};
                    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
                    clearValues[1].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
                    clearValues[2].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
                    clearValues[3].depthStencil = { 1.0f, 0 };
                    renderPassInfo.clearValueCount = (uint32_t)clearValues.size();
                    renderPassInfo.pClearValues = clearValues.data();

                    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                    VkViewport viewport{};
                    viewport.width = (float)lveWindow.getExtent().width;
                    viewport.height = (float)lveWindow.getExtent().height;
                    viewport.minDepth = 0.0f;
                    viewport.maxDepth = 1.0f;
                    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

                    VkRect2D scissor{ {0, 0}, lveWindow.getExtent() };
                    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

                    simpleRenderSystem.renderEntities(frameInfo, registry, storageSet, textureSet);

                    vkCmdEndRenderPass(commandBuffer);
                }

                // ================================================
                // БАРЬЕР: G-Buffer готов, compute может читать
                // ================================================
                {
                    std::array<VkImageMemoryBarrier, 4> gBufBarriers{};
                    VkImage gBufImages[4] = {
                        gBuffer->getNormalRoughness()->getImage(),
                        gBuffer->getAlbedoMetallic()->getImage(),
                        gBuffer->getHeightAO()->getImage(),
                        gBuffer->getDepth()->getImage()
                    };
                    VkImageAspectFlags aspects[4] = {
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT
                    };
                    for (int i = 0; i < 4; i++) {
                        gBufBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        gBufBarriers[i].pNext = nullptr;
                        gBufBarriers[i].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        gBufBarriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        gBufBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        gBufBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        gBufBarriers[i].image = gBufImages[i];
                        gBufBarriers[i].subresourceRange = { aspects[i], 0, 1, 0, 1 };
                        gBufBarriers[i].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                        gBufBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    }
                    gBufBarriers[3].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0,
                        0, nullptr, 0, nullptr,
                        4, gBufBarriers.data());
                }

                // ================================================
                // ФАЗА 2: COMPUTE LIGHTING
                // ================================================
                {
                    std::vector<VkDescriptorSet> computeSets = {
                        globalDescriptorSets[frameIndex],
                        gBufferSet,
                        shadowDummySet,
                        lightDummySet,
                        computeOutputSet
                    };

                    lightingSystem->computeLighting(
                        commandBuffer, computeSets,
                        lveWindow.getExtent().width,
                        lveWindow.getExtent().height);
                }

                // ================================================
                // БАРЬЕР: Compute закончил → готовим blit
                // ================================================
                {
                    VkImageMemoryBarrier hdrToSrc{};
                    hdrToSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    hdrToSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                    hdrToSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    hdrToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    hdrToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    hdrToSrc.image = hdrOutputTexture->getImage();
                    hdrToSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    hdrToSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    hdrToSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr,
                        1, &hdrToSrc);
                }

                // ================================================
                // ФАЗА 3: BLIT на swapchain
                // ================================================
                {
                    VkImage swapChainImage = lveRenderer.getCurrentSwapChainImage();

                    // Swapchain → TRANSFER_DST
                    VkImageMemoryBarrier swapToDst{};
                    swapToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    swapToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    swapToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    swapToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    swapToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    swapToDst.image = swapChainImage;
                    swapToDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    swapToDst.srcAccessMask = 0;
                    swapToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr,
                        1, &swapToDst);

                    // Blit
                    VkImageBlit blit{};
                    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                    blit.srcOffsets[0] = { 0, 0, 0 };
                    blit.srcOffsets[1] = {
                        (int32_t)lveWindow.getExtent().width,
                        (int32_t)lveWindow.getExtent().height, 1 };
                    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                    blit.dstOffsets[0] = { 0, 0, 0 };
                    blit.dstOffsets[1] = {
                        (int32_t)lveWindow.getExtent().width,
                        (int32_t)lveWindow.getExtent().height, 1 };

                    vkCmdBlitImage(commandBuffer,
                        hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1, &blit, VK_FILTER_LINEAR);

                    // Swapchain → COLOR_ATTACHMENT для UI
                    VkImageMemoryBarrier swapToAttach{};
                    swapToAttach.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    swapToAttach.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    swapToAttach.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    swapToAttach.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    swapToAttach.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    swapToAttach.image = swapChainImage;
                    swapToAttach.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    swapToAttach.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    swapToAttach.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

                    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        0, 0, nullptr, 0, nullptr,
                        1, &swapToAttach);
                }

                // ================================================
                // ФАЗА 4: UI поверх
                // ================================================
                lveRenderer.beginSwapChainRenderPass(commandBuffer);
                ui.Draw(lveWindow, camera, registry, commandBuffer);
                lveRenderer.endSwapChainRenderPass(commandBuffer);

                // ================================================
                // ФАЗА 5: hdrOutputTexture обратно в GENERAL
                // ================================================
                {
                    VkImageMemoryBarrier hdrToGeneral{};
                    hdrToGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    hdrToGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    hdrToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    hdrToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    hdrToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    hdrToGeneral.image = hdrOutputTexture->getImage();
                    hdrToGeneral.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    hdrToGeneral.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    hdrToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

                    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr,
                        1, &hdrToGeneral);
                }
                VkImageMemoryBarrier depthToAttach{};
                depthToAttach.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                depthToAttach.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                depthToAttach.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthToAttach.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthToAttach.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthToAttach.image = gBuffer->getDepth()->getImage();
                depthToAttach.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
                depthToAttach.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                depthToAttach.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                vkCmdPipelineBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    0, 0, nullptr, 0, nullptr,
                    1, &depthToAttach);

                lveRenderer.endFrame();
            }
        }

        vkDeviceWaitIdle(lveDevice.device());
    }
    void FirstApp::initCompute(VkDescriptorSetLayout globalSetLayout) {
        VkExtent3D extent = { lveWindow.getExtent().width, lveWindow.getExtent().height, 1 };
        hdrOutputTexture = std::make_unique<BurnhopeTexture>(
            lveDevice,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            extent,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);

        hdrOutputTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);

        // 1. СОХРАНЯЕМ LAYOUT'Ы В КЛАСС (Они больше не удалятся)
        gBufferLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        outputLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        dummyShadowPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        dummyLightPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        // 2. ЗАПОЛНЯЕМ G-BUFFER СЕТ
        auto normInfo = gBuffer->getNormalRoughness()->getImageInfo();
        auto albInfo = gBuffer->getAlbedoMetallic()->getImageInfo();
        auto extraInfo = gBuffer->getHeightAO()->getImageInfo();
        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = gBuffer->getDepth()->getSampler();
        depthInfo.imageView = gBuffer->getDepth()->getImageView();
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // <-- вот фикс

        BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
            .writeImage(0, &normInfo)
            .writeImage(1, &albInfo)
            .writeImage(2, &extraInfo)
            .writeImage(3, &depthInfo)  // depth с правильным layout
            .build(gBufferSet);

        // 3. ЗАПОЛНЯЕМ OUTPUT СЕТ
        VkDescriptorImageInfo outImgInfo{};
        outImgInfo.imageView = hdrOutputTexture->getImageView();
        outImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        BurnhopeDescriptorWriter(*outputLayoutPtr, *globalPool)
            .writeImage(0, &outImgInfo)
            .build(computeOutputSet);

        // 4. ЗАПОЛНЯЕМ ЗАГЛУШКУ ТЕНЕЙ
        auto whiteInfo = defaultWhiteTex->getImageInfo();
        BurnhopeDescriptorWriter(*dummyShadowPtr, *globalPool)
            .writeImage(0, &whiteInfo).writeImage(1, &whiteInfo).writeImage(2, &whiteInfo).build(shadowDummySet);

        // 5. СОЗДАЕМ И ЗАПОЛНЯЕМ ЗАГЛУШКУ СВЕТА (Этого не хватало!)
        dummyBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, 16, 1, // Размер 16 байт хватит для пустышки
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        auto dummyBufInfo = dummyBuffer->descriptorInfo();

        BurnhopeDescriptorWriter(*dummyLightPtr, *globalPool)
            .writeBuffer(0, &dummyBufInfo).writeBuffer(1, &dummyBufInfo).writeBuffer(2, &dummyBufInfo)
            .build(lightDummySet);

        // 6. ПРАВИЛЬНЫЙ ПОРЯДОК LAYOUT'ОВ
        std::vector<VkDescriptorSetLayout> computeLayouts = {
            globalSetLayout,                            // Set 0: Камера (ПРАВИЛЬНО!)
            gBufferLayoutPtr->getDescriptorSetLayout(), // Set 1: G-Buffer
            dummyShadowPtr->getDescriptorSetLayout(),   // Set 2: Shadows
            dummyLightPtr->getDescriptorSetLayout(),    // Set 3: Lights
            outputLayoutPtr->getDescriptorSetLayout()   // Set 4: Output
        };

        lightingSystem = std::make_unique<DeferredLightingSystem>(lveDevice, computeLayouts);
    }
    void FirstApp::rebuildGBufferDescriptorSets() {
        if (gBufferSet != VK_NULL_HANDLE) {
            std::vector<VkDescriptorSet> toFree = { gBufferSet };
            globalPool->freeDescriptors(toFree);
            gBufferSet = VK_NULL_HANDLE;
        }
        if (computeOutputSet != VK_NULL_HANDLE) {
            std::vector<VkDescriptorSet> toFree = { computeOutputSet };
            globalPool->freeDescriptors(toFree);
            computeOutputSet = VK_NULL_HANDLE;
        }

        // Пересоздаём G-Buffer сет (текстуры изменились)
        auto normInfo = gBuffer->getNormalRoughness()->getImageInfo();
        auto albInfo = gBuffer->getAlbedoMetallic()->getImageInfo();
        auto extraInfo = gBuffer->getHeightAO()->getImageInfo();

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = gBuffer->getDepth()->getSampler();
        depthInfo.imageView = gBuffer->getDepth()->getImageView();
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        bool ok = BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
            .writeImage(0, &normInfo)
            .writeImage(1, &albInfo)
            .writeImage(2, &extraInfo)
            .writeImage(3, &depthInfo)
            .build(gBufferSet);

        if (!ok || gBufferSet == VK_NULL_HANDLE) {
            throw std::runtime_error("Failed to rebuild gBufferSet!");
        }


        // Пересоздаём output сет (hdrOutputTexture тоже пересоздана)
        // Сначала переводим layout в GENERAL
        hdrOutputTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);

        VkDescriptorImageInfo outImgInfo{};
        outImgInfo.imageView = hdrOutputTexture->getImageView();
        outImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        ok = BurnhopeDescriptorWriter(*outputLayoutPtr, *globalPool)
            .writeImage(0, &outImgInfo)
            .build(computeOutputSet);

        if (!ok || computeOutputSet == VK_NULL_HANDLE) {
            throw std::runtime_error("Failed to rebuild computeOutputSet!");
        }

    }
    void FirstApp::loadGameObjects(entt::registry& registry) {
        std::shared_ptr<BurnhopeTexture> diffuseTexture =
            BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/diffuse.png");

        std::shared_ptr<BurnhopeModel> lveModel =
            BurnhopeModel::createModelFromFile(lveDevice, "models/evil.bhmesh");

        std::shared_ptr<Material> material = std::make_shared<Material>();
        material->setAlbedo(diffuseTexture);

        auto cubeEntity = registry.create();
        registry.emplace<TagComponent>(cubeEntity, "Vulkan Cube");

        auto& transform = registry.emplace<TransformComponent>(cubeEntity);
        transform.transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
        transform.transform.matrix = glm::translate(glm::mat4(1.0f), transform.transform.position);

        auto& mesh = registry.emplace<MeshComponent>(cubeEntity);
        mesh.model = lveModel;
        mesh.materials.push_back(material);
    }
} // namespace burnhope