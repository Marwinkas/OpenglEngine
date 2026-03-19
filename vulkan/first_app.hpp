#pragma once

#include "lve_device.hpp"
#include "lve_renderer.hpp"
#include "lve_window.hpp"
#include "lve_descriptors.hpp"
#include "Material.hpp"
#include "Components.h"
#include "UI.h"
#include "lve_gbuffer.hpp"
// Подключаем систему рендера (убедись, что название файла совпадает с твоим)
#include "simple_render_system.hpp" 

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
#include "deferred_lighting_system.hpp"

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

        // Сеты для Compute-шейдера
        VkDescriptorSetLayout gBufferSetLayout;
        VkDescriptorSetLayout outputSetLayout;
        VkDescriptorSetLayout dummyShadowLayout; // Временная заглушка
        VkDescriptorSetLayout dummyLightLayout;  // Временная заглушка
        std::unique_ptr<BurnhopeDescriptorSetLayout> gBufferLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> outputLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> dummyShadowPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> dummyLightPtr;

        std::unique_ptr<BurnhopeBuffer> dummyBuffer; // Буфер для заглушек

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