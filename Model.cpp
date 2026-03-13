#include "Model.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <ModelImporter.h>

using json = nlohmann::json;
namespace fs = std::filesystem;
std::map<std::string, std::vector<Mesh>> Model::globalMeshCache;
std::map<std::string, std::vector<std::string>> Model::globalMaterialCache;
Model::Model(std::string const& path, std::string const& root)
{
    this->projectRoot = root; // Запоминаем ПЕРЕД загрузкой
    loadModel(path);
}

void Model::loadModel(std::string const& path)
{
    if (!path.ends_with(".bhmesh")) {
        std::cerr << "[ERROR] Engine now only supports .bhmesh files! Please convert: " << path << std::endl;
        return;
    }

    directory = path.substr(0, path.find_last_of("/\\"));

    // ===============================================================
    // 1. СУПЕР-ОПТИМИЗАЦИЯ: ПРОВЕРКА КЭША
    // Если модель уже грузили, берем данные из RAM/VRAM и выходим!
    // ===============================================================
    if (globalMeshCache.find(path) != globalMeshCache.end()) {
        this->meshes = globalMeshCache[path];
        this->loadedMaterialPaths = globalMaterialCache[path];
        std::cout << "[CACHE] Instanced .bhmesh loaded from memory: " << path << std::endl;
        return;
    }

    // ===============================================================
    // 2. ЕСЛИ В КЭШЕ НЕТ, ЧИТАЕМ С ДИСКА (Твой оригинальный код)
    // ===============================================================
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Failed to open mesh: " << path << std::endl;
        return;
    }

    BHModelHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(BHModelHeader));

    loadedMaterialPaths.clear();
    meshes.clear();

    // Читаем пути к материалам
    for (uint32_t i = 0; i < header.materialCount; ++i) {
        char pathBuf[256];
        file.read(pathBuf, sizeof(pathBuf));
        loadedMaterialPaths.push_back(std::string(pathBuf));
    }

    // Читаем все меши
    for (uint32_t m = 0; m < header.meshCount; ++m) {
        BHMeshHeader meshHeader;
        file.read(reinterpret_cast<char*>(&meshHeader), sizeof(BHMeshHeader));

        uint32_t vCount;
        file.read(reinterpret_cast<char*>(&vCount), sizeof(uint32_t));

        std::vector<Vertex> vertices(vCount);
        file.read(reinterpret_cast<char*>(vertices.data()), vCount * sizeof(Vertex));

        std::vector<LODLevel> lodLevels;
        std::vector<GLuint> allIndices;

        for (uint32_t l = 0; l < meshHeader.lodCount; ++l) {
            BHLodHeader lodHeader;
            file.read(reinterpret_cast<char*>(&lodHeader), sizeof(BHLodHeader));

            uint32_t offset = allIndices.size();

            if (meshHeader.indexType == 0) {
                std::vector<uint16_t> idx16(lodHeader.indexCount);
                file.read(reinterpret_cast<char*>(idx16.data()), lodHeader.indexCount * sizeof(uint16_t));
                for (uint16_t idx : idx16) allIndices.push_back((GLuint)idx);
            }
            else {
                std::vector<uint32_t> idx32(lodHeader.indexCount);
                file.read(reinterpret_cast<char*>(idx32.data()), lodHeader.indexCount * sizeof(uint32_t));
                for (uint32_t idx : idx32) allIndices.push_back(idx);
            }

            lodLevels.push_back({ lodHeader.indexCount, offset });
        }

        Mesh newMesh(vertices, allIndices);
        newMesh.name = meshHeader.name;
        newMesh.lods = lodLevels;
        newMesh.boundingRadius = meshHeader.boundingRadius;

        meshes.push_back(newMesh);
    }

    file.close();

    // ===============================================================
    // 3. СОХРАНЯЕМ В КЭШ ДЛЯ СЛЕДУЮЩИХ КОПИЙ
    // ===============================================================
    globalMeshCache[path] = this->meshes;
    globalMaterialCache[path] = this->loadedMaterialPaths;

    std::cout << "[SUCCESS] Loaded new .bhmesh to VRAM: " << path << std::endl;
}

