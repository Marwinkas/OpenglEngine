#include "ModelImporter.h"
#include "Components.h" // Для struct Vertex
#include <iostream>
#include <fstream>
#include <filesystem>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <glm/gtc/packing.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Наш упаковщик из прошлых уроков
uint32_t ModelImporter::PackNormalTo10_10_10_2(glm::vec3 normal) {
    if (glm::length(normal) < 0.0001f) normal = glm::vec3(0.0f, 1.0f, 0.0f);
    else normal = glm::normalize(normal);
    return glm::packSnorm3x10_1x2(glm::vec4(normal, 0.0f));
}

// Извлечение и авто-создание .bhmat (взято из твоего старого Model.cpp)
std::string ModelImporter::ExtractMaterial(aiMaterial* mat, const std::string& directory, const std::string& projectRoot) {
    if (!mat) return "";
    aiString matName;
    mat->Get(AI_MATKEY_NAME, matName);
    std::string safeMatName = matName.length > 0 ? matName.C_Str() : "DefaultMaterial";
    std::replace(safeMatName.begin(), safeMatName.end(), '/', '_');
    std::replace(safeMatName.begin(), safeMatName.end(), '\\', '_');
    std::replace(safeMatName.begin(), safeMatName.end(), ':', '_');

    std::string finalMatPath = directory + "/" + safeMatName + ".bhmat";

    if (!fs::exists(finalMatPath)) {
        json j;
        j["name"] = safeMatName;
        j["textures"] = json::object();

        auto getTex = [&](aiTextureType type, const char* key) {
            if (mat->GetTextureCount(type) > 0) {
                aiString str; mat->GetTexture(type, 0, &str);
                if (str.length > 0) {
                    std::string filename = fs::path(str.C_Str()).filename().string();
                    size_t lastDot = filename.find_last_of(".");
                    if (lastDot != std::string::npos) filename = filename.substr(0, lastDot) + ".bhtex"; // Ищем сжатую версию!

                    fs::path fullTexPath = fs::absolute(fs::path(directory) / filename);
                    j["textures"][key] = fs::relative(fullTexPath, fs::absolute(projectRoot)).generic_string();
                }
            }
            };

        getTex(aiTextureType_DIFFUSE, "albedo");
        getTex(aiTextureType_NORMALS, "normal");
        if (!j["textures"].contains("normal")) getTex(aiTextureType_HEIGHT, "normal");
        getTex(aiTextureType_METALNESS, "metallic");
        getTex(aiTextureType_DIFFUSE_ROUGHNESS, "roughness");

        std::ofstream fileOut(finalMatPath);
        if (fileOut.is_open()) { fileOut << j.dump(4); fileOut.close(); }
    }
    return fs::relative(fs::absolute(finalMatPath), fs::absolute(projectRoot)).generic_string();
}

