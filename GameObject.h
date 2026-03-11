#ifndef GAMEOBJECT_CLASS_H
#define GAMEOBJECT_CLASS_H
#include "Mesh.h"
#include "Light.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include <string>
#include <vector>
class GameObject {
public:
    GameObject() {}
    std::string name = "GameObject";
        bool hasMesh = false;
    bool hasLight = false;
        std::string modelPath = "";
    std::vector<std::string> materialPaths;
    bool isStatic = false;
    bool isVisible = true;
    bool castShadow = true;
        int parentID = -1;              std::vector<int> children;  
    std::vector<std::string> tags;
    std::vector<std::string> layers;
    Transform transform;
    MeshRenderer renderer;
    Light light;
};
#endif