void Model::processNode(aiNode* node, const aiScene* scene, std::string const& modelPath)
{
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(processMesh(mesh, scene, modelPath));
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene, modelPath);
    }
}
std::string Model::loadMaterialTexture(aiMaterial* mat, aiTextureType type, std::string typeName)
{
    if (!mat) return "";

    if (mat->GetTextureCount(type) > 0)
    {
        aiString str;
        mat->GetTexture(type, 0, &str);
        if (str.length == 0) return "";

        fs::path p(str.C_Str());
        std::string filename = p.filename().string();

        // ==========================================
        // МАГИЯ: АВТО-ПОДМЕНА РАСШИРЕНИЯ НА .bhtex
        // ==========================================
        size_t lastDot = filename.find_last_of(".");
        if (lastDot != std::string::npos) {
            // Отрезаем старое расширение (.png/.jpg) и приклеиваем наше!
            filename = filename.substr(0, lastDot) + ".bhtex";
        }

        // Теперь движок будет искать именно скомпилированный файл!
        fs::path fullTexPath = fs::absolute(fs::path(directory) / filename);
        if (fs::exists(fullTexPath)) {
            if (!projectRoot.empty()) {
                return fs::relative(fullTexPath, fs::absolute(projectRoot)).generic_string();
            }
            else {
                return (fs::path(directory) / filename).generic_string();
            }
        }
        else {
            std::cout << "[WARNING] Texture not found: " << fullTexPath << std::endl;
        }
    }
    return "";
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene, std::string const& modelPath)
{
    if (!mesh || !scene) {
        return Mesh(); // Возвращаем пустой меш, чтобы не упасть
    }

    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    float maxRadiusSquared = 0.0f;
    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        vertex.position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        float distSq = vertex.position.x * vertex.position.x +
            vertex.position.y * vertex.position.y +
            vertex.position.z * vertex.position.z;
        if (distSq > maxRadiusSquared) {
            maxRadiusSquared = distSq;
        }
        if (mesh->HasNormals()) {
            glm::vec3 normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z); \
                vertex.normal = PackNormalTo10_10_10_2(normal);
        }
        else {
            vertex.normal = PackNormalTo10_10_10_2(glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (mesh->mTextureCoords[0])
        {
             glm::vec2 texUV(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            vertex.texUV = glm::packHalf2x16(texUV);
            if (mesh->HasTangentsAndBitangents()) {
                glm::vec3 tangent(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                glm::vec3 bitangent(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
                vertex.tangent = PackNormalTo10_10_10_2(tangent);
                vertex.bitangent = PackNormalTo10_10_10_2(bitangent);
            }
        }
        else {
            vertex.texUV = glm::packHalf2x16(glm::vec2(0.0f, 0.0f));
            vertex.tangent = PackNormalTo10_10_10_2(glm::vec3(1.0f, 0.0f, 0.0f));
            vertex.bitangent = PackNormalTo10_10_10_2(glm::vec3(0.0f, 1.0f, 0.0f));
        }
        vertices.push_back(vertex);
    }
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }
    std::vector<LODLevel> levels;
    levels.push_back({ (unsigned int)indices.size(), 0 });

    // БЕЗОПАСНОСТЬ: Делаем LOD'ы ТОЛЬКО если в меше есть вершины и хотя бы 30 индексов (10 полигонов)
    if (!vertices.empty() && indices.size() >= 30) {
        float thresholds[2] = { 0.5f, 0.1f };
        for (int i = 0; i < 2; i++) {
            unsigned int sourceCount = levels.back().count;
            unsigned int sourceOffset = levels.back().firstIndex;
            unsigned int targetCount = (unsigned int)(levels[0].count * thresholds[i]);

            // Если целевое количество полигонов слишком маленькое, останавливаем генерацию
            if (targetCount < 3) break;

            std::vector<unsigned int> lodIndices(sourceCount);
            unsigned int newCount = 0;

            if (i == 0) {
                float targetError = 0.05f;
                newCount = meshopt_simplify(
                    &lodIndices[0], &indices[sourceOffset], sourceCount,
                    &vertices[0].position.x, vertices.size(), sizeof(Vertex),
                    targetCount, targetError
                );
            }

            if (i == 1 || newCount > targetCount * 1.15f) {
                newCount = meshopt_simplifySloppy(
                    &lodIndices[0], &indices[sourceOffset], sourceCount,
                    &vertices[0].position.x, vertices.size(), sizeof(Vertex),
                    targetCount, 0.1f);
            }

            // Если meshopt вернул 0 (не смог сжать), просто прерываемся
            if (newCount == 0) break;

            lodIndices.resize(newCount);
            unsigned int newOffset = indices.size();
            levels.push_back({ newCount, newOffset });
            indices.insert(indices.end(), lodIndices.begin(), lodIndices.end());
        }
    }

    std::string finalMatPath = ""; // Путь к готовому .bhmat

    if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < scene->mNumMaterials)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        // Берем имя материала из FBX
        aiString matName;
        material->Get(AI_MATKEY_NAME, matName);
        std::string safeMatName = matName.length > 0 ? matName.C_Str() : "DefaultMaterial";

        // Очищаем имя от плохих символов
        std::replace(safeMatName.begin(), safeMatName.end(), '/', '_');
        std::replace(safeMatName.begin(), safeMatName.end(), '\\', '_');
        std::replace(safeMatName.begin(), safeMatName.end(), ':', '_');

        // Путь, куда мы сохраним наш новый .bhmat файл
        finalMatPath = directory + "/" + safeMatName + ".bhmat";

        // Если файл еще не создан
        if (!fs::exists(finalMatPath))
        {
            std::cout << "Авто-создание материала: " << finalMatPath << std::endl;

            // Пытаемся вытащить текстуры (если их нет в папке, вернутся пустые строки)
            std::string albedoPath = loadMaterialTexture(material, aiTextureType_DIFFUSE, "texture_diffuse");
            std::string normalPath = loadMaterialTexture(material, aiTextureType_NORMALS, "texture_normal");
            if (normalPath.empty()) normalPath = loadMaterialTexture(material, aiTextureType_HEIGHT, "texture_normal");
            std::string metallicPath = loadMaterialTexture(material, aiTextureType_METALNESS, "texture_metallic");
            std::string roughnessPath = loadMaterialTexture(material, aiTextureType_DIFFUSE_ROUGHNESS, "texture_roughness");

            // Собираем JSON (ДАЖЕ ЕСЛИ ТЕКСТУР НЕТ, МЫ СОЗДАДИМ ПУСТОЙ МАТЕРИАЛ)
            json j;
            j["name"] = safeMatName;
            j["textures"] = json::object();

            // Записываем только те текстуры, которые реально нашлись
            if (!albedoPath.empty()) j["textures"]["albedo"] = albedoPath;
            if (!normalPath.empty()) j["textures"]["normal"] = normalPath;
            if (!metallicPath.empty()) j["textures"]["metallic"] = metallicPath;
            if (!roughnessPath.empty()) j["textures"]["roughness"] = roughnessPath;

            // Сохраняем файл на диск!
            std::ofstream fileOut(finalMatPath);
            if (fileOut.is_open()) {
                fileOut << j.dump(4);
                fileOut.close();
            }
        }
    }

    // Сохраняем путь в список (ВАЖНО: мы сохраняем путь ОТ КОРНЯ ПРОЕКТА, чтобы UI мог его найти)
    if (!finalMatPath.empty() && !projectRoot.empty()) {
        std::string relMatPath = fs::relative(fs::absolute(finalMatPath), fs::absolute(projectRoot)).generic_string();
        loadedMaterialPaths.push_back(relMatPath);
    }
    else {
        // Если что-то пошло не так (нет projectRoot), кидаем пустоту, чтобы не сломать массив слотов
        loadedMaterialPaths.push_back("");
    }

    Mesh resultMesh(vertices, indices);
    resultMesh.name = mesh->mName.length > 0 ? mesh->mName.C_Str() : "UnnamedMesh";
    resultMesh.lods = levels;
    resultMesh.boundingRadius = std::sqrt(maxRadiusSquared);
    return resultMesh;
}