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
#include <map> 
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

	GLTexture voxelTex;
	VoxelShader voxelShader;
	Shader voxelMipmapShader;

	SkyAtmosphere sky; // Твое небо
	bool needsVoxelization = true;
	int voxelGridSize = 128;
	float voxelWorldSize = 100.0f;
	glm::vec3 gridMin;
	glm::vec3 gridMax;
	glm::vec3 sunDir = glm::vec3(0.0f, -1.0f, 0.0f);


	// Главная функция стала гораздо короче
	void Draw(std::vector<GameObject>& Objects, LitShader& litshader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime, UI& ui, CullingShader& cullingshader);

	void UpdateClusterGrid(Camera& camera, Window& window, CullingShader& cullingshader);
	glm::mat4 CalculateCalculateCSMMatrix(float nearP, float farP, Camera& camera, float shadowSize);
	void Voxelization(glm::vec3 Position, std::vector<GameObject>& Objects, Window& window);

private:
	// --- НОВЫЕ ПОМОЩНИКИ, ЧТОБЫ РАЗГРУЗИТЬ DRAW ---

	void UpdateTransforms(std::vector<GameObject>& Objects);
	glm::vec3 SetupLightsAndUBO(std::vector<GameObject>& Objects, ShadowShader& shadowshader);

	// Тот самый универсальный метод, который убрал 150 строк дублирования
	void DispatchGPUCulling(Mesh* mesh, std::vector<GameObject*>& batchObjects, const glm::mat4& viewProj, const glm::vec3& camPos, bool isShadowPass, CullingShader& cullingshader, Window& window, GLuint hiZTex);

	std::vector<glm::mat4> RenderCSM(Camera& camera, std::map<Mesh*, std::vector<GameObject*>>& shadowBatches, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window);
	void RenderAtlasShadows(Camera& camera, std::vector<GameObject>& Objects, std::map<Mesh*, std::vector<GameObject*>>& shadowBatches, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window);
	void RenderMainPass(Camera& camera, Window& window, std::map<std::pair<Mesh*, Material*>, std::vector<GameObject*>>& mainBatches, std::vector<glm::mat4>& sunMatrices,LitShader& litshader, ShadowShader& shadowshader, CullingShader& cullingshader, PostProcessingShader& ppShader);
};
#endif