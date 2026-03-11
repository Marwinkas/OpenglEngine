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
#include "VoxelShader.h"
#include "GLTexture.h"
#include "GLBuffer.h"
#include <entt/entt.hpp>
#include "Components.h"
#include <map> 
class DefferedShader;
class UI;
class CullingShader;
struct LightGrid {
	uint32_t offset;
	uint32_t count;
};
struct ClusterAABB {
	glm::vec4 minPoint; 	glm::vec4 maxPoint;
};
class Render {
public:
	Render();
public:
	GLBuffer materialBuffer;
	GLBuffer clusterBuffer;
	GLBuffer lightGridBuffer;
	GLBuffer globalLightIndexBuffer;
	GLBuffer atomicCounterBuffer;
	GLBuffer uboLights;

	SkyAtmosphere sky; // Твое небо
	glm::vec3 sunDir = glm::vec3(0.0f, -1.0f, 0.0f);

	GLuint gBufferFBO;
	GLuint gPositionMetallic;
	GLuint gNormalRoughness;
	GLuint gAlbedoAO;
	GLuint rboDepth;

	// Метод инициализации
	void InitGBuffer(int width, int height);

	// Главная функция стала гораздо короче
	void Draw(entt::registry& registry, LitShader& geometryShader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime, UI& ui, CullingShader& cullingshader, DefferedShader& defferedShader);

	void UpdateClusterGrid(Camera& camera, Window& window, CullingShader& cullingshader);
	glm::mat4 CalculateCalculateCSMMatrix(float nearP, float farP, Camera& camera, float shadowSize);

private:
	

	void UpdateTransforms(entt::registry& registry);

	glm::vec3 SetupLightsAndUBO(entt::registry& registry, ShadowShader& shadowshader);

	// Вспомогательные функции тоже меняются (теперь они принимают std::vector<entt::entity> вместо std::vector<GameObject*>)
	void DispatchGPUCulling(Mesh* mesh, std::vector<entt::entity>& batchObjects, entt::registry& registry, const glm::mat4& viewProj, const glm::vec3& camPos, bool isShadowPass, CullingShader& cullingshader, Window& window, GLuint hiZTex);

	std::vector<glm::mat4> RenderCSM(Camera& camera, std::map<Mesh*, std::vector<entt::entity>>& shadowBatches, entt::registry& registry, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window);

	void RenderAtlasShadows(Camera& camera, entt::registry& registry, std::map<Mesh*, std::vector<entt::entity>>& shadowBatches, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window);

	void RenderMainPass(Camera& camera, Window& window, std::map<std::pair<Mesh*, Material*>, std::vector<entt::entity>>& mainBatches, entt::registry& registry, std::vector<glm::mat4>& sunMatrices, LitShader& geometryShader, DefferedShader& deferredShader, ShadowShader& shadowshader, CullingShader& cullingshader, PostProcessingShader& ppShader);
};
#endif