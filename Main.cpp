#include<filesystem>
namespace fs = std::filesystem;
#include"Model.h"
#include"GameObject.h"
#include "tinyfiledialogs.h"
#include "LitShader.h"
#include "ShadowShader.h"
#include "PostProcessingShader.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"
#include "Window.h"
#include <map>
#include <nlohmann/json.hpp>
#include "UI.h"
#include "Render.h"
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
void TestObj() {
	Objects.push_back(GameObject());
	Objects.push_back(GameObject());
	Objects.push_back(GameObject());


	material.push_back(Material());
	material.push_back(Material());
	mesh.push_back(Mesh(cubeVertices, cubeIndices));


	material[0].setAlbedo("albedo.png");
	material[0].setNormal("normal.png");
	material[0].setMetallic("metallic.png");
	material[0].setRoughness("roughness.png");
	material[0].setAO("ao.png");


	Objects[1].transform.position = glm::vec3(5, 0, 5);
	Objects[1].light.enable = true;
	Objects[0].mesh = &mesh[0];
	Objects[0].material = &material[0];

	Objects[2].mesh = &mesh[0];
	Objects[2].material = &material[0];
	Objects[2].transform.position = glm::vec3(0, -2, 0);
	Objects[2].transform.scale = glm::vec3(5, 1, 5);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);
	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);	Objects.push_back(Objects[2]);
}

double prevTime = 0.0;
double crntTime = 0.0;
double deltaTime = 0.0;
int frameCount = 0;
void FPS(Window& window) {
	crntTime = glfwGetTime();
	deltaTime = crntTime - prevTime;
	prevTime = crntTime;

	frameCount++;

	static double timer = 0.0;
	timer += deltaTime;

	if (timer >= 1.0) {
		double fps = double(frameCount) / timer;
		double msPerFrame = 1000.0 / fps;

		std::string newTitle =
			"YoutubeOpenGL - " + std::to_string((int)fps) + " FPS / " +
			std::to_string((int)msPerFrame) + " ms";

		glfwSetWindowTitle(window.window, newTitle.c_str());

		frameCount = 0;
		timer = 0.0;
	}
}
int main()
{
	Window window = Window();
	UI ui(window);

	TestObj();

	LitShader litshader(Objects[1]);
	PostProcessingShader postprocessingshader(window);
	ShadowShader shadowshader(Objects[1]);

	Camera camera(window.width, window.height, glm::vec3(0.0f, 0.0f, 2.0f));

	Render render;
	render.sort(Objects);
	glfwSwapInterval(0);
	while (!glfwWindowShouldClose(window.window))
	{
		glEnable(GL_DEPTH_TEST);
		camera.Inputs(window.window);
		camera.updateMatrix(45.0f, 0.1f, 100);

		FPS(window);

		render.Draw(Objects, litshader, shadowshader, postprocessingshader, window, camera, crntTime);

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