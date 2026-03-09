#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp> // Подключаем скачанный заголовочный файл
#include "GameObject.h"
#include "Material.h"
#include "Model.h"
// Для удобства
using json = nlohmann::json;
class Serializer {
private:
    // Наши склады. Они статичные, чтобы память сохранялась на всё время работы сцены
public:
    // ==========================================
    // РАБОТА С МАТЕРИАЛАМИ (.bhmat)
    // ==========================================
    static std::unordered_map<std::string, Model*> loadedModels;
    static std::unordered_map<std::string, Material*> loadedMaterials;
    static void SaveMaterial(const std::string& filepath, Material* material, const std::string& matName) {
        json j;
        j["name"] = matName;
        // Сюда мы запишем пути к текстурам. 
        // В идеале в классе Material нужно хранить эти пути (std::string albedoPath), 
        // чтобы их можно было оттуда достать.
        j["textures"]["albedo"] = "path_to_albedo.png"; // Замени на реальные переменные путей из твоего Material
        std::ofstream file(filepath);
        file << j.dump(4); // 4 - это количество пробелов для красивого форматирования
    }
    static Material* LoadMaterial(const std::string& filepath, const std::string& basePath) {
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
                return mat; // Возвращаем пустую сцену, чтобы движок не закрывался
            }   
            if (j.contains("textures")) {
                // Склеиваем путь папки проекта и путь до картинки
                if (j["textures"].contains("albedo")) {
                    std::string fullTexPath = basePath + "/" + j["textures"]["albedo"].get<std::string>();
                    mat->setAlbedo(fullTexPath.c_str());
                }
                if (j["textures"].contains("normal")) {
                    std::string fullTexPath = basePath + "/" + j["textures"]["normal"].get<std::string>();
                    mat->setNormal(fullTexPath.c_str());
                }
                if (j["textures"].contains("height")) {
                    std::string fullTexPath = basePath + "/" + j["textures"]["height"].get<std::string>();
                    mat->setHeight(fullTexPath.c_str());
                }
                if (j["textures"].contains("metallic")) {
                    std::string fullTexPath = basePath + "/" + j["textures"]["metallic"].get<std::string>();
                    mat->setMetallic(fullTexPath.c_str());
                }
                if (j["textures"].contains("roughness")) {
                    std::string fullTexPath = basePath + "/" + j["textures"]["roughness"].get<std::string>();
                    mat->setRoughness(fullTexPath.c_str());
                }
                if (j["textures"].contains("ao")) {
                    std::string fullTexPath = basePath + "/" + j["textures"]["ao"].get<std::string>();
                    mat->setAO(fullTexPath.c_str());
                }
            }
        }
        else {
            std::cout << "Ой, не удалось найти материал: " << filepath << std::endl;
        }
        loadedMaterials[filepath] = mat;
        return mat;
    }
    // ==========================================
    // ЗАГРУЗКА СЦЕНЫ (.bhscene)
    // ==========================================
    // ==========================================
    // РАБОТА СО СЦЕНОЙ (.bhscene)
    // ==========================================
    static void SaveScene(const std::string& filepath, const std::vector<GameObject>& objects) {
        json sceneJson;
        sceneJson["scene_name"] = "Saved Scene";
        sceneJson["objects"] = json::array();
        for (const auto& obj : objects) {
            // --- ЗАЩИТА 1: Не сохраняем пустышки и жестко закодированные лампочки ---
            if (obj.modelPath.empty()) continue;
            json objJson;
            objJson["name"] = obj.name.empty() ? "GameObject" : obj.name;
            objJson["model_path"] = obj.modelPath;
            // Сохраняем массив материалов
            objJson["materials"] = json::array();
            for (const auto& matPath : obj.materialPaths) {
                objJson["materials"].push_back(matPath);
            }
            // Сохраняем координаты
            objJson["transform"]["position"] = { obj.transform.position.x, obj.transform.position.y, obj.transform.position.z };
            objJson["transform"]["rotation"] = { obj.transform.rotation.x, obj.transform.rotation.y, obj.transform.rotation.z };
            objJson["transform"]["scale"] = { obj.transform.scale.x, obj.transform.scale.y, obj.transform.scale.z };
            sceneJson["objects"].push_back(objJson);
        }
        std::ofstream file(filepath);
        file << sceneJson.dump(4);
        std::cout << "Сцена успешно сохранена в: " << filepath << std::endl;
    }
    static std::vector<GameObject> LoadScene(const std::string& filepath, const std::string& basePath) {
        std::vector<GameObject> objects;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cout << "Не могу открыть файл сцены: " << filepath << std::endl;
            return objects;
        }
        json sceneJson;
        try {
            file >> sceneJson;
        }
        catch (json::parse_error& e) {
            std::cout << "ОШИБКА ЧТЕНИЯ JSON в файле " << filepath << "\n";
            std::cout << "Подробности: " << e.what() << "\n";
            return objects;
        }
        for (const auto& objJson : sceneJson["objects"]) {
            GameObject obj;
            if (objJson.contains("name")) obj.name = objJson["name"];
            // 1. Читаем координаты (ПРАВИЛЬНО ИЗВЛЕКАЕМ FLOAT ДЛЯ GLM)
            if (objJson.contains("transform")) {
                auto pos = objJson["transform"]["position"];
                obj.transform.position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
                auto rot = objJson["transform"]["rotation"];
                obj.transform.rotation = glm::vec3(rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>());
                auto scale = objJson["transform"]["scale"];
                obj.transform.scale = glm::vec3(scale[0].get<float>(), scale[1].get<float>(), scale[2].get<float>());
                obj.transform.updatematrix = true;
            }
            // 2. ЗАЩИТА 2: Если в JSON закралась пустышка - игнорируем её
            std::string relativeModelPath = objJson.contains("model_path") ? objJson["model_path"].get<std::string>() : "";
            if (relativeModelPath.empty()) continue;
            // СКЛЕИВАЕМ ПУТЬ К МОДЕЛИ
            std::string fullModelPath = basePath + "/" + relativeModelPath;
            if (loadedModels.find(fullModelPath) == loadedModels.end()) {
                loadedModels[fullModelPath] = new Model(fullModelPath);
            }
            Model* model = loadedModels[fullModelPath];
            // 3. СКЛЕИВАЕМ ПУТИ К МАТЕРИАЛАМ
            std::vector<std::string> matPaths = objJson["materials"];
            for (int i = 0; i < model->meshes.size(); i++) {
                std::string relativeMatPath = (i < matPaths.size()) ? matPaths[i] : matPaths[0];
                std::string fullMatPath = basePath + "/" + relativeMatPath;
                Material* mat = LoadMaterial(fullMatPath, basePath);
                obj.renderer.AddSubMesh(&model->meshes[i], mat);
            }
            obj.modelPath = relativeModelPath;
            obj.materialPaths = matPaths;
            objects.push_back(obj);
        }
        return objects;
    }
};
inline std::unordered_map<std::string, Model*> Serializer::loadedModels;
inline std::unordered_map<std::string, Material*> Serializer::loadedMaterials;