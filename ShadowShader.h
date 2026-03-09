#ifndef SHADOWSHADER_CLASS_H
#define SHADOWSHADER_CLASS_H

#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include<glad/glad.h>
#include "shaderClass.h"

class ShadowShader {
public:
    Shader atlasShader;

    unsigned int atlasFBO;
    unsigned int shadowAtlas;

    int atlasResolution = 8192; // Размер всего гигантского холста
    int tileSize = 2048;        // Размер одной тени. Значит у нас сетка 4x4 (16 теней одновременно!)

    ShadowShader() {
        // Убедись, что эти файлы шейдеров у тебя есть (те самые старые для солнца)
        atlasShader = Shader("shadowMap.vert", "shadowMap.frag");

        glGenFramebuffers(1, &atlasFBO);
        glGenTextures(1, &shadowAtlas);
        glBindTexture(GL_TEXTURE_2D, shadowAtlas);

        // Создаем огромную пустую текстуру
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, atlasResolution, atlasResolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float clampColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, clampColor);

        glBindFramebuffer(GL_FRAMEBUFFER, atlasFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowAtlas, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

#endif