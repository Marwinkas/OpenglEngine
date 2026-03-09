#include "Model.h"
Model::Model(std::string const& path)
{
    loadModel(path);
}
void Model::loadModel(std::string const& path)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals |
        aiProcess_LimitBoneWeights);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "ОШИБКА ЗАГРУЗКИ МОДЕЛИ: " << importer.GetErrorString() << std::endl;
        return;
    }
    directory = path.substr(0, path.find_last_of("/\\"));
    processNode(scene->mRootNode, scene);
}
void Model::processNode(aiNode* node, const aiScene* scene)
{
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(processMesh(mesh, scene));
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene);
    }
}
Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene)
{
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
            vertex.normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        }
        else {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        if (mesh->mTextureCoords[0])
        {
            vertex.texUV = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent = glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                vertex.bitangent = glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
            }
        }
        else {
            vertex.texUV = glm::vec2(0.0f, 0.0f);
        }
        vertices.push_back(vertex);
    }
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }
    // --- АВТОГЕНЕРАЦИЯ LOD ---
    std::vector<LODLevel> levels;
    // Уровень 0: Оригинал
    levels.push_back({ (unsigned int)indices.size(), 0 });
    // Генерируем еще 2 уровня (LOD 1 - 50% и LOD 2 - 10%)
    float thresholds[2] = { 0.5f, 0.1f };
    for (int i = 0; i < 2; i++) {
        unsigned int sourceCount = levels.back().count;
        unsigned int sourceOffset = levels.back().firstIndex;
        unsigned int targetCount = (unsigned int)(levels[0].count * thresholds[i]);
        std::vector<unsigned int> lodIndices(sourceCount);
        unsigned int newCount = 0;
        if (i == 0) {
            // Для LOD 1 (средний) пробуем аккуратное упрощение
            float targetError = 0.05f;
            newCount = meshopt_simplify(
                &lodIndices[0], &indices[sourceOffset], sourceCount,
                &vertices[0].position.x, vertices.size(), sizeof(Vertex),
                targetCount, targetError
            );
        }
        // Если обычный метод не справился (упростил меньше чем на 15%) 
        // или если это уже LOD 2 (далекий)
        if (i == 1 || newCount > targetCount * 1.15f) {
            // Используем Sloppy (грубый) метод. Он точно дожмет до цели!
            newCount = meshopt_simplifySloppy(
                &lodIndices[0], &indices[sourceOffset], sourceCount,
                &vertices[0].position.x, vertices.size(), sizeof(Vertex),
                targetCount, 0.1f // допустимая ошибка для sloppy
            );
        }
        lodIndices.resize(newCount);
        unsigned int newOffset = indices.size();
        levels.push_back({ newCount, newOffset });
        std::cout << "LOD " << i + 1 << " (" << (i == 1 ? "Sloppy" : "Smart")
            << "): " << newCount << " из " << levels[0].count << std::endl;
        indices.insert(indices.end(), lodIndices.begin(), lodIndices.end());
    }
    Mesh resultMesh(vertices, indices);
    resultMesh.lods = levels; // Передаем уровни в меш
    resultMesh.boundingRadius = std::sqrt(maxRadiusSquared);
    return resultMesh;
}