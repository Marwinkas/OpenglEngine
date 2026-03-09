#ifndef CULLINGSHADER_CLASS_H
#define CULLINGSHADER_CLASS_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>
#include <vector>
#include <string>
#include <array>
#include "shaderClass.h"
#include "Window.h"
#include "GameObject.h"
#include "Camera.h"

// 1. Структуры данных (должны совпадать с шейдером!)
struct BoundingSphere {
    glm::vec4 posAndRadius;
};

struct DrawCommand {
    unsigned int count;
    unsigned int instanceCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    unsigned int baseInstance;
};

struct Plane {
    glm::vec3 normal;
    float distance;
    void Normalize() {
        float mag = glm::length(normal);
        normal /= mag;
        distance /= mag;
    }
};

class CullingShader {
public:
    Shader shader;
    GLuint ssboObjects, ssboCommands; // Наши гигантские буферы

    CullingShader() {
        shader = Shader("culling.comp");

        // Создаем два SSBO буфера
        glGenBuffers(1, &ssboObjects);
        glGenBuffers(1, &ssboCommands);
    }

    // Функция извлечения плоскостей камеры
    std::array<Plane, 6> ExtractFrustumPlanes(const glm::mat4& vpMatrix) {
        std::array<Plane, 6> planes;
        // Левая
        planes[0].normal.x = vpMatrix[0][3] + vpMatrix[0][0]; planes[0].normal.y = vpMatrix[1][3] + vpMatrix[1][0]; planes[0].normal.z = vpMatrix[2][3] + vpMatrix[2][0]; planes[0].distance = vpMatrix[3][3] + vpMatrix[3][0];
        // Правая
        planes[1].normal.x = vpMatrix[0][3] - vpMatrix[0][0]; planes[1].normal.y = vpMatrix[1][3] - vpMatrix[1][0]; planes[1].normal.z = vpMatrix[2][3] - vpMatrix[2][0]; planes[1].distance = vpMatrix[3][3] - vpMatrix[3][0];
        // Нижняя
        planes[2].normal.x = vpMatrix[0][3] + vpMatrix[0][1]; planes[2].normal.y = vpMatrix[1][3] + vpMatrix[1][1]; planes[2].normal.z = vpMatrix[2][3] + vpMatrix[2][1]; planes[2].distance = vpMatrix[3][3] + vpMatrix[3][1];
        // Верхняя
        planes[3].normal.x = vpMatrix[0][3] - vpMatrix[0][1]; planes[3].normal.y = vpMatrix[1][3] - vpMatrix[1][1]; planes[3].normal.z = vpMatrix[2][3] - vpMatrix[2][1]; planes[3].distance = vpMatrix[3][3] - vpMatrix[3][1];
        // Ближняя
        planes[4].normal.x = vpMatrix[0][3] + vpMatrix[0][2]; planes[4].normal.y = vpMatrix[1][3] + vpMatrix[1][2]; planes[4].normal.z = vpMatrix[2][3] + vpMatrix[2][2]; planes[4].distance = vpMatrix[3][3] + vpMatrix[3][2];
        // Дальняя
        planes[5].normal.x = vpMatrix[0][3] - vpMatrix[0][2]; planes[5].normal.y = vpMatrix[1][3] - vpMatrix[1][2]; planes[5].normal.z = vpMatrix[2][3] - vpMatrix[2][2]; planes[5].distance = vpMatrix[3][3] - vpMatrix[3][2];

        for (int i = 0; i < 6; i++) planes[i].Normalize();
        return planes;
    }

    void Update(std::vector<GameObject>& objects, Camera& camera)
    {
        if (objects.empty()) return;

        shader.Activate();

        // 1. Собираем данные в векторы
        std::vector<BoundingSphere> spheres(objects.size());
        std::vector<DrawCommand> commands(objects.size());

        for (size_t i = 0; i < objects.size(); i++) {
            // Убедись, что у тебя в GameObject есть переменная для радиуса (например boundingRadius)
            float radius = 10.0f; // Замени на objects[i].boundingRadius
            spheres[i].posAndRadius = glm::vec4(objects[i].transform.position, radius);

            // ЗАМЕНИ objects[i].indices.size() на то, как ты получаешь количество индексов меша!
            commands[i].count = 36; // <-- ВАЖНО: Тут должно быть количество индексов модели!
            commands[i].instanceCount = 0; // Изначально 0. Шейдер сам поставит 1, если увидит объект
            commands[i].firstIndex = 0;
            commands[i].baseVertex = 0;
            commands[i].baseInstance = 0;
        }

        // 2. Отправляем сферы в буфер (binding = 0)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboObjects);
        glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(BoundingSphere), spheres.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboObjects);

        // 3. Отправляем пустые команды в буфер (binding = 1)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboCommands);
        glBufferData(GL_SHADER_STORAGE_BUFFER, commands.size() * sizeof(DrawCommand), commands.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboCommands);

        // 4. Передаем Uniforms (Плоскости камеры)
        auto planes = ExtractFrustumPlanes(camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f) * camera.GetViewMatrix());
        for (int i = 0; i < 6; i++) {
            std::string name = "frustumPlanes[" + std::to_string(i) + "]";
            glUniform4f(glGetUniformLocation(shader.ID, name.c_str()), planes[i].normal.x, planes[i].normal.y, planes[i].normal.z, planes[i].distance);
        }
        glUniform1ui(glGetUniformLocation(shader.ID, "objectCount"), objects.size());

        // 5. МАГИЯ ВЫЧИСЛЕНИЙ! Запускаем ядра RTX 3090.
        // Делим на 256, потому что в шейдере мы указали local_size_x = 256
        GLuint workGroups = (objects.size() + 255) / 256;
        glDispatchCompute(workGroups, 1, 1);

        // 6. БАРЬЕР ПАМЯТИ: Запрещаем видеокарте рисовать картинку, пока Compute Shader не закончит считать!
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }
};
#endif