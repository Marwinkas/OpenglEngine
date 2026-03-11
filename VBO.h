#ifndef VBO_CLASS_H
#define VBO_CLASS_H
#include<glm/glm.hpp>
#include<glad/glad.h>
#include<vector>
#define MAX_BONE_INFLUENCE 4
struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 texUV;
	glm::vec3 tangent;
	glm::vec3 bitangent;
	int m_BoneIDs[MAX_BONE_INFLUENCE]; 	float m_Weights[MAX_BONE_INFLUENCE]; };
class VBO
{
public:
		GLuint ID;
		VBO(std::vector<Vertex>& vertices);
		VBO() : ID(0) {}
		void Bind();
		void Unbind();
		void Delete();
};
#endif