#include "Model.hpp"
#include "../Utils/Utils.hpp"

// libs
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// std
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

#ifndef ENGINE_DIR
#define ENGINE_DIR "../"
#endif
#include "ModelImporter.h"
#include <filesystem>

namespace burnhope {

    // ----------------------------------------

    BurnhopeModel::BurnhopeModel(BurnhopeDevice& device, const Builder& builder) : lveDevice{ device } {
        createVertexBuffers(builder.vertices);
        createIndexBuffers(builder.indices);
        this->subMeshes = builder.subMeshes;
    }

    BurnhopeModel::~BurnhopeModel() {}

    std::unique_ptr<BurnhopeModel> BurnhopeModel::createModelFromFile(
        BurnhopeDevice& device, const std::string& filepath) {

        std::string finalPath = ENGINE_DIR + filepath;

        // Если ты попросил загрузить оригинальную модель (obj или fbx)
        if (finalPath.ends_with(".obj") || finalPath.ends_with(".fbx")) {

            // Придумываем имя для нашего бинарника (меняем расширение на .bhmesh)
            std::string bhmeshPath = finalPath.substr(0, finalPath.find_last_of('.')) + ".bhmesh";

            // Если такого файла еще нет, значит нужно сконвертировать!
            if (!std::filesystem::exists(bhmeshPath)) {
                std::cout << "Вижу новую модель! Конвертирую в .bhmesh...\n";

                // Вызываем твой импортер (корневую папку укажешь свою)
                ModelImporter::ImportModel(finalPath, bhmeshPath);
            }

            // Теперь мы точно знаем, что .bhmesh существует. Загружаем именно его!
            finalPath = bhmeshPath;
        }

        Builder builder{};
        // Загрузчик теперь всегда читает только быстрые .bhmesh
        builder.loadModel(finalPath);

        return std::make_unique<BurnhopeModel>(device, builder);
    }
    void BurnhopeModel::createVertexBuffers(const std::vector<Vertex>& vertices) {
        vertexCount = static_cast<uint32_t>(vertices.size());
        assert(vertexCount >= 3 && "Vertex count must be at least 3");
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
        uint32_t vertexSize = sizeof(vertices[0]);

        BurnhopeBuffer stagingBuffer{
            lveDevice,
            vertexSize,
            vertexCount,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };

        stagingBuffer.map();
        stagingBuffer.writeToBuffer((void*)vertices.data());

        vertexBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            vertexSize,
            vertexCount,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        lveDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
    }
    void BurnhopeModel::createIndexBuffers(const std::vector<uint32_t>& indices) {
        indexCount = static_cast<uint32_t>(indices.size());
        hasIndexBuffer = indexCount > 0;

        if (!hasIndexBuffer) {
            return;
        }

        VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
        uint32_t indexSize = sizeof(indices[0]);

        BurnhopeBuffer stagingBuffer{
            lveDevice,
            indexSize,
            indexCount,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };

        stagingBuffer.map();
        stagingBuffer.writeToBuffer((void*)indices.data());

        indexBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            indexSize,
            indexCount,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        lveDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
    }
    void BurnhopeModel::bind(VkCommandBuffer commandBuffer) {
        VkBuffer buffers[] = { vertexBuffer->getBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

        if (hasIndexBuffer) {
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
        }
    }
    void BurnhopeModel::draw(VkCommandBuffer commandBuffer) {
        if (hasIndexBuffer) {
            vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
        }
        else {
            vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
        }
    }
    std::vector<VkVertexInputBindingDescription> Vertex::getBindingDescriptions() {
        std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
        bindingDescriptions[0].binding = 0;
        bindingDescriptions[0].stride = sizeof(Vertex);
        bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescriptions;
    }

    std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(5);

        // 0: Позиция (vec3)
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, position);

