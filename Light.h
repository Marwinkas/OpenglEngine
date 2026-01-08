#ifndef LIGHT_CLASS_H
#define LIGHT_CLASS_H
#include<glm/glm.hpp>
#include<glad/glad.h>
#include<vector>

class Light {
public:
	glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	bool enable = false;
};

#endif