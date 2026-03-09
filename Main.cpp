#include<filesystem>
namespace fs = std::filesystem;
#include <windows.h>
#include <string>
#include"Model.h"
#include"GameObject.h"
#include "LitShader.h"
#include "ShadowShader.h"
#include "PostProcessingShader.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include "Window.h"
#include <map>
#include <nlohmann/json.hpp>
#include "UI.h"
#include "Render.h"
#include "Serializer.h"
#include "CullingShader.h"
std::vector<Vertex> cubeVertices = {
	// Front face (normal Z+, tangent X, bitangent Y)
	Vertex{glm::vec3(-1,-1, 1), glm::vec3(0,0,1), glm::vec3(0), glm::vec2(0,0), glm::vec3(1,0,0), glm::vec3(0,1,0)},
	Vertex{glm::vec3(1,-1, 1),  glm::vec3(0,0,1), glm::vec3(0), glm::vec2(1,0), glm::vec3(1,0,0), glm::vec3(0,1,0)},
	Vertex{glm::vec3(1, 1, 1),  glm::vec3(0,0,1), glm::vec3(0), glm::vec2(1,1), glm::vec3(1,0,0), glm::vec3(0,1,0)},
	Vertex{glm::vec3(-1, 1, 1), glm::vec3(0,0,1), glm::vec3(0), glm::vec2(0,1), glm::vec3(1,0,0), glm::vec3(0,1,0)},

	// Back face (normal Z-, tangent -X, bitangent Y)
	Vertex{glm::vec3(1,-1,-1),  glm::vec3(0,0,-1), glm::vec3(0), glm::vec2(0,0), glm::vec3(-1,0,0), glm::vec3(0,1,0)},
	Vertex{glm::vec3(-1,-1,-1), glm::vec3(0,0,-1), glm::vec3(0), glm::vec2(1,0), glm::vec3(-1,0,0), glm::vec3(0,1,0)},
	Vertex{glm::vec3(-1, 1,-1), glm::vec3(0,0,-1), glm::vec3(0), glm::vec2(1,1), glm::vec3(-1,0,0), glm::vec3(0,1,0)},
	Vertex{glm::vec3(1, 1,-1),  glm::vec3(0,0,-1), glm::vec3(0), glm::vec2(0,1), glm::vec3(-1,0,0), glm::vec3(0,1,0)},

	// Left face (normal X-, tangent -Z, bitangent Y)
	Vertex{glm::vec3(-1,-1,-1), glm::vec3(-1,0,0), glm::vec3(0), glm::vec2(0,0), glm::vec3(0,0,-1), glm::vec3(0,1,0)},
	Vertex{glm::vec3(-1,-1, 1), glm::vec3(-1,0,0), glm::vec3(0), glm::vec2(1,0), glm::vec3(0,0,-1), glm::vec3(0,1,0)},
	Vertex{glm::vec3(-1, 1, 1), glm::vec3(-1,0,0), glm::vec3(0), glm::vec2(1,1), glm::vec3(0,0,-1), glm::vec3(0,1,0)},
	Vertex{glm::vec3(-1, 1,-1), glm::vec3(-1,0,0), glm::vec3(0), glm::vec2(0,1), glm::vec3(0,0,-1), glm::vec3(0,1,0)},

	// Right face (normal X+, tangent Z, bitangent Y)
	Vertex{glm::vec3(1,-1, 1),  glm::vec3(1,0,0), glm::vec3(0), glm::vec2(0,0), glm::vec3(0,0,1), glm::vec3(0,1,0)},
	Vertex{glm::vec3(1,-1,-1),  glm::vec3(1,0,0), glm::vec3(0), glm::vec2(1,0), glm::vec3(0,0,1), glm::vec3(0,1,0)},
	Vertex{glm::vec3(1, 1,-1),  glm::vec3(1,0,0), glm::vec3(0), glm::vec2(1,1), glm::vec3(0,0,1), glm::vec3(0,1,0)},
	Vertex{glm::vec3(1, 1, 1),  glm::vec3(1,0,0), glm::vec3(0), glm::vec2(0,1), glm::vec3(0,0,1), glm::vec3(0,1,0)},

	// Top face (normal Y+, tangent X, bitangent Z)
	Vertex{glm::vec3(-1, 1, 1), glm::vec3(0,1,0), glm::vec3(0), glm::vec2(0,0), glm::vec3(1,0,0), glm::vec3(0,0,1)},
	Vertex{glm::vec3(1, 1, 1),  glm::vec3(0,1,0), glm::vec3(0), glm::vec2(1,0), glm::vec3(1,0,0), glm::vec3(0,0,1)},
	Vertex{glm::vec3(1, 1,-1),  glm::vec3(0,1,0), glm::vec3(0), glm::vec2(1,1), glm::vec3(1,0,0), glm::vec3(0,0,1)},
	Vertex{glm::vec3(-1, 1,-1), glm::vec3(0,1,0), glm::vec3(0), glm::vec2(0,1), glm::vec3(1,0,0), glm::vec3(0,0,1)},

	// Bottom face (normal Y-, tangent X, bitangent -Z)
	Vertex{glm::vec3(-1,-1,-1), glm::vec3(0,-1,0), glm::vec3(0), glm::vec2(0,0), glm::vec3(1,0,0), glm::vec3(0,0,-1)},
	Vertex{glm::vec3(1,-1,-1),  glm::vec3(0,-1,0), glm::vec3(0), glm::vec2(1,0), glm::vec3(1,0,0), glm::vec3(0,0,-1)},
	Vertex{glm::vec3(1,-1, 1),  glm::vec3(0,-1,0), glm::vec3(0), glm::vec2(1,1), glm::vec3(1,0,0), glm::vec3(0,0,-1)},
	Vertex{glm::vec3(-1,-1, 1), glm::vec3(0,-1,0), glm::vec3(0), glm::vec2(0,1), glm::vec3(1,0,0), glm::vec3(0,0,-1)},
};

