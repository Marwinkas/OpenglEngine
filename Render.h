#ifndef RENDER_CLASS_H
#define RENDER_CLASS_H
#include "Mesh.h"
#include "LitShader.h"
#include "ShadowShader.h"
#include "GameObject.h"
#include "PostProcessingShader.h"
#include "Window.h"
#include <unordered_map>
#include "SkyAtmosphere.h"
class UI;
class CullingShader;
struct LightGrid {
	uint32_t offset;
	uint32_t count;
};
class Render {
public:
	Render() {
		glGenBuffers(1, &materialSSBO);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialSSBO);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(MaterialGPUData), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialSSBO);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	};
public:
	bool lit = true;
	bool lightupdate = false;
	bool shadow = false;
	SkyAtmosphere sky;
	GLuint clusterSSBO = 0;      
	GLuint lightGridSSBO = 0;         
	GLuint globalLightIndexListSSBO = 0; 
	GLuint atomicCounterSSBO = 0;    
	GLuint materialSSBO;
	void Draw(std::vector<GameObject>& Objects, LitShader& litshader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime,
		std::vector<glm::mat4>& boneTransforms, UI& ui, unsigned int uboLights);
};
#endif