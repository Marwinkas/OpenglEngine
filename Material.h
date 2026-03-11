#ifndef MATERIAL_CLASS_H
#define MATERIAL_CLASS_H
#include<string>
#include <filesystem>
namespace fs = std::filesystem;
#include"VAO.h"
#include"EBO.h"
#include"Camera.h"
#include"Texture.h"
#include "shaderClass.h"
static int MaterialID;
struct MaterialGPUData {
    GLuint64 albedoHandle;
    GLuint64 normalHandle;
    GLuint64 heightHandle;
    GLuint64 metallicHandle;
    GLuint64 roughnessHandle;
    GLuint64 aoHandle;
    int hasAlbedo;
    int hasNormal;
    int hasHeight;
    int hasMetallic;
    int hasRoughness;
    int hasAO;
    int useTriplanar = 1;
    float triplanarScale = 4.0f;
    float padding[2]; 
};
class Material
{
public:
    int ID;
    Material() {
        ID = MaterialID;
        MaterialID++;
        hasalbedo = false;
        hasnormal = false;
        hasheight = false;
        hasmetallic = false;
        hasroughness = false;
        hasao = false;
    };
    MaterialGPUData getGPUData();
    Texture albedo;
    Texture normal;
    Texture height;
    Texture metallic;
    Texture roughness;
    Texture ao;
    bool hasalbedo;
    bool hasnormal;
    bool hasheight;
    bool hasmetallic;
    bool hasroughness;
    bool hasao;
    void setAlbedo(std::string path);
    void setNormal(std::string path);
    void setHeight(std::string path);
    void setMetallic(std::string path);
    void setRoughness(std::string path);
    void setAO(std::string path);
    void Activate(Shader& shader);
};
#endif