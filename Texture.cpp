#include"Texture.h"
#include <stdexcept>
#include <stdexcept>
Texture::Texture()
{
}
void Texture::makeResident() {
	if (handle == 0) {
		handle = glGetTextureHandleARB(ID);
		glMakeTextureHandleResidentARB(handle);
	}
}
Texture::Texture(int width, int height, const char* texType, GLuint slot)
{
	type = texType;
	unit = slot;
		glGenTextures(1, &ID);
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, ID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	makeResident();
	glBindTexture(GL_TEXTURE_2D, 0);
		glGenFramebuffers(1, &FBO);
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ID, 0);
		glGenRenderbuffers(1, &RBO);
	glBindRenderbuffer(GL_RENDERBUFFER, RBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, RBO);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Portal FBO error" << std::endl;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
Texture::Texture(const char* image, const char* texType, GLuint slot, int typemap)
{
	type = texType;
	int widthImg, heightImg, numColCh;
	stbi_set_flip_vertically_on_load(false);
	unsigned char* bytes = stbi_load(image, &widthImg, &heightImg, &numColCh, 0);
	if (bytes == NULL) {
		throw std::invalid_argument("Failed to load texture file");
	}
	glGenTextures(1, &ID);
	glActiveTexture(GL_TEXTURE0 + slot);
	unit = slot;
	glBindTexture(GL_TEXTURE_2D, ID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	if (typemap != 2) {
		int col1 = GL_SRGB_ALPHA;
		int col2 = GL_RGB8;
		int col3 = GL_SRGB;
		if (typemap == 1) {
			col1 = GL_RGBA;
			col2 = GL_RGB;
			col3 = GL_R8;
		}
		if (numColCh == 4)
			glTexImage2D
			(
				GL_TEXTURE_2D,
				0,
				col1,
				widthImg,
				heightImg,
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				bytes
			);
		else if (numColCh == 3)
			glTexImage2D
			(
				GL_TEXTURE_2D,
				0,
				col2,
				widthImg,
				heightImg,
				0,
				GL_RGB,
				GL_UNSIGNED_BYTE,
				bytes
			);
		else if (numColCh == 1)
			glTexImage2D
			(
				GL_TEXTURE_2D,
				0,
				col3,
				widthImg,
				heightImg,
				0,
				GL_RED,
				GL_UNSIGNED_BYTE,
				bytes
			);
		else
			throw std::invalid_argument("Automatic Texture type recognition failed");
	}
	else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, widthImg, heightImg, 0, GL_RED, GL_UNSIGNED_BYTE, bytes);
	}
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(bytes);
	makeResident();
	glBindTexture(GL_TEXTURE_2D, 0);
}
void Texture::texUnit(Shader& shader, const char* uniform, GLuint unit)
{
	GLuint texUni = glGetUniformLocation(shader.ID, uniform);
	shader.Activate();
	glUniform1i(texUni, unit);
}
void Texture::Bind()
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, ID);
}
void Texture::Unbind()
{
	glBindTexture(GL_TEXTURE_2D, 0);
}
void Texture::Delete()
{
	glDeleteTextures(1, &ID);
}