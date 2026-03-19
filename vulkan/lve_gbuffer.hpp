#pragma once

#include "lve_device.hpp"
#include "lve_texture.hpp"

#include <memory>
#include <vector>

namespace burnhope {

    class BurnhopeGBuffer {
    public:
        BurnhopeGBuffer(BurnhopeDevice& device, VkExtent2D windowExtent);
        ~BurnhopeGBuffer();

        // Запрет копирования
        BurnhopeGBuffer(const BurnhopeGBuffer&) = delete;
        BurnhopeGBuffer& operator=(const BurnhopeGBuffer&) = delete;

        VkRenderPass getRenderPass() const { return renderPass; }
        VkFramebuffer getFramebuffer() const { return framebuffer; }

        // Доступ к текстурам для Compute Шейдера
        BurnhopeTexture* getNormalRoughness() { return normalRoughness.get(); }
        BurnhopeTexture* getAlbedoMetallic() { return albedoMetallic.get(); }
        BurnhopeTexture* getHeightAO() { return heightAO.get(); }
        BurnhopeTexture* getDepth() { return depthTexture.get(); }

    private:
        void createResources();
        void createRenderPass();
        void createFramebuffer();

        BurnhopeDevice& lveDevice;
        VkExtent2D extent;

        // 4 главные текстуры Deferred рендера
        std::unique_ptr<BurnhopeTexture> normalRoughness;
        std::unique_ptr<BurnhopeTexture> albedoMetallic;
        std::unique_ptr<BurnhopeTexture> heightAO;
        std::unique_ptr<BurnhopeTexture> depthTexture;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

} // namespace burnhope