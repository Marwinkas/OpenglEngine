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
    ssaoShader = Shader("framebuffer.vert", "ssao.frag");
    bloomThresholdShader = Shader("framebuffer.vert", "bloom_threshold.frag");
    bloomBlurShader = Shader("framebuffer.vert", "bloom_blur.frag");
    combineShader = Shader("framebuffer.vert", "combine.frag");
    cameraFxShader = Shader("framebuffer.vert", "camera_fx.frag");
    finalShader = Shader("framebuffer.vert", "final.frag"); // <-- ПЕРЕИМЕНОВАЛИ blurShader!

    // --- 1. Готовим данные для SSAO (Ядро и Шум) ---
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
        glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f);
        ssaoNoise.push_back(noise);
    }
    glGenTextures(1, &ssaoNoiseTexture);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // --- 2. Геометрия экрана ---
    glGenVertexArrays(1, &rectVAO);
    glGenBuffers(1, &rectVBO);
    glBindVertexArray(rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectangleVerticesSSAO), &rectangleVerticesSSAO, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // --- 3. Настройка FBO ---
    auto createTex = [&](unsigned int& fbo, unsigned int& tex, GLenum format) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, format, window.width, window.height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        };

    // MSAA основной буфер
    glGenFramebuffers(1, &FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    auto createMS = [&](unsigned int& tex, GLenum attach) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGB16F, window.width, window.height, GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D_MULTISAMPLE, tex, 0);
        };
    createMS(framebufferTexture, GL_COLOR_ATTACHMENT0);
    createMS(normalTextureMSAA, GL_COLOR_ATTACHMENT1);
    createMS(positionTextureMSAA, GL_COLOR_ATTACHMENT2);
    unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, attachments);

    glGenRenderbuffers(1, &RBO);
    glBindRenderbuffer(GL_RENDERBUFFER, RBO);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, window.width, window.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, RBO);

    // G-Buffer для пост-обработки
    glGenFramebuffers(1, &postProcessingFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, postProcessingFBO);

    auto createGBufferTex = [&](unsigned int& tex, GLenum attach) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width, window.height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D, tex, 0);
        };

    // ЦВЕТ (с мипмапами для автоэкспозиции)
    glGenTextures(1, &postProcessingTexture);
    glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width, window.height, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postProcessingTexture, 0);

    createGBufferTex(postNormalTexture, GL_COLOR_ATTACHMENT1);
    createGBufferTex(postPositionTexture, GL_COLOR_ATTACHMENT2);
    glDrawBuffers(3, attachments);

    // Буферы для Пинг-Понга (Блум)
    glGenFramebuffers(2, pingpongFBO);
    glGenTextures(2, pingpongColorbuffers);
    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width, window.height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorbuffers[i], 0);
    }

    createTex(bloomThresholdFBO, bloomThresholdTexture, GL_RGB16F);
    createTex(ssaoFBO, ssaoTexture, GL_RGB16F);
    createTex(ssgiFBO, ssgiTexture, GL_RGB16F);
    createTex(combineFBO, combineTexture, GL_RGB16F);
    createTex(cameraFxFBO, cameraFxTexture, GL_RGB16F);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcessingShader::Bind(Window& window) {
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glViewport(0, 0, window.width, window.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}

void PostProcessingShader::Update(Window& window, float crntTime, Camera& camera, UI& ui) {
    // --- 1. RESOLVE MSAA ---
    glBindFramebuffer(GL_READ_FRAMEBUFFER, FBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postProcessingFBO);
    for (int i = 0; i < 3; ++i) {
        glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
        glBlitFramebuffer(0, 0, window.width, window.height, 0, 0, window.width, window.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_DEPTH_TEST);

    // --- 2. ПРОХОД SSAO ---
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoShader.Activate();
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "enableSSAO"), ui.ppSettings.enableSSAO);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "radius"), ui.ppSettings.ssaoRadius);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "bias"), ui.ppSettings.ssaoBias);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "intensity"), ui.ppSettings.ssaoIntensity);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "power"), ui.ppSettings.ssaoPower);

    glm::mat4 view = camera.GetLookAtMatrix();
    glm::mat4 projection = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);

    for (unsigned int i = 0; i < 64; ++i)
        glUniform3fv(glGetUniformLocation(ssaoShader.ID, ("samples[" + std::to_string(i) + "]").c_str()), 1, glm::value_ptr(ssaoKernel[i]));

    glUniformMatrix4fv(glGetUniformLocation(ssaoShader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(ssaoShader.ID, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniform2f(glGetUniformLocation(ssaoShader.ID, "noiseScale"), (float)window.width / 4.0f, (float)window.height / 4.0f);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, postNormalTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "normalTexture"), 0);
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "positionTexture"), 1);
    glUniform1i(glGetUniformLocation(ssaoShader.ID, "texNoise"), 2);
    glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // --- 3. ПРОХОД SSGI ---
    glBindFramebuffer(GL_FRAMEBUFFER, ssgiFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    ssgiShader.Activate();
    glUniformMatrix4fv(glGetUniformLocation(ssgiShader.ID, "camMatrix"), 1, GL_FALSE, glm::value_ptr(camera.cameraMatrix));
    glUniform1f(glGetUniformLocation(ssgiShader.ID, "time"), crntTime);
    glUniform1i(glGetUniformLocation(ssgiShader.ID, "enableSSGI"), ui.ppSettings.enableSSGI);
    glUniform1i(glGetUniformLocation(ssgiShader.ID, "rayCount"), ui.ppSettings.ssgiRayCount);
    glUniform1f(glGetUniformLocation(ssgiShader.ID, "stepSize"), ui.ppSettings.ssgiStepSize);
    glUniform1f(glGetUniformLocation(ssgiShader.ID, "thickness"), ui.ppSettings.ssgiThickness);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, postNormalTexture);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glUniform1i(glGetUniformLocation(ssgiShader.ID, "screenTexture"), 0);
    glUniform1i(glGetUniformLocation(ssgiShader.ID, "normalTexture"), 1);
    glUniform1i(glGetUniformLocation(ssgiShader.ID, "positionTexture"), 2);
    glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // --- 4. КОМПОЗИТИНГ (Combine Pass) ---
    glBindFramebuffer(GL_FRAMEBUFFER, combineFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    combineShader.Activate();

    glUniform1i(glGetUniformLocation(combineShader.ID, "blurRange"), ui.ppSettings.blurRange);
    glUniform3f(glGetUniformLocation(combineShader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);

    glUniform1i(glGetUniformLocation(combineShader.ID, "enableFog"), ui.ppSettings.enableFog);
    glUniform1f(glGetUniformLocation(combineShader.ID, "fogDensity"), ui.ppSettings.fogDensity);
    glUniform1f(glGetUniformLocation(combineShader.ID, "fogHeightFalloff"), ui.ppSettings.fogHeightFalloff);
    glUniform1f(glGetUniformLocation(combineShader.ID, "fogBaseHeight"), ui.ppSettings.fogBaseHeight);
    glUniform3fv(glGetUniformLocation(combineShader.ID, "fogColor"), 1, glm::value_ptr(ui.ppSettings.fogColor));
    glUniform3fv(glGetUniformLocation(combineShader.ID, "inscatterColor"), 1, glm::value_ptr(ui.ppSettings.inscatterColor));
    glUniform1f(glGetUniformLocation(combineShader.ID, "inscatterPower"), ui.ppSettings.inscatterPower);
    glUniform1f(glGetUniformLocation(combineShader.ID, "inscatterIntensity"), ui.ppSettings.inscatterIntensity);
    glUniform3fv(glGetUniformLocation(combineShader.ID, "sunDirFog"), 1, glm::value_ptr(glm::vec3(0.0, -1.0, 0.0)));

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, postNormalTexture);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssaoTexture);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, ssgiTexture);

    glUniform1i(glGetUniformLocation(combineShader.ID, "screenTexture"), 0);
    glUniform1i(glGetUniformLocation(combineShader.ID, "positionTexture"), 1);
    glUniform1i(glGetUniformLocation(combineShader.ID, "normalTexture"), 2);
    glUniform1i(glGetUniformLocation(combineShader.ID, "ssaoTexture"), 3);
    glUniform1i(glGetUniformLocation(combineShader.ID, "ssgiTexture"), 4);
    glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // --- 5. ЭФФЕКТЫ ОБЪЕКТИВА (Camera FX Pass - БЕЗ Motion Blur) ---
    glBindFramebuffer(GL_FRAMEBUFFER, cameraFxFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    cameraFxShader.Activate();

    glUniform3f(glGetUniformLocation(cameraFxShader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
    glUniform1i(glGetUniformLocation(cameraFxShader.ID, "enableDoF"), ui.ppSettings.enableDoF);
    glUniform1f(glGetUniformLocation(cameraFxShader.ID, "focusDistance"), ui.ppSettings.focusDistance);
    glUniform1f(glGetUniformLocation(cameraFxShader.ID, "focusRange"), ui.ppSettings.focusRange);
    glUniform1f(glGetUniformLocation(cameraFxShader.ID, "bokehSize"), ui.ppSettings.bokehSize);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, combineTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, postPositionTexture);

    glUniform1i(glGetUniformLocation(cameraFxShader.ID, "combineTexture"), 0);
    glUniform1i(glGetUniformLocation(cameraFxShader.ID, "positionTexture"), 1);

    glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // --- 6. БЛУМ (Bloom) ---
    bool horizontal = true, first_iteration = true;
    int amount = ui.ppSettings.bloomBlurIterations * 2;

    if (ui.ppSettings.enableBloom) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        bloomThresholdShader.Activate();
        glUniform1f(glGetUniformLocation(bloomThresholdShader.ID, "threshold"), ui.ppSettings.bloomThreshold);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cameraFxTexture); // Берем картинку после DoF
        glUniform1i(glGetUniformLocation(bloomThresholdShader.ID, "screenTexture"), 0);

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
    }

    // --- 7. АДАПТАЦИЯ ГЛАЗА (Auto Exposure) ---
    if (ui.ppSettings.autoExposure) {
        glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
        glGenerateMipmap(GL_TEXTURE_2D);
        int maxLevel = (int)std::floor(std::log2(std::max(window.width, window.height)));

        float avgColor[3];
        glGetTexImage(GL_TEXTURE_2D, maxLevel, GL_RGB, GL_FLOAT, avgColor);

        float avgLuminance = 0.2126f * avgColor[0] + 0.7152f * avgColor[1] + 0.0722f * avgColor[2];
        avgLuminance = std::max(avgLuminance, 0.001f);

        float targetExposure = 0.5f / avgLuminance;
        targetExposure = std::clamp(targetExposure, ui.ppSettings.minBrightness, ui.ppSettings.maxBrightness);

        static float lastTime = 0.0f;
        float deltaTime = crntTime - lastTime;
        lastTime = crntTime;

        float adaptationSpeed = 1.5f;
        ui.ppSettings.currentExposure += (targetExposure - ui.ppSettings.currentExposure) * adaptationSpeed * deltaTime;
    }
    else {
        ui.ppSettings.currentExposure = ui.ppSettings.manualExposure;
    }

    // --- 8. FINAL PASS (Вывод на экран) ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Рисуем в главное окно

    finalShader.Activate();
    glUniform1f(glGetUniformLocation(finalShader.ID, "time"), crntTime);
    glUniform1f(glGetUniformLocation(finalShader.ID, "gamma"), ui.ppSettings.gamma);
    // Передаем то, что забыли:
    glUniform1i(glGetUniformLocation(finalShader.ID, "enableSharpen"), ui.ppSettings.enableSharpen);
    glUniform1f(glGetUniformLocation(finalShader.ID, "sharpenIntensity"), ui.ppSettings.sharpenIntensity);
    glUniform1i(glGetUniformLocation(finalShader.ID, "enableFilmGrain"), ui.ppSettings.enableFilmGrain);
    glUniform1f(glGetUniformLocation(finalShader.ID, "grainIntensity"), ui.ppSettings.grainIntensity);
    glUniform1f(glGetUniformLocation(finalShader.ID, "temperature"), ui.ppSettings.temperature); 
    glUniform1f(glGetUniformLocation(finalShader.ID, "currentExposure"), ui.ppSettings.currentExposure);
    glUniform1f(glGetUniformLocation(finalShader.ID, "exposureCompensation"), ui.ppSettings.exposureCompensation);
    glUniform1f(glGetUniformLocation(finalShader.ID, "contrast"), ui.ppSettings.contrast);
    glUniform1f(glGetUniformLocation(finalShader.ID, "saturation"), ui.ppSettings.saturation);
    glUniform1i(glGetUniformLocation(finalShader.ID, "enableVignette"), ui.ppSettings.enableVignette);
    glUniform1f(glGetUniformLocation(finalShader.ID, "vignetteIntensity"), ui.ppSettings.vignetteIntensity);
    glUniform1i(glGetUniformLocation(finalShader.ID, "enableChromaticAberration"), ui.ppSettings.enableChromaticAberration);
    glUniform1f(glGetUniformLocation(finalShader.ID, "caIntensity"), ui.ppSettings.caIntensity);
    glUniform1i(glGetUniformLocation(finalShader.ID, "enableBloom"), ui.ppSettings.enableBloom);
    glUniform1f(glGetUniformLocation(finalShader.ID, "bloomIntensity"), ui.ppSettings.bloomIntensity);
    glUniform1i(glGetUniformLocation(finalShader.ID, "enableLensFlares"), ui.ppSettings.enableLensFlares);
    glUniform1f(glGetUniformLocation(finalShader.ID, "flareIntensity"), ui.ppSettings.flareIntensity);
    glUniform1f(glGetUniformLocation(finalShader.ID, "ghostDispersal"), ui.ppSettings.ghostDispersal);
    glUniform1i(glGetUniformLocation(finalShader.ID, "ghosts"), ui.ppSettings.ghosts);
    glUniform1i(glGetUniformLocation(finalShader.ID, "enableGodRays"), ui.ppSettings.enableGodRays);
    glUniform1f(glGetUniformLocation(finalShader.ID, "godRaysIntensity"), ui.ppSettings.godRaysIntensity);

    // Передаем позицию солнца для God Rays (здесь можешь взять из UI)
    glUniform2f(glGetUniformLocation(finalShader.ID, "lightScreenPos"), 0.5f, 0.5f);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, cameraFxTexture); // Наша готовая картинка
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[!horizontal]); // Блум
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, postPositionTexture); // Позиция для God Rays

    glUniform1i(glGetUniformLocation(finalShader.ID, "screenTexture"), 0);
    glUniform1i(glGetUniformLocation(finalShader.ID, "bloomTexture"), 1);
    glUniform1i(glGetUniformLocation(finalShader.ID, "positionTexture"), 2);

    glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
}