std::vector<GLuint> cubeIndices = {
	0, 1, 2, 0, 2, 3,       // Front
	4, 5, 6, 4, 6, 7,       // Back
	8, 9,10, 8,10,11,       // Left
   12,13,14,12,14,15,       // Right
   16,17,18,16,18,19,       // Top
   20,21,22,20,22,23        // Bottom
};
std::vector <GameObject> Objects;
std::vector <Material> material;
std::vector <Mesh> mesh;

std::string getExecutablePaths()
{
	char buffer[MAX_PATH];
	GetModuleFileNameA(NULL, buffer, MAX_PATH);
	std::filesystem::path exePath(buffer);
	return exePath.parent_path().string();
}
double crntTime = 0.0;

glm::vec3 GetMouseRay(Window& window, Camera& camera) {
	double mouseX, mouseY;
	glfwGetCursorPos(window.window, &mouseX, &mouseY);

	glm::vec4 viewport = glm::vec4(0, 0, window.width, window.height);
	glm::mat4 view = glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up);
	glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);

	// Точка на ближней плоскости (z = 0)
	glm::vec3 nearPos = glm::unProject(glm::vec3(mouseX, window.height - mouseY, 0.0f), view, proj, viewport);
	// Точка на дальней плоскости (z = 1)
	glm::vec3 farPos = glm::unProject(glm::vec3(mouseX, window.height - mouseY, 1.0f), view, proj, viewport);

	return glm::normalize(farPos - nearPos);
}
int main()
{
	Window window = Window();
	Objects.reserve(1000);
	std::string projectFolder = getExecutablePaths() + "/project";
	std::string sceneFile = projectFolder + "/level1.bhscene";
	std::vector<GameObject> Objects = Serializer::LoadScene(sceneFile, projectFolder);

	UI ui(window,getExecutablePaths() + "/project", getExecutablePaths());
	




	Objects.push_back(GameObject());
	Objects[Objects.size()-1].light.enable = true;
	CullingShader cullingshader;
	LitShader litshader;
	PostProcessingShader postprocessingshader(window);
	ShadowShader shadowshader;

	Camera camera(window.width, window.height, glm::vec3(0.0f, 0.0f, 2.0f));
	
	Render render;
	render.shadow = true;

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	// Переменная для хранения ID буфера
	unsigned int uboLights;
	glGenBuffers(1, &uboLights);

	// Привязываем буфер и выделяем под него память (размер нашей структуры)
	glBindBuffer(GL_UNIFORM_BUFFER, uboLights);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(LightUBOBlock), NULL, GL_DYNAMIC_DRAW);

	// Привязываем этот буфер к "точке подключения" №0
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboLights);
	glBindBuffer(GL_UNIFORM_BUFFER, 0); // Отвязываем, чтобы ничего не сломать

	while (!glfwWindowShouldClose(window.window))
	{
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		camera.Inputs(window.window);
		camera.updateMatrix(45.0f, 0.1f, 1000);

		
		static float lastFrame = 0.0f;
		float currentFrame = glfwGetTime();
		float deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		std::vector<glm::mat4> defaultBones(250, glm::mat4(1.0f));
		render.Draw(Objects, litshader, shadowshader, postprocessingshader, window, camera, glfwGetTime(), defaultBones, ui, uboLights);

		ui.Draw(window, camera, Objects,render);

		glfwSwapBuffers(window.window);

		glfwPollEvents();
	}

	litshader.shader.Delete();
	glDeleteFramebuffers(1, &postprocessingshader.FBO);
	glDeleteFramebuffers(1, &postprocessingshader.postProcessingFBO);
	glfwDestroyWindow(window.window);

	glfwTerminate();
	return 0;
}