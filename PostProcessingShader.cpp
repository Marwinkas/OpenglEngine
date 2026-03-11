#include "PostProcessingShader.h"
#include "UI.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <random>
#include <string>
const float rectangleVerticesSSAO[] =
{
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f
};
PostProcessingShader::PostProcessingShader(Window& window) {
    ssgiShader = Shader("framebuffer.vert", "ssgi.frag");
    blurShader = Shader("framebuffer.vert", "blur.frag");
    ssaoShader = Shader("framebuffer.vert", "ssao.frag");
    bloomThresholdShader = Shader("framebuffer.vert", "bloom_threshold.frag");
    bloomBlurShader = Shader("framebuffer.vert", "bloom_blur.frag");
    ssaoBlurShader = Shader("framebuffer.vert", "ssao_blur.frag");
    ssgiSeparableBlurShader = Shader("framebuffer.vert", "ssgi_blur.frag");
    taaShader = Shader("framebuffer.vert", "taa.frag");     hiZShader = Shader("hiz.comp");
    sharpenShader = Shader("framebuffer.vert", "sharpen.frag"); 
    volInjectShader = Shader("volumetric_inject.comp");
    volAccumulateShader = Shader("volumetric_accumulate.comp");
    rcRaycastShader = Shader("rc_raycast.comp");
    rcCollapseShader = Shader("rc_collapse.comp");
    int numCascades = 4;
    int rcWidth = window.width / 2;
    int rcHeight = window.height / 2;
        glGenTextures(1, &rcCascadeArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, rcCascadeArray);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA16F, rcWidth, rcHeight, numCascades);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenTextures(1, &rcMergedTexture);
    glBindTexture(GL_TEXTURE_2D, rcMergedTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, rcWidth, rcHeight);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenTextures(1, &volumeTexture);
    glBindTexture(GL_TEXTURE_3D, volumeTexture);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, 160, 90, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glGenTextures(1, &accumVolumeTexture);
    glBindTexture(GL_TEXTURE_3D, accumVolumeTexture);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, 160, 90, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;
    for (unsigned int i = 0; i < 64; ++i) {
        glm::vec3 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = (float)i / 64.0f;
        scale = 0.1f + scale * scale * (1.0f - 0.1f);
        ssaoKernel.push_back(sample * scale);
    }
    std::vector<glm::vec3> ssaoNoise;
    for (unsigned int i = 0; i < 16; i++) {
        ssaoNoise.push_back(glm::vec3(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f));
    }
    glGenTextures(1, &ssaoNoiseTexture);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenVertexArrays(1, &rectVAO);
    glGenBuffers(1, &rectVBO);
    glBindVertexArray(rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectangleVerticesSSAO), &rectangleVerticesSSAO, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glGenFramebuffers(1, &FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    auto createMS = [&](unsigned int& tex, GLenum attach) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGB16F, window.width, window.height, GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D_MULTISAMPLE, tex, 0);
        };
    createMS(framebufferTextureMS, GL_COLOR_ATTACHMENT0);
    createMS(normalTextureMS, GL_COLOR_ATTACHMENT1);
    createMS(positionTextureMS, GL_COLOR_ATTACHMENT2);
    createMS(gNormalTexMS, GL_COLOR_ATTACHMENT3);
    createMS(gAlbedoTexMS, GL_COLOR_ATTACHMENT4);
    unsigned int msAttachments[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
    glDrawBuffers(5, msAttachments);
    glGenRenderbuffers(1, &RBO);
    glBindRenderbuffer(GL_RENDERBUFFER, RBO);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, window.width, window.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, RBO);
    glGenFramebuffers(1, &resolveFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO);
    auto createFlatTex = [&](unsigned int& tex, GLenum attach, bool mipmaps) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, window.width, window.height, 0, GL_RGBA, GL_FLOAT, NULL);
        if (mipmaps) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glGenerateMipmap(GL_TEXTURE_2D); }
        else { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D, tex, 0);
        };
    createFlatTex(postProcessingTexture, GL_COLOR_ATTACHMENT0, true);
    createFlatTex(postNormalTexture, GL_COLOR_ATTACHMENT1, false);
    createFlatTex(postPositionTexture, GL_COLOR_ATTACHMENT2, false);
    createFlatTex(gNormalTexResolved, GL_COLOR_ATTACHMENT3, false);
    createFlatTex(gAlbedoTexResolved, GL_COLOR_ATTACHMENT4, false);
    glGenTextures(1, &postDepthTexture);
    glBindTexture(GL_TEXTURE_2D, postDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, window.width, window.height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, postDepthTexture, 0);
    auto createFBO = [&](unsigned int& fbo, unsigned int& tex, int w, int h, GLenum format) {
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format == GL_RED ? GL_RED : GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        };
    createFBO(bloomThresholdFBO, bloomThresholdTexture, window.width / 4, window.height / 4, GL_RGB16F);
    glGenFramebuffers(2, pingpongFBO);
    glGenTextures(2, pingpongColorbuffers);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width / 4, window.height / 4, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorbuffers[i], 0);
    }
    createFBO(ssaoFBO, ssaoTexture, window.width / 2, window.height / 2, GL_RED);
    createFBO(ssaoBlurFBO, ssaoBlurTexture, window.width / 2, window.height / 2, GL_RED);
    createFBO(ssgiFBO, ssgiTexture, window.width, window.height, GL_RGB16F);
    glGenFramebuffers(2, ssgiBlurFBO);     glGenTextures(2, ssgiBlurTextures); 
    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, ssgiBlurFBO[i]);
        glBindTexture(GL_TEXTURE_2D, ssgiBlurTextures[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width / 2, window.height / 2, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssgiBlurTextures[i], 0);
    }
    hiZMipMapCount = (int)std::floor(std::log2(std::max(window.width, window.height))) + 1;
        glGenTextures(1, &hiZTexture);
    glBindTexture(GL_TEXTURE_2D, hiZTexture);
        glTexStorage2D(GL_TEXTURE_2D, hiZMipMapCount, GL_R32F, window.width, window.height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    createFBO(compositeFBO, compositeTexture, window.width, window.height, GL_RGB16F); 
    glGenFramebuffers(2, taaFBO);
    glGenTextures(2, taaTexture);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, taaFBO[i]);
        glBindTexture(GL_TEXTURE_2D, taaTexture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width, window.height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, taaTexture[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void PostProcessingShader::Bind(Window& window) {
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glViewport(0, 0, window.width, window.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}
static glm::mat4 lastViewProj = glm::mat4(1.0f);
void PostProcessingShader::Update(Window& window, float crntTime, Camera& camera, UI& ui, glm::vec3 sunDir, unsigned int uboLights, ShadowShader& shadowshader
) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, FBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);
    for (int i = 0; i < 3; ++i) {
        glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
        glBlitFramebuffer(0, 0, window.width, window.height, 0, 0, window.width, window.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBlitFramebuffer(0, 0, window.width, window.height, 0, 0, window.width, window.height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_DEPTH_TEST);
    hiZShader.Activate();
    for (int i = 0; i < hiZMipMapCount; ++i) {
                int mipWidth = std::max(1, window.width >> i);
        int mipHeight = std::max(1, window.height >> i);
        glActiveTexture(GL_TEXTURE0);
        if (i == 0) {
                        glBindTexture(GL_TEXTURE_2D, postDepthTexture);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "inMipLevel"), 0);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "copyOnly"), 1);
        }
        else {
                        glBindTexture(GL_TEXTURE_2D, hiZTexture);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "inMipLevel"), i - 1);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "copyOnly"), 0);
        }
        glUniform1i(glGetUniformLocation(hiZShader.ID, "inDepth"), 0);
                glBindImageTexture(0, hiZTexture, i, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
                GLuint groupsX = (mipWidth + 15) / 16;
        GLuint groupsY = (mipHeight + 15) / 16;
        glDispatchCompute(groupsX, groupsY, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
                int halfW = window.width / 2;
    int halfH = window.height / 2;
    glViewport(0, 0, halfW, halfH); 
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoShader.Activate();
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "enableSSAO"), ui.ppSettings.enableSSAO);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "radius"), ui.ppSettings.ssaoRadius);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "bias"), ui.ppSettings.ssaoBias);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "intensity"), ui.ppSettings.ssaoIntensity);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "power"), ui.ppSettings.ssaoPower);
            glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 projection = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);
        for (unsigned int i = 0; i < 64; ++i)
        glUniform3fv(glGetUniformLocation(ssaoShader.ID, ("samples[" + std::to_string(i) + "]").c_str()), 1, glm::value_ptr(ssaoKernel[i]));
        glUniformMatrix4fv(glGetUniformLocation(ssaoShader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(ssaoShader.ID, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniform2f(glGetUniformLocation(ssaoShader.ID, "noiseScale"), (float)halfW / 4.0f, (float)halfH / 4.0f);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, postNormalTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "normalTexture"), 0);
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "positionTexture"), 1);
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "texNoise"), 2);
    glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glViewport(0, 0, window.width / 2, window.height / 2);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoBlurShader.Activate();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoTexture);     glUniform1i(glGetUniformLocation(ssaoBlurShader.ID, "ssaoInput"), 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glUniform1i(glGetUniformLocation(ssaoBlurShader.ID, "positionTexture"), 1);
    
    bool horizontal = true, first_iteration = true;
    int amount = ui.ppSettings.bloomBlurIterations * 2;     if (ui.ppSettings.enableBloom) {
                glViewport(0, 0, window.width / 4, window.height / 4);
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        bloomThresholdShader.Activate();
        glUniform1f(glGetUniformLocation(bloomThresholdShader.ID, "threshold"), ui.ppSettings.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessingTexture);         glUniform1i(glGetUniformLocation(bloomThresholdShader.ID, "screenTexture"), 0);
        glBindVertexArray(rectVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
                bloomBlurShader.Activate();
        for (unsigned int i = 0; i < amount; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal]);
            glUniform1i(glGetUniformLocation(bloomBlurShader.ID, "horizontal"), horizontal);
            glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, first_iteration ? pingpongColorbuffers[0] : pingpongColorbuffers[!horizontal]);
            glUniform1i(glGetUniformLocation(bloomBlurShader.ID, "image"), 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            horizontal = !horizontal;
            first_iteration = false;
        }
        glViewport(0, 0, window.width, window.height);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glGenerateMipmap(GL_TEXTURE_2D);
    glViewport(0, 0, window.width, window.height);
                        if (ui.ppSettings.enableFog) {
                glm::mat4 cleanViewProj = camera.cleanViewProjectionMatrix;
        glm::mat4 invViewProj = glm::inverse(cleanViewProj);
                volInjectShader.Activate();
        glUniformMatrix4fv(glGetUniformLocation(volInjectShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
        glUniform3fv(glGetUniformLocation(volInjectShader.ID, "camPos"), 1, glm::value_ptr(camera.Position));
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "zNear"), 0.1f);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "zFar"), 100.0f);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "fogDensity"), ui.ppSettings.fogDensity);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "fogBaseHeight"), ui.ppSettings.fogBaseHeight);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "fogHeightFalloff"), ui.ppSettings.fogHeightFalloff);
        glUniform3fv(glGetUniformLocation(volInjectShader.ID, "fogColor"), 1, ui.ppSettings.fogColor);
                glActiveTexture(GL_TEXTURE0);
                glUniform1f(glGetUniformLocation(volInjectShader.ID, "atlasResolution"), 8192.0f);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "tileSize"), 2048.0f);
                glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboLights); 
                glBindImageTexture(0, volumeTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                glDispatchCompute(20, 12, 8);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                volAccumulateShader.Activate();
        glUniform1f(glGetUniformLocation(volAccumulateShader.ID, "zNear"), 0.1f);
        glUniform1f(glGetUniformLocation(volAccumulateShader.ID, "zFar"), 100.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, volumeTexture);         glUniform1i(glGetUniformLocation(volAccumulateShader.ID, "volumeTex"), 0);
        glBindImageTexture(0, accumVolumeTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute(20, 12, 1);         glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    glm::vec3 sunPos = -sunDir * 1000.0f;
    glm::vec4 clipSpacePos = camera.cleanViewProjectionMatrix * glm::vec4(sunPos, 1.0f);
    glm::vec3 ndcSpacePos = glm::vec3(clipSpacePos) / clipSpacePos.w;
    glm::vec2 sunScreenPos = (glm::vec2(ndcSpacePos.x, ndcSpacePos.y) + 1.0f) / 2.0f;
    float horizonFade = glm::clamp(-sunDir.y * 10.0f + 0.5f, 0.0f, 1.0f);
    float sunVisible = (clipSpacePos.w > 0.0f && clipSpacePos.z < clipSpacePos.w) ? horizonFade : 0.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, compositeFBO);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);     glClear(GL_COLOR_BUFFER_BIT);
    blurShader.Activate();
    glUniform1f(glGetUniformLocation(blurShader.ID, "time"), crntTime);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableContactShadows"), ui.ppSettings.enableContactShadows);
    glUniform1f(glGetUniformLocation(blurShader.ID, "contactShadowLength"), ui.ppSettings.contactShadowLength);
    glUniform1i(glGetUniformLocation(blurShader.ID, "contactShadowSteps"), ui.ppSettings.contactShadowSteps);
    glUniform1f(glGetUniformLocation(blurShader.ID, "contactShadowThickness"), ui.ppSettings.contactShadowThickness);

    glUniform1i(glGetUniformLocation(blurShader.ID, "enableMotionBlur"), ui.ppSettings.enableMotionBlur);
    glUniform1f(glGetUniformLocation(blurShader.ID, "mbStrength"), ui.ppSettings.mbStrength);
    glUniformMatrix4fv(glGetUniformLocation(blurShader.ID, "projectionMatrix"),1, GL_FALSE, glm::value_ptr(camera.GetViewProjectionMatrix()));
    glUniformMatrix4fv(glGetUniformLocation(blurShader.ID, "prevViewProj"), 1, GL_FALSE, glm::value_ptr(lastViewProj));
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableGodRays"), ui.ppSettings.enableGodRays);
    glUniform1f(glGetUniformLocation(blurShader.ID, "godRaysIntensity"), ui.ppSettings.godRaysIntensity * sunVisible);
    glUniform2f(glGetUniformLocation(blurShader.ID, "lightScreenPos"), sunScreenPos.x, sunScreenPos.y);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableFilmGrain"), ui.ppSettings.enableFilmGrain);
    glUniform1f(glGetUniformLocation(blurShader.ID, "grainIntensity"), ui.ppSettings.grainIntensity);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableSharpen"), ui.ppSettings.enableSharpen);
    glUniform1f(glGetUniformLocation(blurShader.ID, "sharpenIntensity"), ui.ppSettings.sharpenIntensity);
    glUniform1i(glGetUniformLocation(blurShader.ID, "blurRange"), ui.ppSettings.blurRange);
    glUniform1f(glGetUniformLocation(blurShader.ID, "gamma"), ui.ppSettings.gamma);
    glUniform1f(glGetUniformLocation(blurShader.ID, "currentExposure"), ui.ppSettings.currentExposure);
    glUniform1i(glGetUniformLocation(blurShader.ID, "autoExposure"), ui.ppSettings.autoExposure);
    glUniform1f(glGetUniformLocation(blurShader.ID, "manualExposure"), ui.ppSettings.manualExposure);
    glUniform1f(glGetUniformLocation(blurShader.ID, "exposureCompensation"), ui.ppSettings.exposureCompensation);
    glUniform1f(glGetUniformLocation(blurShader.ID, "minBrightness"), ui.ppSettings.minBrightness);
    glUniform1f(glGetUniformLocation(blurShader.ID, "maxBrightness"), ui.ppSettings.maxBrightness);
    glUniform1f(glGetUniformLocation(blurShader.ID, "contrast"), ui.ppSettings.contrast);
    glUniform1f(glGetUniformLocation(blurShader.ID, "saturation"), ui.ppSettings.saturation);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableVignette"), ui.ppSettings.enableVignette);
    glUniform1f(glGetUniformLocation(blurShader.ID, "vignetteIntensity"), ui.ppSettings.vignetteIntensity);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableChromaticAberration"), ui.ppSettings.enableChromaticAberration);
    glUniform1f(glGetUniformLocation(blurShader.ID, "caIntensity"), ui.ppSettings.caIntensity);
    glUniform1f(glGetUniformLocation(blurShader.ID, "temperature"), ui.ppSettings.temperature);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableDoF"), ui.ppSettings.enableDoF);
    glUniform1f(glGetUniformLocation(blurShader.ID, "focusDistance"), ui.ppSettings.focusDistance);
    glUniform1f(glGetUniformLocation(blurShader.ID, "focusRange"), ui.ppSettings.focusRange);
    glUniform1f(glGetUniformLocation(blurShader.ID, "bokehSize"), ui.ppSettings.bokehSize);
    glUniform3f(glGetUniformLocation(blurShader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[!horizontal]);     glUniform1i(glGetUniformLocation(blurShader.ID, "bloomTexture"), 5);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableBloom"), ui.ppSettings.enableBloom);
    glUniform1f(glGetUniformLocation(blurShader.ID, "bloomIntensity"), ui.ppSettings.bloomIntensity);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableLensFlares"), ui.ppSettings.enableLensFlares);
    glUniform1f(glGetUniformLocation(blurShader.ID, "flareIntensity"), ui.ppSettings.flareIntensity);
    glUniform1f(glGetUniformLocation(blurShader.ID, "ghostDispersal"), ui.ppSettings.ghostDispersal);
    glUniform1i(glGetUniformLocation(blurShader.ID, "ghosts"), ui.ppSettings.ghosts);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableFog"), ui.ppSettings.enableFog);
    glUniform1f(glGetUniformLocation(blurShader.ID, "fogDensity"), ui.ppSettings.fogDensity);
    glUniform1f(glGetUniformLocation(blurShader.ID, "fogHeightFalloff"), ui.ppSettings.fogHeightFalloff);
    glUniform1f(glGetUniformLocation(blurShader.ID, "fogBaseHeight"), ui.ppSettings.fogBaseHeight);
    glUniform3fv(glGetUniformLocation(blurShader.ID, "fogColor"), 1, ui.ppSettings.fogColor);
    glUniform3fv(glGetUniformLocation(blurShader.ID, "inscatterColor"), 1, ui.ppSettings.inscatterColor);
    glUniform1f(glGetUniformLocation(blurShader.ID, "inscatterPower"), ui.ppSettings.inscatterPower);
    glUniform1f(glGetUniformLocation(blurShader.ID, "inscatterIntensity"), ui.ppSettings.inscatterIntensity);
    glUniform3f(glGetUniformLocation(blurShader.ID, "sunDirFog"), sunDir.x, sunDir.y, sunDir.z);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rcMergedTexture);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, postNormalTexture);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, ssaoBlurTexture);

    glUniform1i(glGetUniformLocation(blurShader.ID, "screenTexture"), 0);
    glUniform1i(glGetUniformLocation(blurShader.ID, "ssgiTexture"), 1);
    glUniform1i(glGetUniformLocation(blurShader.ID, "normalTexture"), 2);
    glUniform1i(glGetUniformLocation(blurShader.ID, "positionTexture"), 3);
    glUniform1i(glGetUniformLocation(blurShader.ID, "ssaoTexture"), 4);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, hiZTexture);
    glUniform1i(glGetUniformLocation(blurShader.ID, "hiZTexture"), 7);
    glUniform1i(glGetUniformLocation(blurShader.ID, "hiZMipCount"), hiZMipMapCount);
        if (ui.ppSettings.enableFog) {
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_3D, accumVolumeTexture);
        glUniform1i(glGetUniformLocation(blurShader.ID, "volumetricFogTex"), 6);
                glUniformMatrix4fv(glGetUniformLocation(blurShader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(camera.GetViewMatrix()));
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, taaFBO[taaFrame]);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);     glClear(GL_COLOR_BUFFER_BIT);
    taaShader.Activate();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, compositeTexture);           glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, taaTexture[1 - taaFrame]);     glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, postPositionTexture);    
    glUniform1i(glGetUniformLocation(taaShader.ID, "currentColor"), 0);
    glUniform1i(glGetUniformLocation(taaShader.ID, "historyColor"), 1);
    glUniform1i(glGetUniformLocation(taaShader.ID, "positionTexture"), 2);
        glUniformMatrix4fv(glGetUniformLocation(taaShader.ID, "prevViewProj"), 1, GL_FALSE, glm::value_ptr(lastViewProj));
        glUniformMatrix4fv(glGetUniformLocation(taaShader.ID, "currentCleanViewProj"), 1, GL_FALSE, glm::value_ptr(camera.cleanViewProjectionMatrix));
    glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);     glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    sharpenShader.Activate();
        glActiveTexture(GL_TEXTURE0); 
    glBindTexture(GL_TEXTURE_2D, taaTexture[taaFrame]); 
    glUniform1i(glGetUniformLocation(sharpenShader.ID, "screenTexture"), 0);
        float sharpness = ui.ppSettings.enableSharpen ? ui.ppSettings.sharpenIntensity : 0.0f;
    glUniform1f(glGetUniformLocation(sharpenShader.ID, "sharpenAmount"), sharpness);
        glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    lastViewProj = camera.cleanViewProjectionMatrix;
    taaFrame = 1 - taaFrame; }