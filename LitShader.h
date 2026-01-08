#ifndef LITSHADER_CLASS_H
#define LITSHADER_CLASS_H
#include<glm/glm.hpp>
#include<glm/gtc/quaternion.hpp>
#include<glad/glad.h>
#include<vector>
#include <GLFW/glfw3.h>
#include <iostream>
#include "tinyfiledialogs.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"
#include "Window.h"
#include "GameObject.h"
#include "shaderClass.h"
#include <map>
#include <nlohmann/json.hpp>
class LitShader {
public:
	Shader shader;

	LitShader(GameObject& light) {
		shader =  Shader("default.vert", "default.frag");
		shader.Activate();
		glUniform4f(glGetUniformLocation(shader.ID, "lightColor"), light.light.color.x, light.light.color.y, light.light.color.z, light.light.color.w);
		glUniform3f(glGetUniformLocation(shader.ID, "lightPos"), light.transform.position.x, light.transform.position.y, light.transform.position.z);

	}
	void Update(GameObject& light,Camera& camera, unsigned int& depthCubemap) {;
		shader.Activate();

		float farPlane = 100.0f;
		glm::mat4 orthgonalProjection = glm::ortho(-35.0f, 35.0f, -35.0f, 35.0f, 0.1f, farPlane);
		glm::mat4 perspectiveProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
		glm::mat4 lightView = glm::lookAt(light.transform.position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 lightProjection = perspectiveProjection * lightView;

		glUniform3f(glGetUniformLocation(shader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
		camera.Matrix(shader, "camMatrix");
		glUniformMatrix4fv(glGetUniformLocation(shader.ID, "lightProjection"), 1, GL_FALSE, glm::value_ptr(lightProjection));
		glUniform1f(glGetUniformLocation(shader.ID, "farPlane"), farPlane);

		glActiveTexture(GL_TEXTURE0 + 2);
		glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubemap);
		glUniform1i(glGetUniformLocation(shader.ID, "shadowCubeMap"), 2);
		glUniform4f(glGetUniformLocation(shader.ID, "lightColor"), light.light.color.x, light.light.color.y, light.light.color.z, light.light.color.w);
		glUniform3f(glGetUniformLocation(shader.ID, "lightPos"), light.transform.position.x, light.transform.position.y, light.transform.position.z);
	}
};

#endif