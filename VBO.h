#ifndef VBO_CLASS_H
#define VBO_CLASS_H
#include<glm/glm.hpp>
#include<glad/glad.h>
#include<vector>
#define MAX_BONE_INFLUENCE 4
// Structure to standardize the vertices used in the meshes
struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 texUV;
	glm::vec3 tangent;
	glm::vec3 bitangent;
	int m_BoneIDs[MAX_BONE_INFLUENCE]; // Номера костей (до 4 штук)
	float m_Weights[MAX_BONE_INFLUENCE]; // Сила влияния каждой кости
};
class VBO
{
public:
	// Reference ID of the Vertex Buffer Object
	GLuint ID;
	// Constructor that generates a Vertex Buffer Object and links it to vertices
	VBO(std::vector<Vertex>& vertices);
	VBO() {};
	// Binds the VBO
	void Bind();
	// Unbinds the VBO
	void Unbind();
	// Deletes the VBO
	void Delete();
};
#endif