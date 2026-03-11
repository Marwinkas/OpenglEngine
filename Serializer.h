#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp> 
#include "GameObject.h"
#include "Material.h"
#include "Model.h"
using json = nlohmann::json;
class Serializer {
private:
    public:
                static std::unordered_map<std::string, Model*> loadedModels;
    static std::unordered_map<std::string, Material*> loadedMaterials;
    static void SaveMaterial(const std::string& filepath, Material* material, const std::string& matName) {
        json j;
        j["name"] = matName;
                                j["textures"]["albedo"] = "path_to_albedo.png";         std::ofstream file(filepath);
        file << j.dump(4);     }
    static Material* LoadMaterial(const std::string& filepath, const std::string& basePath) {
        // 1. Проверяем кэш, чтобы не загружать один и тот же файл дважды
        if (loadedMaterials.find(filepath) != loadedMaterials.end()) {
            return loadedMaterials[filepath];
        }

        Material* mat = new Material();
        std::ifstream file(filepath);

        if (file.is_open()) {
            json j;
            try {
                file >> j;
            }
            catch (json::parse_error& e) {
                std::cout << "ОШИБКА ЧТЕНИЯ JSON в файле " << filepath << "\n";
                std::cout << "Подробности: " << e.what() << "\n";
                return mat;
            }

            // --- Загрузка текстур с проверкой на пустоту ---
            if (j.contains("textures")) {
                auto& tex = j["textures"];

                // Лямбда-помощник, чтобы не дублировать код для каждой карты
                auto loadIfNotEmpty = [&](const std::string& key, void (Material::* func)(std::string)) {
                    if (tex.contains(key)) {
                        std::string path = tex[key].get<std::string>();
                        if (!path.empty()) {
                            std::string fullTexPath = basePath + "/" + path;
                            (mat->*func)(fullTexPath);
                        }
                    }
                    };

                loadIfNotEmpty("albedo", &Material::setAlbedo);
                loadIfNotEmpty("normal", &Material::setNormal);
                loadIfNotEmpty("height", &Material::setHeight);
                loadIfNotEmpty("metallic", &Material::setMetallic);
                loadIfNotEmpty("roughness", &Material::setRoughness);
                loadIfNotEmpty("ao", &Material::setAO);
            }
        }
        else {
            std::cout << "Ой, не удалось найти файл материала: " << filepath << std::endl;
        }

        // Сохраняем в кэш и возвращаем
        loadedMaterials[filepath] = mat;
        return mat;
    }
    static void SaveScene(const std::string& filepath, const std::vector<GameObject>& objects) {
        json sceneJson;
        sceneJson["scene_name"] = "Saved Scene";
        sceneJson["objects"] = json::array();

        for (const auto& obj : objects) {
            json objJson;
            objJson["name"] = obj.name.empty() ? "GameObject" : obj.name;
            objJson["parentID"] = obj.parentID;
            objJson["hasMesh"] = obj.hasMesh;
            objJson["hasLight"] = obj.hasLight;

            // --- ТРАНСФОРМ ---
            objJson["transform"]["position"] = { obj.transform.position.x, obj.transform.position.y, obj.transform.position.z };
            objJson["transform"]["rotation"] = { obj.transform.rotation.x, obj.transform.rotation.y, obj.transform.rotation.z };
            objJson["transform"]["scale"] = { obj.transform.scale.x, obj.transform.scale.y, obj.transform.scale.z };

            // --- МЕШ ---
            if (obj.hasMesh) {
                objJson["isStatic"] = obj.isStatic;
                objJson["isVisible"] = obj.isVisible;
                objJson["castShadow"] = obj.castShadow;
                objJson["model_path"] = obj.modelPath;
                objJson["materials"] = json::array();
                for (const auto& matPath : obj.materialPaths) objJson["materials"].push_back(matPath);
            }

            // --- СВЕТ ---
            if (obj.hasLight) {
                objJson["light"]["enable"] = obj.light.enable;
                objJson["light"]["type"] = (int)obj.light.type;
                objJson["light"]["mobility"] = (int)obj.light.mobility;
                objJson["light"]["color"] = { obj.light.color.x, obj.light.color.y, obj.light.color.z };
                objJson["light"]["intensity"] = obj.light.intensity;
                objJson["light"]["radius"] = obj.light.radius;
                objJson["light"]["innerCone"] = obj.light.innerCone;
                objJson["light"]["outerCone"] = obj.light.outerCone;
                objJson["light"]["castShadows"] = obj.light.castShadows;
            }

  
            sceneJson["objects"].push_back(objJson);
        }
        std::ofstream file(filepath);
        file << sceneJson.dump(4);
    }

