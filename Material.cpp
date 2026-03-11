#include "Material.h"
#include <windows.h>
#include <string>
#include <filesystem>
#include <iostream>
std::string getExecutablePath()
{
	char buffer[MAX_PATH];
	GetModuleFileNameA(NULL, buffer, MAX_PATH);
	std::filesystem::path exePath(buffer);
	std::string path = exePath.parent_path().string();

	// Лёша, это поможет нам понять, не "врет" ли путь
	std::cout << "[DEBUG] Путь к EXE: " << path << std::endl;

	return path;
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
	data.triplanarScale = 0.1f;
	data.useTriplanar = 0;

	return data;
}
// Универсальный внутренний помощник для загрузки
bool SafeLoad(std::string path, bool& flag, Texture& tex, const char* type, int slot, int format) {
	if (path.empty() || !fs::exists(path) || fs::is_directory(path)) {
		return false;
	}

	if (flag) {
		glDeleteTextures(1, &tex.ID);
	}

	tex = Texture(path.c_str(), type, slot, format);
	flag = true;
	return true;
}

void Material::setAlbedo(std::string path) { SafeLoad(path, hasalbedo, albedo, "diffuse", 0, 0); }
void Material::setNormal(std::string path) { SafeLoad(path, hasnormal, normal, "normal", 1, 1); }
void Material::setHeight(std::string path) { SafeLoad(path, hasheight, height, "height", 2, 2); }
void Material::setMetallic(std::string path) { SafeLoad(path, hasmetallic, metallic, "metallic", 3, 1); }
void Material::setRoughness(std::string path) { SafeLoad(path, hasroughness, roughness, "roughness", 4, 1); }
void Material::setAO(std::string path) { SafeLoad(path, hasao, ao, "ao", 5, 1); }
void Material::Activate(Shader& shader) {
	glUniform1i(glGetUniformLocation(shader.ID, "materialID"), this->ID);
}