#ifndef MODEL_CLASS_H
#define MODEL_CLASS_H
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "Mesh.h"
#include <string>
#include <vector>
#include <iostream>
#include <map>
#include <unordered_map> 
#include <cmath> 
#include <algorithm>
#include <meshoptimizer.h>
class Model
{ 
public: 
    std::vector<Mesh> meshes;
    std::string directory;
    Model(std::string const& path);
private:
    void loadModel(std::string const& path);
    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
};
#endif