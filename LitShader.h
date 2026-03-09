#ifndef LITSHADER_CLASS_H
#define LITSHADER_CLASS_H

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

class LitShader {
public:
    Shader shader;
    const int noiseSize = 4;
    GLuint noiseTexture;

    void generateNoiseTexture() {
        std::vector<glm::vec3> noiseData(noiseSize * noiseSize);
        for (int i = 0; i < noiseSize * noiseSize; i++) {
            noiseData[i] = glm::normalize(glm::sphericalRand(1.0f));
        }
        glGenTextures(1, &noiseTexture);
        glBindTexture(GL_TEXTURE_2D, noiseTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, noiseSize, noiseSize, 0, GL_RGB, GL_FLOAT, noiseData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    LitShader() {
        shader = Shader("default.vert", "default.frag");
        generateNoiseTexture();
    }

    void Update(std::vector<GameObject>& objects, Camera& camera,
        unsigned int shadowAtlasTex,
        std::vector<glm::mat4>& boneTransforms, unsigned int uboLights)
    {
        shader.Activate();

        unsigned int lightIndex = glGetUniformBlockIndex(shader.ID, "LightBlock");
        glUniformBlockBinding(shader.ID, lightIndex, 0);

        // 1. Создаем локальную копию нашего чемодана
        LightUBOBlock lightBlock;
        lightBlock.activeLightsCount = 0;

        // 2. Упаковываем АБСОЛЮТНО ВСЕ данные ламп в один пакет
        for (auto& obj : objects) {
            if (obj.light.enable && obj.light.type != LightType::None) {

                int i = lightBlock.activeLightsCount;

                // Позиция и тип (0=Dir, 1=Point, 2=Spot, 3=Rect, 4=Sky)
                float lightType = (float)obj.light.type;
                lightBlock.lights[i].posType = glm::vec4(obj.transform.position, lightType);

                // Цвет и яркость
                lightBlock.lights[i].colorInt = glm::vec4(obj.light.color, obj.light.intensity);

                // Направление (через матрицы вращения, как у тебя было)
                glm::mat4 rotMat(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.z), glm::vec3(0, 0, 1));
                rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.y), glm::vec3(0, 1, 0));
                rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.x), glm::vec3(1, 0, 0));
                glm::vec3 direction = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));

                // Направление и радиус
                lightBlock.lights[i].dirRadius = glm::vec4(direction, obj.light.radius);

                // --- ПАРАМЕТРЫ ТЕНЕЙ (Упаковываем в один vec4) ---
                float inner = glm::cos(glm::radians(obj.light.innerCone));
                float outer = glm::cos(glm::radians(obj.light.outerCone));
                float castsShadows = obj.light.castShadows ? 1.0f : 0.0f;
                float shadowSlot = (float)obj.light.shadowSlot;

                lightBlock.lights[i].shadowParams = glm::vec4(inner, outer, castsShadows, shadowSlot);

                // Матрица для теневого атласа
                lightBlock.lights[i].lightSpaceMatrix = obj.light.lightSpaceMatrix;

                lightBlock.activeLightsCount++;

                // Защита от переполнения (ровно 100 ламп)
                if (lightBlock.activeLightsCount >= 100) break;
            }
        }

        // 3. ОТПРАВЛЯЕМ ВЕСЬ ПАКЕТ НА ВИДЕОКАРТУ ОДНИМ БРОСКОМ!
        glBindBuffer(GL_UNIFORM_BUFFER, uboLights);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightUBOBlock), &lightBlock);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboLights);

        // 4. Обычные uniform-переменные (Камера, Атлас, Шум)
        glUniform3f(glGetUniformLocation(shader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
        camera.Matrix(shader, "camMatrix");
        glUniform1f(glGetUniformLocation(shader.ID, "farPlane"), 100.0f);

        glUniform1f(glGetUniformLocation(shader.ID, "atlasResolution"), 8192.0f);
        glUniform1f(glGetUniformLocation(shader.ID, "tileSize"), 2048.0f);

        // Привязываем текстуры
        glActiveTexture(GL_TEXTURE0 + 12);
        glBindTexture(GL_TEXTURE_2D, shadowAtlasTex);
        glUniform1i(glGetUniformLocation(shader.ID, "shadowAtlas"), 12);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, noiseTexture);
        glUniform1i(glGetUniformLocation(shader.ID, "noiseTexture"), 0);

        if (!boneTransforms.empty()) {
            glUniformMatrix4fv(glGetUniformLocation(shader.ID, "finalBonesMatrices"), boneTransforms.size(), GL_FALSE, glm::value_ptr(boneTransforms[0]));
        }
    }
};
#endif