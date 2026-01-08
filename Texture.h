#ifndef TEXTURE_CLASS_H
#define TEXTURE_CLASS_H

#include<glad/glad.h>
#include<stb/stb_image.h>

#include"shaderClass.h"

class Texture
{
public:
	GLuint ID;
	const char* type;
	GLuint unit;
	GLuint FBO = 0;  // для порталов
	GLuint RBO = 0;  // для глубины/стенсила
	Texture(int width, int height, const char* texType, GLuint slot);
	Texture();
	Texture(const char* image, const char* texType, GLuint slot);
	Texture(const char* image, const char* texType, GLuint slot,bool Normal);
	Texture(const char* image, const char* texType, GLuint slot, bool Normal, bool Normals);
	// Assigns a texture unit to a texture
	void texUnit(Shader& shader, const char* uniform, GLuint unit);
	// Binds a texture
	void Bind();
	// Unbinds a texture
	void Unbind();
	// Deletes a texture
	void Delete();
};
#endif