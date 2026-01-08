#ifndef MESH_CLASS_H
#define MESH_CLASS_H

#include<string>

#include"Material.h"
#include"Transform.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"
static int meshid;

class Mesh
{
public:

	int ID = 0;
	std::vector <Vertex> vertices;
	std::vector <GLuint> indices;
	

	// Store VAO in public so it can be used in the Draw function
	VAO VAO;
	GLuint VBOS;
	// Initializes the mesh
	Mesh(std::vector <Vertex>& vertices, std::vector <GLuint>& indices);
	Mesh(){}
	
	// Draws the mesh
	void Draw
	(
		Shader& shader,
		Camera& camera,
		Transform& transform,
		Material& material
	);
	void DrawShadow
	(
		Shader& shader,
		Transform& transform
	);
};
#endif