#include"Texture.h"
#include <stdexcept>
#include <stdexcept>
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT                   0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT                  0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT                  0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT                  0x83F3
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT                  0x8C4C
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT            0x8C4D
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT            0x8C4E
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT            0x8C4F
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

    // Пытаемся загрузить. bytes - это просто указатель на массив в оперативной памяти
    unsigned char* bytes = stbi_load(image, &widthImg, &heightImg, &numColCh, 0);

    if (bytes == NULL) {
        std::cout << "[Texture Error] Failed to load: " << image << std::endl;
        return; // Вместо throw лучше просто выйти, чтобы движок не падал
    }

    glGenTextures(1, &ID);
    glActiveTexture(GL_TEXTURE0 + slot);
    unit = slot;
    glBindTexture(GL_TEXTURE_2D, ID);

    // ВАЖНО: Убираем выравнивание по 4 байта (спасает от 0xC0000005 на странных размерах)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    GLint internalFormat;
    GLenum format;

    // Определяем форматы максимально аккуратно
    if (typemap == 0) { // sRGB (Цвет)
        if (numColCh == 4) {
            internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
            format = GL_RGBA;
        }
        else if (numColCh == 3) {
            internalFormat = GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
            format = GL_RGB;
        }
        else {
            internalFormat = GL_COMPRESSED_RED_RGTC1;
            format = GL_RED;
        }
    }
    else { // Linear (Нормали, Маски)
        if (numColCh == 4) {
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            format = GL_RGBA;
        }
        else if (numColCh == 3) {
            internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            format = GL_RGB;
        }
        else {
            internalFormat = GL_COMPRESSED_RED_RGTC1;
            format = GL_RED;
        }
    }

    // Финальная проверка перед отправкой на GPU
    if (bytes) {
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, widthImg, heightImg, 0, format, GL_UNSIGNED_BYTE, bytes);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

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