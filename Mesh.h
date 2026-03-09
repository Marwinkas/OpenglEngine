#ifndef MESH_CLASS_H
#define MESH_CLASS_H
#include<string>
#include <cstddef>
#include"Material.h"
#include"Transform.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"
static int meshid;
struct LODLevel {
	unsigned int count;
	unsigned int firstIndex;
};
class Mesh
{
public:
	int ID = 0;
	std::vector <Vertex> vertices;
	std::vector <GLuint> indices;
	std::vector<LODLevel> lods;
	void* mappedInstanceVBO = nullptr;
	VAO VAO;
	GLuint VBOS;
	Mesh(std::vector <Vertex>& vertices, std::vector <GLuint>& indices);
	Mesh(){}
	float boundingRadius = 10.0f; 
};
#endif