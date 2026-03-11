#ifndef POSTPROCESSINGSHADER_CLASS_H
#define POSTPROCESSINGSHADER_CLASS_H
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <string>
#include "shaderClass.h"
#include "Window.h"
#include "Camera.h"
class UI;
class ShadowShader;
class PostProcessingShader {
public:
    Shader ssgiShader;
    Shader blurShader;
    Shader ssaoShader;
    Shader bloomThresholdShader;
    Shader bloomBlurShader;
    Shader ssgiSeparableBlurShader;
    Shader ssaoBlurShader;
    Shader rcRaycastShader;
    Shader rcCollapseShader;
    unsigned int rcCascadeArray;
    unsigned int rcMergedTexture;
    int numCascades = 4;
        Shader taaShader;
    Shader sharpenShader; 
    Shader volInjectShader;
    Shader volAccumulateShader;
    unsigned int volumeTexture;
    unsigned int accumVolumeTexture;
    unsigned int compositeFBO, compositeTexture;     unsigned int taaFBO[2];                          unsigned int taaTexture[2];                      int taaFrame = 0;                            
        unsigned int bloomThresholdFBO, bloomThresholdTexture;
    unsigned int ssgiBlurFBO[2], ssgiBlurTextures[2];
    unsigned int ssaoBlurFBO, ssaoBlurTexture;
    unsigned int pingpongFBO[2], pingpongColorbuffers[2];
        unsigned int samples = 8;
    unsigned int rectVAO, rectVBO;
    unsigned int FBO, RBO;     unsigned int framebufferTextureMS, normalTextureMS, positionTextureMS, gNormalTexMS, gAlbedoTexMS;
        unsigned int resolveFBO;
    unsigned int postProcessingTexture, postNormalTexture, postPositionTexture, gNormalTexResolved, gAlbedoTexResolved, postDepthTexture;
    unsigned int ssgiFBO, ssgiTexture;
    unsigned int ssaoFBO, ssaoTexture;
    unsigned int ssaoNoiseTexture;
        Shader hiZShader;
    unsigned int hiZTexture;
    int hiZMipMapCount;
    std::vector<glm::vec3> ssaoKernel;
    PostProcessingShader(Window& window);
    void Bind(Window& window);
    void Update(Window& window, float crntTime, Camera& camera, UI& ui, glm::vec3 sunDir, unsigned int uboLights, ShadowShader& shadowshader
        );
};
#endif