    static std::vector<GameObject> LoadScene(const std::string& filepath, const std::string& basePath) {
        std::vector<GameObject> objects;
        std::ifstream file(filepath);
        if (!file.is_open()) return objects;

        json sceneJson;
        try { file >> sceneJson; }
        catch (...) { return objects; }

        for (const auto& objJson : sceneJson["objects"]) {
            GameObject obj;
            if (objJson.contains("name")) obj.name = objJson["name"];
            if (objJson.contains("parentID")) obj.parentID = objJson["parentID"];
            if (objJson.contains("hasMesh")) obj.hasMesh = objJson["hasMesh"];
            if (objJson.contains("hasLight")) obj.hasLight = objJson["hasLight"];

            if (objJson.contains("transform")) {
                auto pos = objJson["transform"]["position"];
                obj.transform.position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
                auto rot = objJson["transform"]["rotation"];
                obj.transform.rotation = glm::vec3(rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>());
                auto scale = objJson["transform"]["scale"];
                obj.transform.scale = glm::vec3(scale[0].get<float>(), scale[1].get<float>(), scale[2].get<float>());
                obj.transform.updatematrix = true;
            }

            if (obj.hasMesh) {
                if (objJson.contains("isStatic")) obj.isStatic = objJson["isStatic"];
                if (objJson.contains("isVisible")) obj.isVisible = objJson["isVisible"];
                if (objJson.contains("castShadow")) obj.castShadow = objJson["castShadow"];
                std::string relativeModelPath = objJson.contains("model_path") ? objJson["model_path"].get<std::string>() : "";

                if (!relativeModelPath.empty()) {
                    std::string fullModelPath = basePath + "/" + relativeModelPath;
                    if (loadedModels.find(fullModelPath) == loadedModels.end()) loadedModels[fullModelPath] = new Model(fullModelPath, basePath);
                    Model* model = loadedModels[fullModelPath];
                    std::vector<std::string> matPaths;
                    if (objJson.contains("materials")) matPaths = objJson["materials"].get<std::vector<std::string>>();

                    for (int i = 0; i < model->meshes.size(); i++) {
                        std::string relativeMatPath = (i < matPaths.size()) ? matPaths[i] : (matPaths.empty() ? "" : matPaths[0]);
                        Material* mat = nullptr;
                        if (!relativeMatPath.empty()) mat = LoadMaterial(basePath + "/" + relativeMatPath, basePath);
                        else mat = new Material();
                        obj.renderer.AddSubMesh(&model->meshes[i], mat);
                    }
                    obj.modelPath = relativeModelPath;
                    obj.materialPaths = matPaths;
                }
            }

            if (obj.hasLight && objJson.contains("light")) {
                obj.light.enable = objJson["light"]["enable"];
                obj.light.type = (LightType)objJson["light"]["type"].get<int>();
                obj.light.mobility = (LightMobility)objJson["light"]["mobility"].get<int>();
                auto c = objJson["light"]["color"];
                obj.light.color = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
                obj.light.intensity = objJson["light"]["intensity"];
                obj.light.radius = objJson["light"]["radius"];
                obj.light.innerCone = objJson["light"]["innerCone"];
                obj.light.outerCone = objJson["light"]["outerCone"];
                obj.light.castShadows = objJson["light"]["castShadows"];
                obj.light.needsShadowUpdate = true;
            }

            objects.push_back(obj);
        }

        // Восстановление иерархии (parent/child)
        for (int i = 0; i < objects.size(); ++i) {
            int pID = objects[i].parentID;
            if (pID >= 0 && pID < objects.size()) objects[pID].children.push_back(i);
        }

        return objects;
    }
};
inline std::unordered_map<std::string, Model*> Serializer::loadedModels;
inline std::unordered_map<std::string, Material*> Serializer::loadedMaterials;