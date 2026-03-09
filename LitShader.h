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

    }
};
#endif