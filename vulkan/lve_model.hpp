#pragma once

#include "lve_buffer.hpp"
#include "lve_device.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// std
#include <memory>
#include <vector>
#include <string>

namespace burnhope {
    struct Vertex {
        glm::vec3 position;   // 12 байт
        glm::vec3 normal;     // 12 байт  
        glm::vec2 texUV;      // 8 байт
        glm::vec3 tangent;    // 12 байт
        glm::vec3 bitangent;  // 12 байт

        static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

        bool operator==(const Vertex& other) const {
            return position == other.position &&
                normal == other.normal &&
                texUV == other.texUV &&
                tangent == other.tangent &&
                bitangent == other.bitangent;
        }
    };
    struct SubMesh {
        uint32_t indexCount;
        uint32_t firstIndex;
        uint32_t materialIndex; // Индекс из исходного файла (FBX/OBJ)
    };
    struct Builder {
        std::vector<Vertex> vertices{};
        std::vector<uint32_t> indices{};
        std::vector<SubMesh> subMeshes{};
        void loadModel(const std::string& filepath);
    };
   
    class BurnhopeModel {
    public:

        const std::vector<SubMesh>& getSubMeshes() const { return subMeshes; }
        BurnhopeModel(BurnhopeDevice& device, const Builder& builder);
        ~BurnhopeModel();

        BurnhopeModel(const BurnhopeModel&) = delete;
        BurnhopeModel& operator=(const BurnhopeModel&) = delete;

        static std::unique_ptr<BurnhopeModel> createModelFromFile(
            BurnhopeDevice& device, const std::string& filepath);

        void bind(VkCommandBuffer commandBuffer);
        void draw(VkCommandBuffer commandBuffer);

    private:
        std::vector<SubMesh> subMeshes; // Наш новый список частей
        void createVertexBuffers(const std::vector<Vertex>& vertices);
        void createIndexBuffers(const std::vector<uint32_t>& indices);

        BurnhopeDevice& lveDevice;

        std::unique_ptr<BurnhopeBuffer> vertexBuffer;
        uint32_t vertexCount;

        bool hasIndexBuffer = false;
        std::unique_ptr<BurnhopeBuffer> indexBuffer;
        uint32_t indexCount;
    };
}  // namespace burnhope