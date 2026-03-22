#pragma once

#include "Utils/Device.hpp"
#include "Utils/Renderer.hpp"
#include "Utils/Window.hpp"
#include "Utils/Descriptors.hpp"
#include "Render/Material.hpp"
#include "Utils/Components.hpp"
#include "Utils/UI.hpp"
#include "Render/Gbuffer.hpp"
// Подключаем систему рендера (убедись, что название файла совпадает с твоим)
#include "Render/MainRender.hpp" 
#include "Render/ShadowRender.hpp"
// std
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <filesystem>

// Небольшой макрос для Windows
#define NOMINMAX
#include <windows.h>
#include "Render/Deferred.hpp"
#include "Render/Shadow.hpp"

namespace burnhope {

    // Функция для получения пути к экзешнику
    inline std::string getExecutablePaths() {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::filesystem::path exePath(buffer);
        std::string path = exePath.parent_path().string();
        std::cout << "[DEBUG] Путь к EXE: " << path << std::endl;
        return path;
    }

    class FirstApp {
    public:
        static constexpr int WIDTH = 800;
        static constexpr int HEIGHT = 600;

        FirstApp();
        ~FirstApp();

        FirstApp(const FirstApp&) = delete;
        FirstApp& operator=(const FirstApp&) = delete;

        void run();

    private:
        void loadGameObjects(entt::registry& registry);

        void initCompute(VkDescriptorSetLayout globalSetLayout);
        void rebuildGBufferDescriptorSets();
        std::unique_ptr<DeferredLightingSystem> lightingSystem;
        std::unique_ptr<BurnhopeTexture> hdrOutputTexture; // Холст для готового кадра
        std::unique_ptr<ShadowRenderSystem> shadowRenderSystem;

        // Отдельный descriptor set layout для shadow pass (objectBuffer)
        std::unique_ptr<BurnhopeDescriptorSetLayout> shadowObjectLayoutPtr;
        VkDescriptorSet shadowObjectSet = VK_NULL_HANDLE;
        // Сеты для Compute-шейдера
        VkDescriptorSetLayout gBufferSetLayout;
        VkDescriptorSetLayout outputSetLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> gBufferLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> outputLayoutPtr;

        std::unique_ptr<BurnhopeBuffer> dummyGridBuffer;
        std::unique_ptr<BurnhopeBuffer> dummyIndexBuffer;

        VkDescriptorSet gBufferSet;      // Set 1
        VkDescriptorSet shadowDummySet;  // Set 2
        VkDescriptorSet lightDummySet;   // Set 3
        VkDescriptorSet computeOutputSet;// Set 4
        // Наша главная функция сборки данных перед кадром
        void RebuildBatches(entt::registry& registry, GeometryRenderSystem& renderSystem);

        BurnhopeWindow lveWindow{ WIDTH, HEIGHT, "BurnHope Engine" };
        BurnhopeDevice lveDevice{ lveWindow };
        BurnhopeRenderer lveRenderer{ lveWindow, lveDevice };

        // Дефолтные текстуры и материалы
        std::shared_ptr<BurnhopeTexture> defaultWhiteTex;
        std::shared_ptr<BurnhopeTexture> defaultNormalTex;
        std::shared_ptr<Material> defaultWhiteMaterial;

        std::unique_ptr<BurnhopeShadowSystem> shadowSystem;
        std::unique_ptr<BurnhopeBuffer>       lightUboBuffer;
        std::unique_ptr<BurnhopeDescriptorSetLayout> shadowLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> lightLayoutPtr;
        VkDescriptorSet shadowSet = VK_NULL_HANDLE;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        // Наш UI
        UI ui{ lveWindow, lveDevice, lveRenderer.getSwapChainRenderPass(), "C:/", "C:/" };

        // База данных объектов
        entt::registry registry;

        // Буферы для передачи данных на видеокарту (ССБО)
        std::unique_ptr<BurnhopeBuffer> objectBuffer;
        std::unique_ptr<BurnhopeBuffer> materialBuffer;

        // Наборы данных (Сеты) для шейдера
        VkDescriptorSet storageSet;
        VkDescriptorSet textureSet;
        std::unique_ptr<BurnhopeGBuffer> gBuffer;
        // Пул, из которого мы берем память под дескрипторы
        std::unique_ptr<BurnhopeDescriptorPool> globalPool{};
    };

} // namespace burnhope