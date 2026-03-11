#ifndef VOXELSHADER_CLASS_H
#define VOXELSHADER_CLASS_H
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>
#include <vector>
#include <string>
#include "shaderClass.h"
#include "Window.h"
#include "GameObject.h"
#include <glm/gtc/random.hpp>
class VoxelShader : public Shader {
public:
    struct {
        GLint gridSize, gridMin, gridMax, sunDir, model, albedoMap;
    } loc;
    VoxelShader() : Shader("voxelize.vert", "voxelize.frag", "voxelize.geom") {
        Init();
    }
    void Init() override {
        loc.gridSize = GetLoc("gridSize");
        loc.gridMin = GetLoc("gridMin");
        loc.gridMax = GetLoc("gridMax");
        loc.sunDir = GetLoc("sunDir");
        loc.model = GetLoc("model");
        loc.albedoMap = GetLoc("model");
        std::cout << "VoxelShader: Локации успешно запечены!" << std::endl;
    }


};
#endif