#ifndef CAMERA_CLASS_H
#define CAMERA_CLASS_H

#define GLM_ENABLE_EXPERIMENTAL
#include<glad/glad.h>
#include<GLFW/glfw3.h>
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include<glm/gtx/rotate_vector.hpp>
#include<glm/gtx/vector_angle.hpp>
#include <array>
#include"shaderClass.h"

class Camera
{
public:
    glm::vec3 Position;
    glm::vec3 Orientation = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f);

    // --- ПРАВИЛЬНЫЕ ИМЕНА ПЕРЕМЕННЫХ ---
    // Это готовые матрицы (Projection * View), которые ты обновляешь каждый кадр
    glm::mat4 viewProjectionMatrix = glm::mat4(1.0f);
    glm::mat4 cleanViewProjectionMatrix = glm::mat4(1.0f); // Для TAA без тряски

    double lastMouseX = 0.0, lastMouseY = 0.0;
    bool firstClick = true;
    int width;
    int height;
    float speed = 0.1f;
    float sensitivity = 1.0f;
    int taaFrameIndex = 0;

    Camera(int width, int height, glm::vec3 position);
    void Matrix(Shader& shader, const char* uniform);
    // 1. МАТРИЦА ПРОЕКЦИИ (Перспектива, FOV, Near/Far)
    glm::mat4 GetProjectionMatrix(float FOVdeg, float nearPlane, float farPlane, bool applyJitter = true)
    {
        glm::mat4 proj = glm::perspective(glm::radians(FOVdeg), (float)width / (float)height, nearPlane, farPlane);
        if (applyJitter) {
            const float jitterX[8] = { 0.125f, -0.375f, 0.625f, -0.875f, 0.375f, -0.125f, 0.875f, -0.625f };
            const float jitterY[8] = { 0.111f, -0.555f, 0.777f, -0.111f, -0.777f, 0.555f, -0.333f, 0.333f };

            float deltaX = jitterX[taaFrameIndex % 8] - 0.5f;
            float deltaY = jitterY[taaFrameIndex % 8] - 0.5f;

            proj[2][0] += (deltaX * 2.0f) / (float)width;
            proj[2][1] += (deltaY * 2.0f) / (float)height;
        }
        return proj;
    }

    // 2. МАТРИЦА ВИДА (Где стоит камера и куда смотрит)
    // Раньше была: GetLookAtMatrix()
    glm::mat4 GetViewMatrix()
    {
        return glm::lookAt(Position, Position + Orientation, Up);
    }

    // 3. КОМБИНИРОВАННАЯ МАТРИЦА (Projection * View)
    // Раньше была: GetViewMatrix() - что вызывало жуткую путаницу!
    glm::mat4 GetViewProjectionMatrix()
    {
        return viewProjectionMatrix;
    }

    void updateMatrix(float FOVdeg, float nearPlane, float farPlane);
    void Inputs(GLFWwindow* window);
};
#endif