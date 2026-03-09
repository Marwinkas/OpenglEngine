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
    // --- 3. Настройка Resolve FBO (G-буфер без MSAA) ---
    glGenFramebuffers(1, &postProcessingFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, postProcessingFBO);
    // Цвет (с мипмапами для экспозиции)
    glGenTextures(1, &postProcessingTexture);
    glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width, window.height, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postProcessingTexture, 0);
    // Нормали и Позиции (без мипмапов)
    auto createGBufferTex = [&](unsigned int& tex, GLenum attach) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width, window.height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D, tex, 0);
        };
    createGBufferTex(postNormalTexture, GL_COLOR_ATTACHMENT1);
    createGBufferTex(postPositionTexture, GL_COLOR_ATTACHMENT2);
    // --- НОВОЕ: Создаем Текстуру Глубины для Occlusion Culling ---
glGenTextures(1, &postDepthTexture);
    glBindTexture(GL_TEXTURE_2D, postDepthTexture);
    
    // ИСПРАВЛЕНИЕ: Формат должен ИДЕАЛЬНО совпадать с MSAA RBO, иначе копирование не сработает!
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, window.width, window.height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);

    // Оставляем обычные фильтры, мипмапы тут не нужны
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ИСПРАВЛЕНИЕ: Привязываем текстуру как совмещенный буфер (Depth + Stencil)
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, postDepthTexture, 0);
    // ВАЖНО: говорим этому FBO, что у него 3 выхода!
    unsigned int postAttachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, postAttachments);
    // Буферы для эффектов
    bloomThresholdShader = Shader("framebuffer.vert", "bloom_threshold.frag");
    bloomBlurShader = Shader("framebuffer.vert", "bloom_blur.frag");
    // Создаем два буфера для Пинг-Понга
    glGenFramebuffers(2, pingpongFBO);
    glGenTextures(2, pingpongColorbuffers);
    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[i]);
        // GL_RGB16F критически важно для HDR (чтобы цвета могли быть ярче 1.0)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, window.width / 4, window.height / 4, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorbuffers[i], 0);
    }
    createTex(bloomThresholdFBO, bloomThresholdTexture, GL_RGB16F);
    // Делаем SSAO в два раза меньше для сумасшедшего буста ФПС
    int halfW = window.width / 2;
    int halfH = window.height / 2;

    auto createSsaoTex = [&](unsigned int& fbo, unsigned int& tex) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        // GL_RED - экономим кучу видеопамяти, нам нужен только 1 канал!
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, halfW, halfH, 0, GL_RED, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        };

    createSsaoTex(ssaoFBO, ssaoTexture);
    createSsaoTex(ssaoBlurFBO, ssaoBlurTexture);

    // Загружаем наш новый шейдер размытия
    ssaoBlurShader = Shader("framebuffer.vert", "ssao_blur.frag");
    createTex(ssgiFBO, ssgiTexture, GL_RGB16F);
    // 1. Загружаем наш новый шейдер
    hiZShader = Shader("hiz.comp");
    // 2. Считаем, сколько "этажей" (мипмапов) поместится в текстуру размером с твой экран
    // Например, для 1920x1080 получится 11 этажей
    hiZMipMapCount = (int)std::floor(std::log2(std::max(window.width, window.height))) + 1;
    // 3. Создаем текстуру GL_R32F (это специальный формат для чистых чисел с плавающей запятой)
    glGenTextures(1, &hiZTexture);
    glBindTexture(GL_TEXTURE_2D, hiZTexture);
    // glTexStorage2D сама выделит память сразу под ВСЕ этажи пирамиды!
    glTexStorage2D(GL_TEXTURE_2D, hiZMipMapCount, GL_R32F, window.width, window.height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- НОВОЕ: Буферы для Separable Blur SSGI ---
// Делаем текстуры в 2 раза меньше экрана для огромного буста ФПС
    int halfWidth = window.width / 2;
    int halfHeight = window.height / 2;

    glGenFramebuffers(2, ssgiBlurFBO); // Нужно объявить ssgiBlurFBO[2] в .h файле
    glGenTextures(2, ssgiBlurTextures); // И ssgiBlurTextures[2] в .h файле

    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, ssgiBlurFBO[i]);
        glBindTexture(GL_TEXTURE_2D, ssgiBlurTextures[i]);
        // Формат GL_RGB16F важен для света
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, halfWidth, halfHeight, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Края зажимаем, чтобы свечение не "перетекало" с одной стороны экрана на другую
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssgiBlurTextures[i], 0);
    }

    // Загружаем новый шейдер размытия (создадим его на Шаге 3)
    ssgiSeparableBlurShader = Shader("framebuffer.vert", "ssgi_blur.frag");
}
void PostProcessingShader::Bind(Window& window) {
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glViewport(0, 0, window.width, window.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}
static glm::mat4 lastViewProj = glm::mat4(1.0f);
void PostProcessingShader::Update(Window& window, float crntTime, Camera& camera, UI& ui, glm::vec3 sunDir) {
    // --- 1. RESOLVE MSAA ---
    // (Твой старый код без изменений)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, FBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postProcessingFBO);
    for (int i = 0; i < 3; ++i) {
        glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
        glBlitFramebuffer(0, 0, window.width, window.height, 0, 0, window.width, window.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBlitFramebuffer(0, 0, window.width, window.height, 0, 0, window.width, window.height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_DEPTH_TEST);
    // ==========================================
    // --- ПОСТРОЕНИЕ ПИРАМИДЫ ГЛУБИНЫ (Hi-Z) ---
    // ==========================================
    hiZShader.Activate();
    for (int i = 0; i < hiZMipMapCount; ++i) {
        // Узнаем размер текущего этажа (делим размер экрана на 2, 4, 8 и т.д.)
        int mipWidth = std::max(1, window.width >> i);
        int mipHeight = std::max(1, window.height >> i);
        glActiveTexture(GL_TEXTURE0);
        if (i == 0) {
            // Этаж 0: Копируем базовую глубину из FBO в нашу пирамиду
            glBindTexture(GL_TEXTURE_2D, postDepthTexture);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "inMipLevel"), 0);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "copyOnly"), 1);
        }
        else {
            // Этаж 1 и выше: Строим на основе предыдущего этажа самой пирамиды
            glBindTexture(GL_TEXTURE_2D, hiZTexture);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "inMipLevel"), i - 1);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "copyOnly"), 0);
        }
        glUniform1i(glGetUniformLocation(hiZShader.ID, "inDepth"), 0);
        // Говорим шейдеру, В КАКОЙ этаж мы будем записывать результат
        glBindImageTexture(0, hiZTexture, i, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
        // Запускаем ядра RTX 3090! (Блоками по 16х16)
        GLuint groupsX = (mipWidth + 15) / 16;
        GLuint groupsY = (mipHeight + 15) / 16;
        glDispatchCompute(groupsX, groupsY, 1);
        // ЖДЁМ: запрещаем строить следующий этаж, пока не достроится этот!
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    // ==========================================
    // 
    // --- 2. ПРОХОД SSAO ---
    int halfW = window.width / 2;
    int halfH = window.height / 2;
    glViewport(0, 0, halfW, halfH); // УМЕНЬШАЕМ ЭКРАН!

    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoShader.Activate();

    glUniform1i(glGetUniformLocation(ssaoShader.ID, "enableSSAO"), ui.ppSettings.enableSSAO);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "radius"), ui.ppSettings.ssaoRadius);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "bias"), ui.ppSettings.ssaoBias);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "intensity"), ui.ppSettings.ssaoIntensity);
    glUniform1f(glGetUniformLocation(ssaoShader.ID, "power"), ui.ppSettings.ssaoPower);
    // ВАЖНО: Вытаскиваем матрицы из камеры. 
    // Используй здесь те же значения FOV, Near и Far, что и в основном цикле игры!
    glm::mat4 view = camera.GetLookAtMatrix();
    glm::mat4 projection = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);
    // Передаем ядро выборки
    for (unsigned int i = 0; i < 64; ++i)
        glUniform3fv(glGetUniformLocation(ssaoShader.ID, ("samples[" + std::to_string(i) + "]").c_str()), 1, glm::value_ptr(ssaoKernel[i]));
    // Теперь передаем матрицы
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
    glBindTexture(GL_TEXTURE_2D, ssaoTexture); // Берем сырой SSAO в точках
    glUniform1i(glGetUniformLocation(ssaoBlurShader.ID, "ssaoInput"), 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glUniform1i(glGetUniformLocation(ssaoBlurShader.ID, "positionTexture"), 1);
    // ВОЗВРАЩАЕМ ПОЛНЫЙ РАЗМЕР ЭКРАНА ДЛЯ ОСТАЛЬНЫХ ЭФФЕКТОВ!
    glViewport(0, 0, window.width, window.height);
    // --- 3. ПРОХОД SSGI ---
    // (Твой код без изменений)
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
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (ui.ppSettings.enableSSGI) {
        ssgiSeparableBlurShader.Activate();
        // Размытие с учетом глубины и нормалей (чтобы не размыть грани объектов)
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, postNormalTexture);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, postPositionTexture);
        glUniform1i(glGetUniformLocation(ssgiSeparableBlurShader.ID, "normalTexture"), 1);
        glUniform1i(glGetUniformLocation(ssgiSeparableBlurShader.ID, "positionTexture"), 2);

        // Пас 1: Горизонтальное размытие (Читаем из сырого SSGI, пишем в BlurFBO[0])
        glBindFramebuffer(GL_FRAMEBUFFER, ssgiBlurFBO[0]);
        // Обязательно меняем вьюпорт под половинное разрешение!
        glViewport(0, 0, window.width / 2, window.height / 2);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1i(glGetUniformLocation(ssgiSeparableBlurShader.ID, "horizontal"), 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ssgiTexture); // Исходник
        glUniform1i(glGetUniformLocation(ssgiSeparableBlurShader.ID, "image"), 0);
        glBindVertexArray(rectVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Пас 2: Вертикальное размытие (Читаем из BlurFBO[0], пишем в BlurFBO[1])
        glBindFramebuffer(GL_FRAMEBUFFER, ssgiBlurFBO[1]);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1i(glGetUniformLocation(ssgiSeparableBlurShader.ID, "horizontal"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ssgiBlurTextures[0]); // Размытое по X
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Возвращаем вьюпорт для остального рендера
        glViewport(0, 0, window.width, window.height);
    }
    else {
        // ИСПРАВЛЕНИЕ: Если SSGI выключен, заливаем буфер черным цветом!
        // Иначе там останется "призрак" старого кадра.
        glBindFramebuffer(GL_FRAMEBUFFER, ssgiBlurFBO[1]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); // Возвращаемся
    }
    bool horizontal = true, first_iteration = true;
    int amount = ui.ppSettings.bloomBlurIterations * 2; // Умножаем на 2 (туда-обратно)
    if (ui.ppSettings.enableBloom) {
        // 1. Threshold (Вырезаем яркие пиксели)
        glViewport(0, 0, window.width / 4, window.height / 4);
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        bloomThresholdShader.Activate();
        glUniform1f(glGetUniformLocation(bloomThresholdShader.ID, "threshold"), ui.ppSettings.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postProcessingTexture); // Берем оригинальную картинку
        glUniform1i(glGetUniformLocation(bloomThresholdShader.ID, "screenTexture"), 0);
        glBindVertexArray(rectVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // 2. Ping-Pong Blur
        bloomBlurShader.Activate();
        for (unsigned int i = 0; i < amount; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal]);
            glUniform1i(glGetUniformLocation(bloomBlurShader.ID, "horizontal"), horizontal);
            glActiveTexture(GL_TEXTURE0);
            // В первой итерации берем картинку из Threshold, дальше - друг у друга
            glBindTexture(GL_TEXTURE_2D, first_iteration ? pingpongColorbuffers[0] : pingpongColorbuffers[!horizontal]);
            glUniform1i(glGetUniformLocation(bloomBlurShader.ID, "image"), 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            horizontal = !horizontal;
            first_iteration = false;
        }
        glViewport(0, 0, window.width, window.height);
    }
    // ==========================================
   // --- ЛОГИКА АДАПТАЦИИ ГЛАЗА (Auto Exposure) ---
   // ==========================================
    // ==========================================
    // --- ЛОГИКА АДАПТАЦИИ ГЛАЗА ---
    // ==========================================
    // ==========================================
    // --- ЛОГИКА АДАПТАЦИИ ГЛАЗА (Диагностика) ---
    // ==========================================
    // ==========================================
    // --- ГЕНЕРАЦИЯ МИПМАПОВ (Для DoF и Экспозиции) ---
    // ==========================================
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glGenerateMipmap(GL_TEXTURE_2D);
    // Всё! Больше процессор не ждет видеокарту.
    // 1. Вычисляем матрицы для Motion Blur

    glm::vec3 sunPos = -sunDir * 1000.0f;
    // Убедись, что тут реально есть яркий свет!
    glm::mat4 currentViewProj = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f) * camera.GetLookAtMatrix();
    glm::vec4 clipSpacePos = currentViewProj * glm::vec4(sunPos, 1.0f);
    glm::vec3 ndcSpacePos = glm::vec3(clipSpacePos) / clipSpacePos.w;
    glm::vec2 sunScreenPos = (glm::vec2(ndcSpacePos.x, ndcSpacePos.y) + 1.0f) / 2.0f;
    // ФИКС: Плавное затухание лучей на закате!
    // sunDir.y отрицательный, когда солнце светит сверху вниз (день).
    // Как только он становится положительным, солнце уходит под землю.
    float horizonFade = glm::clamp(-sunDir.y * 10.0f + 0.5f, 0.0f, 1.0f);
    float sunVisible = (clipSpacePos.w > 0.0f && clipSpacePos.z < clipSpacePos.w) ? horizonFade : 0.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // Теперь, когда ui.ppSettings.currentExposure обновился, он улетит в шейдер ниже!
    blurShader.Activate();
    glUniform1f(glGetUniformLocation(blurShader.ID, "time"), crntTime);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableMotionBlur"), ui.ppSettings.enableMotionBlur);
    glUniform1f(glGetUniformLocation(blurShader.ID, "mbStrength"), ui.ppSettings.mbStrength);
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
    glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[!horizontal]); // Наше свечение
    glUniform1i(glGetUniformLocation(blurShader.ID, "bloomTexture"), 5);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableBloom"), ui.ppSettings.enableBloom);
    glUniform1f(glGetUniformLocation(blurShader.ID, "bloomIntensity"), ui.ppSettings.bloomIntensity);
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableLensFlares"), ui.ppSettings.enableLensFlares);
    glUniform1f(glGetUniformLocation(blurShader.ID, "flareIntensity"), ui.ppSettings.flareIntensity);
    glUniform1f(glGetUniformLocation(blurShader.ID, "ghostDispersal"), ui.ppSettings.ghostDispersal);
    glUniform1i(glGetUniformLocation(blurShader.ID, "ghosts"), ui.ppSettings.ghosts);
    // ... твои старые uniform ...
    // Передаем настройки тумана
    glUniform1i(glGetUniformLocation(blurShader.ID, "enableFog"), ui.ppSettings.enableFog);
    glUniform1f(glGetUniformLocation(blurShader.ID, "fogDensity"), ui.ppSettings.fogDensity);
    glUniform1f(glGetUniformLocation(blurShader.ID, "fogHeightFalloff"), ui.ppSettings.fogHeightFalloff);
    glUniform1f(glGetUniformLocation(blurShader.ID, "fogBaseHeight"), ui.ppSettings.fogBaseHeight);
    glUniform3fv(glGetUniformLocation(blurShader.ID, "fogColor"), 1, ui.ppSettings.fogColor);
    glUniform3fv(glGetUniformLocation(blurShader.ID, "inscatterColor"), 1, ui.ppSettings.inscatterColor);
    glUniform1f(glGetUniformLocation(blurShader.ID, "inscatterPower"), ui.ppSettings.inscatterPower);
    glUniform1f(glGetUniformLocation(blurShader.ID, "inscatterIntensity"), ui.ppSettings.inscatterIntensity);
    // Направление солнца (возьми тот же sunDir, что мы делали для неба)
    glUniform3f(glGetUniformLocation(blurShader.ID, "sunDirFog"), sunDir.x, sunDir.y, sunDir.z);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ssgiBlurTextures[1]);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, postNormalTexture);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, postPositionTexture);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, ssaoBlurTexture);
    glUniform1i(glGetUniformLocation(blurShader.ID, "screenTexture"), 0);
    glUniform1i(glGetUniformLocation(blurShader.ID, "ssgiTexture"), 1);
    glUniform1i(glGetUniformLocation(blurShader.ID, "normalTexture"), 2);
    glUniform1i(glGetUniformLocation(blurShader.ID, "positionTexture"), 3);
    glUniform1i(glGetUniformLocation(blurShader.ID, "ssaoTexture"), 4);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);

    lastViewProj = currentViewProj;
}