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
MaterialGPUData Material::getGPUData() {
	MaterialGPUData data;
	data.albedoHandle = hasalbedo ? albedo.handle : 0;
	data.normalHandle = hasnormal ? normal.handle : 0;
	data.heightHandle = hasheight ? height.handle : 0;
	data.metallicHandle = hasmetallic ? metallic.handle : 0;
	data.roughnessHandle = hasroughness ? roughness.handle : 0;
	data.aoHandle = hasao ? ao.handle : 0;

	data.hasAlbedo = hasalbedo;
	data.hasNormal = hasnormal;
	data.hasHeight = hasheight;
	data.hasMetallic = hasmetallic;
	data.hasRoughness = hasroughness;
	data.hasAO = hasao;

	return data;
}
void Material::setAlbedo(std::string path) {
	if (hasalbedo) {
		glDeleteTextures(1, &albedo.ID);
	}

	albedo = Texture(path.c_str(), "diffuse", 0,0);
	hasalbedo = true;
}
void Material::setNormal(std::string path) {
	if (hasnormal) {
		glDeleteTextures(1, &normal.ID);
	}
	normal = Texture(path.c_str(), "normal", 1, 1);
	hasnormal = true;
}
void Material::setHeight(std::string path) {
	if (hasheight) {
		glDeleteTextures(1, &height.ID);
	}
	height = Texture(path.c_str(), "height", 2, 2);
	hasheight = true;
}
void Material::setMetallic(std::string path) {
	if (hasmetallic) {
		glDeleteTextures(1, &metallic.ID);
	}
	metallic = Texture(path.c_str(), "metallic", 3, 1);
	hasmetallic = true;
}
void Material::setRoughness(std::string path) {
	if (hasroughness) {
		glDeleteTextures(1, &roughness.ID);
	}
	roughness = Texture(path.c_str(), "roughness", 4, 1);
	hasroughness = true;
}
void Material::setAO(std::string path) {
	if (hasao) {
		glDeleteTextures(1, &ao.ID);
	}
	ao = Texture(path.c_str(), "ao", 5, 1);
	hasao = true;
}
void Material::Activate(Shader& shader) {
	glUniform1i(glGetUniformLocation(shader.ID, "materialID"), this->ID);
}