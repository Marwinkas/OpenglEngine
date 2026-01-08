#include "Material.h"
#include <windows.h>
#include <string>
#include <filesystem>

std::string getExecutablePath()
{
	char buffer[MAX_PATH];
	GetModuleFileNameA(NULL, buffer, MAX_PATH);
	std::filesystem::path exePath(buffer);
	return exePath.parent_path().string();
}
void Material::setAlbedo(std::string path) {
	albedo = Texture((getExecutablePath() + "/textures/" + path).c_str(), "diffuse", 0);
	hasalbedo = true;
}
void Material::setNormal(std::string path) {
	normal = Texture((getExecutablePath() + "/textures/" + path).c_str(), "normal", 1, true);
	hasnormal = true;
}
void Material::setHeight(std::string path) {
	height = Texture((getExecutablePath() + "/textures/" + path).c_str(), "height", 2, true, true);
	hasheight = true;
}
void Material::setMetallic(std::string path) {
	metallic = Texture((getExecutablePath() + "/textures/" + path).c_str(), "metallic", 3, true);
	hasmetallic = true;
}
void Material::setRoughness(std::string path) {
	roughness = Texture((getExecutablePath() + "/textures/" + path).c_str(), "roughness", 4, true);
	hasroughness = true;
}
void Material::setAO(std::string path) {
	ao = Texture((getExecutablePath() + "/textures/" + path).c_str(), "ao", 5, true);
	hasao = true;
}
void Material::Activate(Shader& shader) {

	glUniform1i(glGetUniformLocation(shader.ID, "hasAlbedo"), hasalbedo);
	glUniform1i(glGetUniformLocation(shader.ID, "hasNormal"), hasnormal);
	glUniform1i(glGetUniformLocation(shader.ID, "hasHeight"), hasheight);
	glUniform1i(glGetUniformLocation(shader.ID, "hasMetallic"), hasmetallic);
	glUniform1i(glGetUniformLocation(shader.ID, "hasRoughness"), hasroughness);
	glUniform1i(glGetUniformLocation(shader.ID, "hasAO"), hasao);
	if (hasalbedo) {
		albedo.Bind();
		glUniform1i(glGetUniformLocation(shader.ID, "diffuse0"), 0);
	}
	if (hasnormal) {
		normal.Bind();
		glUniform1i(glGetUniformLocation(shader.ID, "normal0"), 1);
	}
	if (hasheight) {
		height.Bind();
		glUniform1i(glGetUniformLocation(shader.ID, "height0"), 2);
	}
	if (hasmetallic) {
		metallic.Bind();
		glUniform1i(glGetUniformLocation(shader.ID, "metallic0"), 3);
	}
	if (hasroughness) {
		roughness.Bind();
		glUniform1i(glGetUniformLocation(shader.ID, "roughness0"), 4);
	}
	if (hasao) {
		ao.Bind();
		glUniform1i(glGetUniformLocation(shader.ID, "ao0"), 5);
	}






}