        // 1: Нормаль (vec3)
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        // 2: UV координаты (vec2)
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texUV);

        // 3: Тангенс (vec3)
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, tangent);

        // 4: Битангенс (vec3)
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, bitangent);

        return attributeDescriptions;
    }

    // ==========================================================
    // НОВЫЙ ЧТИТЕЛЬ ФОРМАТА .BHMESH
    // ==========================================================
    void Builder::loadModel(const std::string& filepath) {
        if (!filepath.ends_with(".bhmesh")) {
            throw std::runtime_error("[ERROR] Only .bhmesh files are supported! " + filepath);
        }

        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("[ERROR] Failed to open .bhmesh: " + filepath);
        }

        // 1. Читаем главный заголовок
        BHModelHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(BHModelHeader));

        if (std::string(header.magic, 4) != "BHMD") {
            throw std::runtime_error("[ERROR] Not a valid BurnHope Model file!");
        }

        // 2. Пропускаем пути к материалам (мы их теперь вешаем через UI)
        for (uint32_t i = 0; i < header.materialCount; ++i) {
            char pathBuf[256];
            file.read(pathBuf, sizeof(pathBuf));
        }

        vertices.clear();
        indices.clear();
        subMeshes.clear(); // Очищаем список частей перед загрузкой

        // 3. Читаем каждый саб-меш
        for (uint32_t m = 0; m < header.meshCount; ++m) {
            BHMeshHeader meshHeader;
            file.read(reinterpret_cast<char*>(&meshHeader), sizeof(BHMeshHeader));

            uint32_t vCount;
            file.read(reinterpret_cast<char*>(&vCount), sizeof(uint32_t));

            // Предохранитель от битых файлов
            if (vCount > 10000000) {
                throw std::runtime_error("[FATAL] Mesh too large! Struct alignment mismatch?");
            }

            // Запоминаем текущее кол-во вершин для смещения индексов
            uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());

            // Читаем вершины (Vertex всегда 56 байт)
            std::vector<Vertex> subVertices(vCount);
            file.read(reinterpret_cast<char*>(subVertices.data()), vCount * sizeof(Vertex));
            vertices.insert(vertices.end(), subVertices.begin(), subVertices.end());

            // 4. Читаем LOD'ы
            for (uint32_t l = 0; l < meshHeader.lodCount; ++l) {
                BHLodHeader lodHeader;
                file.read(reinterpret_cast<char*>(&lodHeader), sizeof(BHLodHeader));

                // Нам нужен только LOD 0 (максимальная детализация)
                if (l == 0) {
                    // СОХРАНЯЕМ ИНФОРМАЦИЮ О SUBMESH
                    SubMesh sub{};
                    sub.indexCount = lodHeader.indexCount;
                    sub.firstIndex = static_cast<uint32_t>(indices.size());
                    sub.materialIndex = meshHeader.materialIndex;
                    subMeshes.push_back(sub);

                    // Читаем индексы
                    if (meshHeader.indexType == 0) { // 16-bit
                        std::vector<uint16_t> idx16(lodHeader.indexCount);
                        file.read(reinterpret_cast<char*>(idx16.data()), lodHeader.indexCount * sizeof(uint16_t));
                        for (uint16_t idx : idx16) {
                            indices.push_back(static_cast<uint32_t>(idx) + vertexOffset);
                        }
                    }
                    else { // 32-bit
                        std::vector<uint32_t> idx32(lodHeader.indexCount);
                        file.read(reinterpret_cast<char*>(idx32.data()), lodHeader.indexCount * sizeof(uint32_t));
                        for (uint32_t idx : idx32) {
                            indices.push_back(idx + vertexOffset);
                        }
                    }
                }
                else {
                    // Если это не LOD 0, просто пропускаем байты индексов в файле
                    uint32_t indexSize = (meshHeader.indexType == 0) ? 2 : 4;
                    file.seekg(lodHeader.indexCount * indexSize, std::ios::cur);
                }
            }
        }

        file.close();
        std::cout << "[SUCCESS] Loaded " << filepath << ": "
            << vertices.size() << " verts, "
            << subMeshes.size() << " submeshes\n";
    }
}