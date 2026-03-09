#ifndef POSTPROCESSINGSHADER_CLASS_H
#define POSTPROCESSINGSHADER_CLASS_H

#include <glm/glm.hpp>
#include <glad/glad.h>
#include <vector>
#include "shaderClass.h"
#include "Window.h"
#include "Camera.h"

class UI;

class PostProcessingShader {
public:
    // --- Шейдеры этапов ---
    Shader ssaoShader;
    Shader ssgiShader;
    Shader combineShader;    // Объединение света, теней и тумана
    Shader cameraFxShader;   // DoF
    Shader bloomThresholdShader;
    Shader bloomBlurShader;
    Shader finalShader;      // Тонмаппинг, цветокоррекция, зерно

    // --- Геометрия (Fullscreen Quad) ---
    unsigned int rectVAO, rectVBO;

    // --- Буферы (FBO) и текстуры ---
    // 1. Основной MSAA буфер (сюда рисуем сцену)
    unsigned int FBO, RBO;
    unsigned int framebufferTexture, normalTextureMSAA, positionTextureMSAA;

    // 2. Промежуточный G-Buffer (после Resolve MSAA)
    unsigned int postProcessingFBO;
    unsigned int postProcessingTexture, postNormalTexture, postPositionTexture;

    // 3. Эффекты освещения
    unsigned int ssaoFBO, ssaoTexture, ssaoNoiseTexture;
    std::vector<glm::vec3> ssaoKernel;
    unsigned int ssgiFBO, ssgiTexture;

    // 4. Композитинг и камера
    unsigned int combineFBO, combineTexture;
    unsigned int cameraFxFBO, cameraFxTexture;

    // 5. Блум (Пинг-понг)
    unsigned int bloomThresholdFBO, bloomThresholdTexture;
    unsigned int pingpongFBO[2];
    unsigned int pingpongColorbuffers[2];

    // Настройки
    unsigned int samples = 8;

    PostProcessingShader(Window& window);
    void Bind(Window& window);
    void Update(Window& window, float crntTime, Camera& camera, UI& ui);
};
#endif