bool ModelImporter::ImportModel(const std::string& srcPath, const std::string& destPath, const std::string& projectRoot) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(srcPath,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "[ERROR] Assimp: " << importer.GetErrorString() << std::endl;
        return false;
    }

    std::string directory = srcPath.substr(0, srcPath.find_last_of("/\\"));

    // Открываем бинарный файл для записи
    std::ofstream outFile(destPath, std::ios::binary);
    if (!outFile.is_open()) return false;

    // 1. Собираем материалы
    std::vector<std::string> materialPaths;
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        materialPaths.push_back(ExtractMaterial(scene->mMaterials[i], directory, projectRoot));
    }

    BHModelHeader modelHeader;
    modelHeader.meshCount = scene->mNumMeshes;
    modelHeader.materialCount = materialPaths.size();

    // Пишем заголовок модели
    outFile.write(reinterpret_cast<const char*>(&modelHeader), sizeof(BHModelHeader));

    // Пишем пути к материалам (фиксированный размер 256 байт для простоты чтения)
    for (const std::string& matPath : materialPaths) {
        char pathBuf[256] = { 0 };
        strcpy_s(pathBuf, sizeof(pathBuf), matPath.c_str());
        outFile.write(pathBuf, sizeof(pathBuf));
    }

    // 2. Обрабатываем каждый меш
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        aiMesh* aimesh = scene->mMeshes[m];

        std::vector<Vertex> vertices(aimesh->mNumVertices);
        std::vector<uint32_t> indices;

        glm::vec3 aabbMin(100000.0f);
        glm::vec3 aabbMax(-100000.0f);

        // --- ИЗВЛЕЧЕНИЕ И УПАКОВКА ВЕРШИН ---
        for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
            Vertex& v = vertices[i];
            v.position = glm::vec3(aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z);

            aabbMin = glm::min(aabbMin, v.position);
            aabbMax = glm::max(aabbMax, v.position);

            if (aimesh->HasNormals()) v.normal = PackNormalTo10_10_10_2(glm::vec3(aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z));
            else v.normal = PackNormalTo10_10_10_2(glm::vec3(0.0f, 1.0f, 0.0f));

            if (aimesh->mTextureCoords[0]) {
                v.texUV = glm::packHalf2x16(glm::vec2(aimesh->mTextureCoords[0][i].x, aimesh->mTextureCoords[0][i].y));
                if (aimesh->HasTangentsAndBitangents()) {
                    v.tangent = PackNormalTo10_10_10_2(glm::vec3(aimesh->mTangents[i].x, aimesh->mTangents[i].y, aimesh->mTangents[i].z));
                    v.bitangent = PackNormalTo10_10_10_2(glm::vec3(aimesh->mBitangents[i].x, aimesh->mBitangents[i].y, aimesh->mBitangents[i].z));
                }
                else {
                    v.tangent = PackNormalTo10_10_10_2(glm::vec3(1.0f, 0.0f, 0.0f));
                    v.bitangent = PackNormalTo10_10_10_2(glm::vec3(0.0f, 1.0f, 0.0f));
                }
            }
            else {
                v.texUV = 0; v.tangent = PackNormalTo10_10_10_2(glm::vec3(1.0f, 0.0f, 0.0f)); v.bitangent = PackNormalTo10_10_10_2(glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }

        for (unsigned int i = 0; i < aimesh->mNumFaces; i++) {
            for (unsigned int j = 0; j < aimesh->mFaces[i].mNumIndices; j++)
                indices.push_back(aimesh->mFaces[i].mIndices[j]);
        }

        // --- СУПЕР-ОПТИМИЗАЦИЯ (MESHOPTIMIZER) ---
        // 1. Оптимизация кэша вершин (сортирует треугольники)
        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());
        // 2. Оптимизация Overdraw (уменьшает нагрузку на пиксельный шейдер)
        meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), &vertices[0].position.x, vertices.size(), sizeof(Vertex), 1.05f);
        // 3. Оптимизация выборки вершин (сортирует сами вершины в памяти)
        meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));

        // --- ГЕНЕРАЦИЯ LOD-ов ---
        std::vector<std::vector<uint32_t>> lods;
        lods.push_back(indices); // LOD 0 (Оригинал)

        if (vertices.size() > 0 && indices.size() >= 30) {
            float thresholds[2] = { 0.5f, 0.1f };
            for (int i = 0; i < 2; i++) {
                uint32_t targetCount = (uint32_t)(lods[0].size() * thresholds[i]);
                if (targetCount < 3) break;

                std::vector<uint32_t> lodIndices(lods.back().size());
                size_t newCount = 0;

                if (i == 0) {
                    newCount = meshopt_simplify(lodIndices.data(), lods.back().data(), lods.back().size(), &vertices[0].position.x, vertices.size(), sizeof(Vertex), targetCount, 0.05f);
                }
                else {
                    newCount = meshopt_simplifySloppy(lodIndices.data(), lods.back().data(), lods.back().size(), &vertices[0].position.x, vertices.size(), sizeof(Vertex), targetCount, 0.1f);
                }

                if (newCount == 0) break;
                lodIndices.resize(newCount);
                lods.push_back(lodIndices);
            }
        }

        // --- ЗАПИСЬ САБ-МЕША ---
        BHMeshHeader meshHeader;
        strcpy_s(meshHeader.name, sizeof(meshHeader.name), aimesh->mName.length > 0 ? aimesh->mName.C_Str() : "Mesh");
        meshHeader.materialIndex = aimesh->mMaterialIndex;
        meshHeader.aabbMin = aabbMin;
        meshHeader.aabbMax = aabbMax;
        meshHeader.boundingRadius = glm::distance(aabbMin, aabbMax) * 0.5f;
        meshHeader.lodCount = lods.size();
        meshHeader.indexType = (vertices.size() <= 65535) ? 0 : 1; // 0 = 16-bit, 1 = 32-bit

        outFile.write(reinterpret_cast<const char*>(&meshHeader), sizeof(BHMeshHeader));

        // Пишем вершины
        uint32_t vCount = vertices.size();
        outFile.write(reinterpret_cast<const char*>(&vCount), sizeof(uint32_t));
        outFile.write(reinterpret_cast<const char*>(vertices.data()), vCount * sizeof(Vertex));

        // Пишем LOD-ы
        for (const auto& lodIndices : lods) {
            BHLodHeader lodHeader;
            lodHeader.indexCount = lodIndices.size();
            outFile.write(reinterpret_cast<const char*>(&lodHeader), sizeof(BHLodHeader));

            if (meshHeader.indexType == 0) {
                // Жмем индексы в 16 бит для экономии!
                std::vector<uint16_t> indices16(lodIndices.size());
                for (size_t i = 0; i < lodIndices.size(); ++i) indices16[i] = (uint16_t)lodIndices[i];
                outFile.write(reinterpret_cast<const char*>(indices16.data()), indices16.size() * sizeof(uint16_t));
            }
            else {
                // Оставляем 32 бита
                outFile.write(reinterpret_cast<const char*>(lodIndices.data()), lodIndices.size() * sizeof(uint32_t));
            }
        }
    }

    outFile.close();
    std::cout << "[SUCCESS] Imported model to .bhmesh: " << destPath << "\n";
    return true;
}