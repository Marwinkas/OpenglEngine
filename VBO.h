#ifndef VBO_CLASS_H
#define VBO_CLASS_H
#include<glm/glm.hpp>
#include<glad/glad.h>
#include<vector>
#include <glm/gtc/packing.hpp> // Убедись, что это подключено

inline uint32_t PackNormalTo10_10_10_2(glm::vec3 normal) {
	// ЗАЩИТА ОТ NaN: Если вектор пустой, даем ему дефолтное направление
	if (glm::length(normal) < 0.0001f) {
		normal = glm::vec3(0.0f, 1.0f, 0.0f);
	}
	else {
		normal = glm::normalize(normal);
	}

	// Встроенный упаковщик GLM (идеально переводит в GL_INT_2_10_10_10_REV)
	return glm::packSnorm3x10_1x2(glm::vec4(normal, 0.0f));
}
struct Vertex
{
	glm::vec3 position = glm::vec3(0.0f);
	uint32_t normal = 0;
	uint32_t texUV = 0;
	uint32_t tangent = 0;
	uint32_t bitangent = 0;
};
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