#ifndef WINDOW_CLASS_H
#define WINDOW_CLASS_H
#include<glm/glm.hpp>
#include<glm/gtc/quaternion.hpp>
#include<glad/glad.h>
#include<vector>
#include <GLFW/glfw3.h>
#include <iostream>
class Window {
public:
	Window() {


		glfwInit();

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		window = glfwCreateWindow(width, height, "YoutubeOpenGL", NULL, NULL);
		if (window == NULL)
		{
			std::cout << "Failed to create GLFW window" << std::endl;
			glfwTerminate();
		}

		glfwMakeContextCurrent(window);

		gladLoadGL();
		glViewport(0, 0, width, height);

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_MULTISAMPLE);
	}
	GLFWwindow* window;
	
	int width = 1280;
	int height = 720;
};

#endif