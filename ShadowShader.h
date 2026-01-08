#ifndef SHADOWSHADER_CLASS_H
#define SHADOWSHADER_CLASS_H
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
class ShadowShader {
public:
	Shader shader;
	Shader shaderCube;

	unsigned int shadowMapFBO;
	unsigned int shadowMapWidth = 512, shadowMapHeight = 512;
	unsigned int shadowMap;
	float farPlane = 100.0f;
	unsigned int pointShadowMapFBO;
	unsigned int depthCubemap;

	ShadowShader(GameObject& light) {
		shader = Shader("shadowMap.vert", "shadowMap.frag");
		shaderCube = Shader("shadowCubeMap.vert", "shadowCubeMap.frag", "shadowCubeMap.geom");


		
		glGenFramebuffers(1, &shadowMapFBO);

		
		glGenTextures(1, &shadowMap);
		glBindTexture(GL_TEXTURE_2D, shadowMap);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, shadowMapWidth, shadowMapHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

		float clampColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, clampColor);

		glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap, 0);

		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		
		glm::mat4 orthgonalProjection = glm::ortho(-35.0f, 35.0f, -35.0f, 35.0f, 0.1f, farPlane);
		glm::mat4 perspectiveProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
		glm::mat4 lightView = glm::lookAt(light.transform.position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 lightProjection = perspectiveProjection * lightView;

		shader.Activate();
		glUniformMatrix4fv(glGetUniformLocation(shader.ID, "lightProjection"), 1, GL_FALSE, glm::value_ptr(lightProjection));

		
		glGenFramebuffers(1, &pointShadowMapFBO);

		
		glGenTextures(1, &depthCubemap);

		glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubemap);
		for (unsigned int i = 0; i < 6; ++i)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT,
				shadowMapWidth, shadowMapHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glBindFramebuffer(GL_FRAMEBUFFER, pointShadowMapFBO);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthCubemap, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
		glm::mat4 shadowTransforms[] =
		{
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0))
		};

		shaderCube.Activate();
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[0]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[0]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[1]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[1]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[2]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[2]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[3]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[3]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[4]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[4]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[5]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[5]));
		glUniform3f(glGetUniformLocation(shaderCube.ID, "lightPos"), light.transform.position.x, light.transform.position.y, light.transform.position.z);
		glUniform1f(glGetUniformLocation(shaderCube.ID, "farPlane"), farPlane);




	}
	void Update(GameObject& light) {;
		glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
		glm::mat4 shadowTransforms[] =
		{
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)),
		shadowProj * glm::lookAt(light.transform.position, light.transform.position + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0))
		};

		float farPlane = 100.0f;
		glm::mat4 orthgonalProjection = glm::ortho(-35.0f, 35.0f, -35.0f, 35.0f, 0.1f, farPlane);
		glm::mat4 perspectiveProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
		glm::mat4 lightView = glm::lookAt(light.transform.position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 lightProjection = perspectiveProjection * lightView;

		shader.Activate();
		glUniformMatrix4fv(glGetUniformLocation(shader.ID, "lightProjection"), 1, GL_FALSE, glm::value_ptr(lightProjection));

		shaderCube.Activate();
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[0]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[0]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[1]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[1]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[2]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[2]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[3]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[3]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[4]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[4]));
		glUniformMatrix4fv(glGetUniformLocation(shaderCube.ID, "shadowMatrices[5]"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[5]));
		glUniform3f(glGetUniformLocation(shaderCube.ID, "lightPos"), light.transform.position.x, light.transform.position.y, light.transform.position.z);
		glUniform1f(glGetUniformLocation(shaderCube.ID, "farPlane"), farPlane);


		glEnable(GL_DEPTH_TEST);
		glViewport(0, 0, shadowMapWidth, shadowMapHeight);
		glBindFramebuffer(GL_FRAMEBUFFER, pointShadowMapFBO);
		glClear(GL_DEPTH_BUFFER_BIT);

	}
};

#endif