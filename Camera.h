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
	glm::mat4 cameraMatrix = glm::mat4(1.0f);
	double lastMouseX = 0.0, lastMouseY = 0.0;
	bool firstClick = true;
	int width;
	int height;
	float speed = 0.1f;
	float sensitivity = 1.0f;
	Camera(int width, int height, glm::vec3 position);
	glm::mat4 GetProjectionMatrix(float FOVdeg, float nearPlane, float farPlane)
	{
		return glm::perspective(glm::radians(FOVdeg), (float)width / (float)height, nearPlane, farPlane);
	}
	glm::mat4 GetLookAtMatrix()
	{
		return glm::lookAt(Position, Position + Orientation, Up);
	}
	glm::mat4 GetViewMatrix()
	{
		return cameraMatrix; 
	}
	void updateMatrix(float FOVdeg, float nearPlane, float farPlane);
	void Matrix(Shader& shader, const char* uniform);
	void Inputs(GLFWwindow* window);
};
#endif