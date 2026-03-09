#ifndef POSTPROCESSINGSHADER_CLASS_H
#define POSTPROCESSINGSHADER_CLASS_H
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <random>
#include <string>
#include "shaderClass.h"
#include "Window.h"
#include "Camera.h"
class UI;
class PostProcessingShader {
public:
    Shader ssgiShader;
    Shader blurShader;
    Shader ssaoShader;
    Shader bloomThresholdShader;
    unsigned int bloomThresholdFBO;
    unsigned int bloomThresholdTexture;
    Shader bloomBlurShader;
    Shader ssgiSeparableBlurShader;
    unsigned int ssgiBlurFBO[2], ssgiBlurTextures[2];
    unsigned int ssaoBlurFBO;
    unsigned int ssaoBlurTexture;
    Shader ssaoBlurShader;
    unsigned int pingpongFBO[2];
    unsigned int pingpongColorbuffers[2];
    unsigned int samples = 8;
    unsigned int rectVAO, rectVBO;
    unsigned int FBO, RBO;
    unsigned int framebufferTexture, normalTextureMSAA, positionTextureMSAA;
    unsigned int postProcessingFBO;
    unsigned int postProcessingTexture, postNormalTexture, postPositionTexture;
    unsigned int ssgiFBO, ssgiTexture;
    unsigned int ssaoFBO, ssaoTexture;
    unsigned int ssaoNoiseTexture;
    unsigned int postDepthTexture; // <--- ДОБАВЛЯЕМ СЮДА

    // --- НОВОЕ ДЛЯ HI-Z ---
    Shader hiZShader;
    unsigned int hiZTexture;
    int hiZMipMapCount;

    std::vector<glm::vec3> ssaoKernel;
    PostProcessingShader(Window& window);
    void Bind(Window& window);
    void Update(Window& window, float crntTime, Camera& camera, UI& ui, glm::vec3 sunDir);
};
#endif 