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
struct ClusterAABB {
	glm::vec4 minPoint; // .w не используется (выравнивание)
	glm::vec4 maxPoint;
};
class Render {
public:
	Render();
public:
	GLuint clusterSSBO = 0;             // Сетка кубиков (AABB)
	GLuint lightGridSSBO = 0;           // Картотека (смещение и количество ламп)
	GLuint globalLightIndexListSSBO = 0; // Список ID всех ламп
	GLuint atomicCounterSSBO = 0;       // Общий счетчик для GPU
	bool lit = true;
	bool lightupdate = false;
	bool shadow = false;
	SkyAtmosphere sky;
	
	void Draw(std::vector<GameObject>& Objects, LitShader& litshader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime,
		std::vector<glm::mat4>& boneTransforms, UI& ui, unsigned int uboLights, CullingShader& cullingshader);
	void UpdateClusterGrid(Camera& camera, Window& window, CullingShader& cullingshader);
};
#endif