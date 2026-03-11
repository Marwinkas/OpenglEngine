#include "Render.h"
#include "UI.h" 
#include "CullingShader.h"
void UpdateMatrices(std::vector<GameObject>& objects, int index, glm::mat4 parentMatrix) {
	GameObject& obj = objects[index];
	glm::mat4 localMatrix = glm::mat4(1.0f);
	ImGuizmo::RecomposeMatrixFromComponents(
		glm::value_ptr(obj.transform.position),
		glm::value_ptr(obj.transform.rotation),
		glm::value_ptr(obj.transform.scale),
		glm::value_ptr(localMatrix)
	);
	obj.transform.matrix = parentMatrix * localMatrix;
	for (int childIdx : obj.children) {
		UpdateMatrices(objects, childIdx, obj.transform.matrix);
	}
}
Render::Render() {
	materialBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(MaterialGPUData), nullptr, GL_DYNAMIC_DRAW, 2);
	clusterBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(ClusterAABB), nullptr, GL_STATIC_COPY, 3);
	lightGridBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(LightGrid), nullptr, GL_DYNAMIC_DRAW, 4);
	globalLightIndexBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * 100 * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW, 5);
	atomicCounterBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW, 6);

	uboLights = GLBuffer(GL_UNIFORM_BUFFER, sizeof(LightUBOBlock), NULL, GL_DYNAMIC_DRAW, 0);

	voxelTex.Create3D(voxelGridSize, voxelGridSize, voxelGridSize, GL_RGBA8, true);
	voxelTex.SetFilter(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
	voxelTex.SetWrap(GL_CLAMP_TO_BORDER);
	voxelTex.SetBorderColor(0.0f, 0.0f, 0.0f, 0.0f);
	voxelShader;
	voxelMipmapShader = Shader("voxel_mipmap.comp");
};
void Render::Draw(std::vector<GameObject>& Objects, LitShader& litshader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime, UI& ui, CullingShader& cullingshader) {
	glEnable(GL_CULL_FACE);

	UpdateTransforms(Objects);

	GLuint zero = 0;
	atomicCounterBuffer.UpdateData(0, sizeof(GLuint), &zero);
	sunDir = SetupLightsAndUBO(Objects, shadowshader);

	if (needsVoxelization) {
		Voxelization(camera.Position, Objects, window);
		needsVoxelization = false; // Выключаем, чтобы не пересчитывать каждый кадр
	}


	cullingshader.lightCullingShader.Activate();
	glUniformMatrix4fv(glGetUniformLocation(cullingshader.lightCullingShader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(camera.GetViewMatrix()));

	int lightCount = 0;
	for (auto& obj : Objects) { if (obj.light.enable) lightCount++; }
	glUniform1ui(glGetUniformLocation(cullingshader.lightCullingShader.ID, "totalLights"), lightCount);
	glDispatchCompute(1, 1, 24);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	std::map<Mesh*, std::vector<GameObject*>> shadowBatches;
	std::map<std::pair<Mesh*, Material*>, std::vector<GameObject*>> mainBatches;
	for (auto& obj : Objects) {
		for (auto& subMesh : obj.renderer.subMeshes) {
			if (obj.castShadow && obj.isVisible) shadowBatches[subMesh.mesh].push_back(&obj);
			if (obj.isVisible) mainBatches[{subMesh.mesh, subMesh.material}].push_back(&obj);
		}
	}


	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	std::vector<glm::mat4> sunMatrices = RenderCSM(camera, shadowBatches, shadowshader, cullingshader, window);
	RenderAtlasShadows(camera, Objects, shadowBatches, shadowshader, cullingshader, window);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glViewport(0, 0, window.width, window.height);
	RenderMainPass(camera, window, mainBatches, sunMatrices, litshader,shadowshader, cullingshader, postprocessingshader);
 
	glDisable(GL_CULL_FACE);
	sky.Draw(camera, sunDir);
	postprocessingshader.Update(window, crntTime, camera, ui,sunDir, uboLights.ID, shadowshader, voxelTex.ID, gridMin, gridMax);
}
void Render::UpdateTransforms(std::vector<GameObject>& Objects) {
	for (int i = 0; i < Objects.size(); i++) {
		if (Objects[i].parentID == -1) {
			UpdateMatrices(Objects, i, glm::mat4(1.0f));
		}
	}
}
glm::vec3 Render::SetupLightsAndUBO(std::vector<GameObject>& Objects, ShadowShader& shadowshader) {
	LightUBOBlock lightBlock;
	lightBlock.activeLightsCount = 0;

	static glm::vec3 lastSunDir = glm::vec3(0.0f); // Храним старое направление
	glm::vec3 currentSunDir = glm::vec3(0.0f, -1.0f, 0.0f);

	for (auto& obj : Objects) {
		if (!obj.light.enable || obj.light.type == LightType::None) continue;

		if (obj.light.type == LightType::Directional) {
			glm::quat qRot = glm::quat(glm::radians(obj.transform.rotation));
			currentSunDir = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));

			// МАГИЯ: Если солнце повернулось — перерисовываем кэш статики!
			if (glm::distance(currentSunDir, lastSunDir) > 0.0001f) {
				shadowshader.staticShadowsDirty = true;
				lastSunDir = currentSunDir;
			}
		}
		// ... остальной твой код (заполнение lightBlock) ...
		int i = lightBlock.activeLightsCount;
		// (копируй сюда заполнение структур lights[i] из своего кода)
		float lightType = (float)obj.light.type;
		lightBlock.lights[i].posType = glm::vec4(obj.transform.position, lightType);
		lightBlock.lights[i].colorInt = glm::vec4(obj.light.color, obj.light.intensity);
		glm::quat qRot = glm::quat(glm::radians(obj.transform.rotation));
		glm::vec3 direction = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));
		lightBlock.lights[i].dirRadius = glm::vec4(direction, obj.light.radius);
		if (obj.light.type == LightType::Spot) {
			glm::mat4 lightProj = glm::perspective(glm::radians(obj.light.outerCone * 2.0f), 1.0f, 0.1f, 1000.0f);
			glm::mat4 lightView = glm::lookAt(obj.transform.position, obj.transform.position + direction, glm::vec3(0.0f, 1.0f, 0.0f));
			obj.light.lightSpaceMatrix = lightProj * lightView;
		}
		float inner = glm::cos(glm::radians(obj.light.innerCone));
		float outer = glm::cos(glm::radians(obj.light.outerCone));
		float castsShadows = obj.light.castShadows ? 1.0f : 0.0f;
		float shadowSlot = (float)obj.light.shadowSlot;
		lightBlock.lights[i].shadowParams = glm::vec4(inner, outer, castsShadows, shadowSlot);
		lightBlock.lights[i].lightSpaceMatrix = obj.light.lightSpaceMatrix;

		lightBlock.activeLightsCount++;
		if (lightBlock.activeLightsCount >= 100) break;
	}

	uboLights.UpdateData(0, sizeof(LightUBOBlock), &lightBlock);
	return currentSunDir;
}

void Render::DispatchGPUCulling(Mesh* mesh, std::vector<GameObject*>& batchObjects, const glm::mat4& viewProj, const glm::vec3& camPos, bool isShadowPass, CullingShader& cullingshader, Window& window, GLuint hiZTex) {
	if (batchObjects.empty()) return;

	std::vector<glm::mat4> matrices;
	std::vector<BoundingSphere> spheres;
	std::vector<DrawCommand> commands;

	for (int i = 0; i < batchObjects.size(); ++i) {
		GameObject* obj = batchObjects[i];
		matrices.push_back(obj->transform.matrix);
		float maxScale = std::max({ obj->transform.scale.x, obj->transform.scale.y, obj->transform.scale.z });
		spheres.push_back({ glm::vec4(obj->transform.position, mesh->boundingRadius * maxScale) });

		DrawCommand cmd = {};
		cmd.count = mesh->indices.size();
		cmd.instanceCount = 0;
		cmd.firstIndex = 0;
		cmd.baseVertex = 0;
		cmd.baseInstance = i;
		commands.push_back(cmd);
	}

	mesh->VBOS.SetData(matrices.size() * sizeof(glm::mat4), matrices.data());

	cullingshader.ssboObjects.SetData(spheres.size() * sizeof(BoundingSphere), spheres.data());
	// ГОВОРИМ COMPUTE ШЕЙДЕРУ, ЧТО СФЕРЫ В СЛОТЕ 0
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cullingshader.ssboObjects.ID);

	cullingshader.ssboCommands.SetData(commands.size() * sizeof(DrawCommand), commands.data());
	// ГОВОРИМ COMPUTE ШЕЙДЕРУ, ЧТО КОМАНДЫ В СЛОТЕ 1
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cullingshader.ssboCommands.ID);

	cullingshader.Activate();

	cullingshader.Set(cullingshader.loc.camPos, camPos);
	cullingshader.Set(cullingshader.loc.isShadowPass, isShadowPass ? 1 : 0);

	float lodDist[2] = { 50.0f, 1000.0f };
	cullingshader.Set(cullingshader.loc.lodDistances,2, lodDist);

	for (int i = 0; i < 3; i++) {
		unsigned int count = (i < mesh->lods.size()) ? mesh->lods[i].count : mesh->lods[0].count;
		unsigned int offset = (i < mesh->lods.size()) ? mesh->lods[i].firstIndex : mesh->lods[0].firstIndex;

		cullingshader.Set(cullingshader.loc.lodCount[i], count);
		cullingshader.Set(cullingshader.loc.lodOffset[i], offset);
	}

	auto planes = cullingshader.ExtractFrustumPlanes(viewProj);

	for (int i = 0; i < 6; i++) {
		// Используем наш новый быстрый Set с 4-мя флоатами (normal.xyz + distance)
		cullingshader.Set(
			cullingshader.loc.frustumPlanes[i],
			planes[i].normal.x,
			planes[i].normal.y,
			planes[i].normal.z,
			planes[i].distance
		);
	}
	cullingshader.Set(cullingshader.loc.objectCount, (unsigned int)batchObjects.size());
	cullingshader.Set(cullingshader.loc.viewProjection, viewProj);;

	if (!isShadowPass) {
		cullingshader.Set(cullingshader.loc.screenSize, (float)window.width, (float)window.height);
		cullingshader.Set(cullingshader.loc.hiZTexture, hiZTex,0);
	}

	GLuint workGroups = (batchObjects.size() + 255) / 256;
	glDispatchCompute(workGroups, 1, 1);
	glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
}
std::vector<glm::mat4> Render::RenderCSM(Camera& camera, std::map<Mesh*, std::vector<GameObject*>>& shadowBatches, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window) {
	shadowshader.Activate();
	glEnable(GL_DEPTH_CLAMP);
	static int frameCounter = 0;
	frameCounter++;

	static std::vector<glm::mat4> sunMatrices(4, glm::mat4(1.0f));
	float lastSplit = 0.1f;
	for (int i = 0; i < 4; i++) {
		bool shouldUpdate = true;
		if (i == 1 && frameCounter % 2 != 0) shouldUpdate = false;
		if (i == 2 && frameCounter % 3 != 0) shouldUpdate = false;
		if (i == 3 && frameCounter % 4 != 0) shouldUpdate = false;
		if (shouldUpdate) sunMatrices[i] = CalculateCalculateCSMMatrix(lastSplit, shadowshader.cascadeSplits[i], camera, (float)shadowshader.sunShadowSize);
		lastSplit = shadowshader.cascadeSplits[i];
	}

	glBindFramebuffer(GL_FRAMEBUFFER, shadowshader.sunFBO);
	glViewport(0, 0, shadowshader.sunShadowSize, shadowshader.sunShadowSize);

	for (int i = 0; i < 4; i++) {
		// Выясняем, обновилась ли матрица для этого каскада в этом кадре
		bool matrixUpdated = (i == 0) ||
			(i == 1 && frameCounter % 2 == 0) ||
			(i == 2 && frameCounter % 3 == 0) ||
			(i == 3 && frameCounter % 4 == 0);

		if (!matrixUpdated) continue;

		// ЕСЛИ МАТРИЦА ОБНОВИЛАСЬ — статика в кэше для этого каскада СДОХЛА.
		// Мы обязаны её перерисовать под новую матрицу.

		// 1. Рисуем статику в кэш под НОВУЮ матрицу
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowshader.staticSunShadowArray, 0, i);
		glClear(GL_DEPTH_BUFFER_BIT);

		for (auto& batch : shadowBatches) {
			std::vector<GameObject*> statics;
			for (auto* o : batch.second) if (o->isStatic) statics.push_back(o);
			if (!statics.empty()) {
				DispatchGPUCulling(batch.first, statics, sunMatrices[i], camera.Position, true, cullingshader, window, 0);
				shadowshader.Activate();
				shadowshader.Set(shadowshader.loc.lightProjection, sunMatrices[i]);
				batch.first->VAO.Bind();
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
				glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, statics.size(), sizeof(DrawCommand));
			}
		}

		// 2. Копируем свежую статику в финальную карту
		glCopyImageSubData(
			shadowshader.staticSunShadowArray, GL_TEXTURE_2D_ARRAY, 0, 0, 0, i,
			shadowshader.sunShadowArray, GL_TEXTURE_2D_ARRAY, 0, 0, 0, i,
			shadowshader.sunShadowSize, shadowshader.sunShadowSize, 1
		);

		// 3. Дорисовываем динамику (персонажей)
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowshader.sunShadowArray, 0, i);
		for (auto& batch : shadowBatches) {
			std::vector<GameObject*> dynamics;
			for (auto* o : batch.second) if (!o->isStatic) dynamics.push_back(o);
			if (!dynamics.empty()) {
				DispatchGPUCulling(batch.first, dynamics, sunMatrices[i], camera.Position, true, cullingshader, window, 0);
				shadowshader.Activate();
				shadowshader.Set(shadowshader.loc.lightProjection, sunMatrices[i]);
				batch.first->VAO.Bind();
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
				glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, dynamics.size(), sizeof(DrawCommand));
			}
		}
	}
	// В конце функции убираем shadowshader.staticShadowsDirty = false; 
	// так как мы теперь управляем этим точечно.

	// Сбрасываем флаг только после прохода всех каскадов
	shadowshader.staticShadowsDirty = false;

	glDisable(GL_DEPTH_CLAMP);
	return sunMatrices;
}
void Render::RenderAtlasShadows(Camera& camera, std::vector<GameObject>& Objects, std::map<Mesh*, std::vector<GameObject*>>& shadowBatches, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window) {
	glBindFramebuffer(GL_FRAMEBUFFER, shadowshader.atlasFBO);
	glEnable(GL_SCISSOR_TEST);
	int currentSlot = 0;
	int gridCount = shadowshader.atlasResolution / shadowshader.tileSize;

	for (auto& obj : Objects) {
		if (!obj.light.enable || !obj.light.castShadows) continue;
		if (obj.light.type == LightType::Directional) continue; // Солнце уже нарисовали в CSM!

		if (obj.light.mobility == LightMobility::Static && obj.light.hasBakedShadows) {
			currentSlot += (obj.light.type == LightType::Point) ? 6 : 1;
			continue;
		}

		if (obj.light.type == LightType::Spot) {
			if (currentSlot >= gridCount * gridCount) break;
			int gridX = currentSlot % gridCount;
			int gridY = currentSlot / gridCount;
			glViewport(gridX * shadowshader.tileSize, gridY * shadowshader.tileSize, shadowshader.tileSize, shadowshader.tileSize);
			glScissor(gridX * shadowshader.tileSize, gridY * shadowshader.tileSize, shadowshader.tileSize, shadowshader.tileSize);
			glClear(GL_DEPTH_BUFFER_BIT);

			for (auto& batch : shadowBatches) {
				Mesh* mesh = batch.first;
				std::vector<GameObject*> filteredObjects;
				for (auto* o : batch.second) {
					if (obj.light.mobility == LightMobility::Static && !o->isStatic) continue;
					filteredObjects.push_back(o);
				}
				if (filteredObjects.empty()) continue;

				DispatchGPUCulling(mesh, filteredObjects, obj.light.lightSpaceMatrix, camera.Position, true, cullingshader, window, 0);

				shadowshader.Activate();
				shadowshader.Set(shadowshader.loc.lightProjection, obj.light.lightSpaceMatrix);
				mesh->VAO.Bind();
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
				glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, filteredObjects.size(), sizeof(DrawCommand));
			}
			if (obj.light.mobility == LightMobility::Static) obj.light.hasBakedShadows = true;
			currentSlot++;
		}
		else if (obj.light.type == LightType::Point) {
			if (currentSlot + 5 >= gridCount * gridCount) break;
			obj.light.shadowSlot = currentSlot;
			glm::vec3 pos = obj.transform.position;
			glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, obj.light.radius);
			glm::mat4 shadowTransforms[6] = {
				shadowProj * glm::lookAt(pos, pos + glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)),
				shadowProj * glm::lookAt(pos, pos + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)),
				shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)),
				shadowProj * glm::lookAt(pos, pos + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)),
				shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)),
				shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0))
			};

			for (int face = 0; face < 6; ++face) {
				int gridX = (currentSlot + face) % gridCount;
				int gridY = (currentSlot + face) / gridCount;
				glViewport(gridX * shadowshader.tileSize, gridY * shadowshader.tileSize, shadowshader.tileSize, shadowshader.tileSize);
				glScissor(gridX * shadowshader.tileSize, gridY * shadowshader.tileSize, shadowshader.tileSize, shadowshader.tileSize);
				glClear(GL_DEPTH_BUFFER_BIT);

				for (auto& batch : shadowBatches) {
					Mesh* mesh = batch.first;
					std::vector<GameObject*> filteredObjects;
					for (auto* o : batch.second) {
						if (obj.light.mobility == LightMobility::Static && !o->isStatic) continue;
						filteredObjects.push_back(o);
					}
					if (filteredObjects.empty()) continue;

					DispatchGPUCulling(mesh, filteredObjects, shadowTransforms[face], camera.Position, true, cullingshader, window, 0);
					shadowshader.Activate();
					shadowshader.Set(shadowshader.loc.lightProjection, shadowTransforms[face]);
					mesh->VAO.Bind();
					glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
					glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, filteredObjects.size(), sizeof(DrawCommand));
				}
			}
			if (obj.light.mobility == LightMobility::Static) obj.light.hasBakedShadows = true;
			currentSlot += 6;
		}
	}
	glDisable(GL_SCISSOR_TEST);
}






void Render::RenderMainPass(Camera& camera, Window& window, std::map<std::pair<Mesh*, Material*>, std::vector<GameObject*>>& mainBatches, std::vector<glm::mat4>& sunMatrices, LitShader& litshader, ShadowShader& shadowshader, CullingShader& cullingshader, PostProcessingShader& ppShader) {
	ppShader.Bind(window);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	// Загружаем материалы
	std::vector<MaterialGPUData> materialDataArray;
	std::map<Material*, int> materialToIndex;
	for (auto& batch : mainBatches) {
		Material* mat = batch.first.second;
		if (materialToIndex.find(mat) == materialToIndex.end()) {
			materialToIndex[mat] = materialDataArray.size();
			materialDataArray.push_back(mat->getGPUData());
		}
	}
	materialBuffer.SetData(materialDataArray.size() * sizeof(MaterialGPUData), materialDataArray.data());

	litshader.Activate();
	// Пока используем старые Set, чтобы ничего не сломать. Потом сможешь заменить на свои короткие!
	litshader.Set(litshader.loc.camPos, camera.Position);
	litshader.Set(litshader.loc.camMatrix, camera.GetViewProjectionMatrix());
	litshader.Set(litshader.loc.nearPlane, 0.1f);
	litshader.Set(litshader.loc.farPlane, 1000.0f);
	litshader.Set(litshader.loc.atlasResolution, 8192.0f);
	litshader.Set(litshader.loc.tileSize, 2048.0f);
	litshader.Set(litshader.loc.shadowAtlas, shadowshader.shadowAtlas, 12);
	litshader.Set(litshader.loc.sunShadowMap, shadowshader.sunShadowArray, 13, GL_TEXTURE_2D_ARRAY);
	litshader.Set(litshader.loc.noiseTexture, litshader.blueNoiseTexture, 0);
	litshader.Set(litshader.loc.viewMatrix, camera.GetViewMatrix());
	litshader.Set(litshader.loc.screenSize, (float)window.width, (float)window.height);
	litshader.Set(litshader.loc.sunLightSpaceMatrices, sunMatrices);
	litshader.Set(litshader.loc.cascadeSplits, shadowshader.cascadeSplits);
	litshader.Set(litshader.loc.view, camera.GetViewMatrix());
	
	for (auto& batch : mainBatches) {
		Mesh* mesh = batch.first.first;
		Material* mat = batch.first.second;

		// Экранный куллинг
		DispatchGPUCulling(mesh, batch.second, camera.GetViewProjectionMatrix(), camera.Position, false, cullingshader, window, ppShader.hiZTexture);

		litshader.Activate();
		litshader.Set(litshader.loc.camMatrix, camera.GetViewProjectionMatrix());
		litshader.Set(litshader.loc.view, camera.GetViewMatrix());
		litshader.Set(litshader.loc.materialID, materialToIndex[mat]);

		mesh->VAO.Bind();
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, batch.second.size(), sizeof(DrawCommand));
	}
}
void Render::UpdateClusterGrid(Camera& camera, Window& window, CullingShader& cullingshader) {
	cullingshader.clusterGridShader.Activate();
	cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.zNear, 0.1f);
	cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.zFar, 1000.0f);
	cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.gridDimX, 16);
	cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.gridDimY, 9);
	cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.gridDimZ, 24);
	cullingshader.clusterGridShader.Set(cullingshader.clusterGridShader.loc.projection, camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f));
	glDispatchCompute(16, 9, 24);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	std::cout << "Сетка кластеров успешно построена на GPU!" << std::endl;
}
glm::mat4 Render::CalculateCalculateCSMMatrix(float nearP, float farP, Camera& camera, float shadowSize) {
	glm::mat4 proj = camera.GetProjectionMatrix(45.0f, nearP, farP, false);
	glm::mat4 invCam = glm::inverse(proj * camera.GetViewMatrix());

	std::vector<glm::vec4> frustumCorners;
	for (int x = 0; x < 2; ++x) {
		for (int y = 0; y < 2; ++y) {
			for (int z = 0; z < 2; ++z) {
				glm::vec4 pt = invCam * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
				frustumCorners.push_back(pt / pt.w);
			}
		}
	}

	glm::vec3 center = glm::vec3(0);
	for (const auto& v : frustumCorners) center += glm::vec3(v);
	center /= 8.0f;

	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
	if (std::abs(sunDir.y) > 0.999f) up = glm::vec3(0.0f, 0.0f, 1.0f);

	glm::mat4 lightView = glm::lookAt(center - sunDir * 100.0f, center, up);

	float minX = std::numeric_limits<float>::max(); float maxX = std::numeric_limits<float>::lowest();
	float minY = std::numeric_limits<float>::max(); float maxY = std::numeric_limits<float>::lowest();

	for (const auto& v : frustumCorners) {
		glm::vec4 trf = lightView * v;
		minX = std::min(minX, trf.x); maxX = std::max(maxX, trf.x);
		minY = std::min(minY, trf.y); maxY = std::max(maxY, trf.y);
	}

	// --- МАГИЯ СТАБИЛИЗАЦИИ ---
	// 1. Считаем, сколько мировых единиц (метров) приходится на 1 пиксель текстуры тени
	float worldUnitsPerTexel = (maxX - minX) / shadowSize;

	// 2. Округляем границы до ближайшего целого "пикселя" в мировых координатах
	minX = std::floor(minX / worldUnitsPerTexel) * worldUnitsPerTexel;
	maxX = std::floor(maxX / worldUnitsPerTexel) * worldUnitsPerTexel;

	worldUnitsPerTexel = (maxY - minY) / shadowSize;
	minY = std::floor(minY / worldUnitsPerTexel) * worldUnitsPerTexel;
	maxY = std::floor(maxY / worldUnitsPerTexel) * worldUnitsPerTexel;
	// --------------------------

	return glm::ortho(minX, maxX, minY, maxY, -500.0f, 500.0f) * lightView;
}
void Render::Voxelization(glm::vec3 Position, std::vector<GameObject>& Objects, Window& window) {
	float halfSize = voxelWorldSize / 2.0f;
	float voxelStep = voxelWorldSize / voxelGridSize;
	glm::vec3 gridCenter = glm::floor(Position / voxelStep) * voxelStep;
	gridMin = gridCenter - glm::vec3(halfSize);
	gridMax = gridCenter + glm::vec3(halfSize);

	voxelShader.Activate();
	
	voxelShader.Set(voxelShader.loc.gridSize, voxelGridSize);
	voxelShader.Set(voxelShader.loc.gridMax, gridMax);
	voxelShader.Set(voxelShader.loc.gridMin, gridMin);

	// Берем солнце из первого попавшегося
	glm::vec3 tempSunDir = glm::vec3(0, -1, 0);
	for (auto& obj : Objects) {
		if (obj.light.type == LightType::Directional) {
			glm::quat qRot = glm::quat(glm::radians(obj.transform.rotation));
			tempSunDir = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));
			break;
		}
	}
	sunDir = tempSunDir;
	voxelShader.Set(voxelShader.loc.sunDir, sunDir);

	GLuint clearColor = 0;
	glClearTexImage(voxelTex.ID, 0, GL_RGBA, GL_UNSIGNED_BYTE, &clearColor);
	glBindImageTexture(0, voxelTex.ID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glViewport(0, 0, voxelGridSize, voxelGridSize);

	for (auto& obj : Objects) {
		if (!obj.hasMesh || !obj.isVisible) continue;
		glUniformMatrix4fv(glGetUniformLocation(voxelShader.ID, "model"), 1, GL_FALSE, glm::value_ptr(obj.transform.matrix));
		for (auto& subMesh : obj.renderer.subMeshes) {
			voxelShader.Set(voxelShader.loc.albedoMap, subMesh.material->albedo.ID, 0);

			subMesh.mesh->VAO.Bind();
			glDrawElements(GL_TRIANGLES, subMesh.mesh->indices.size(), GL_UNSIGNED_INT, 0);
		}
	}

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glViewport(0, 0, window.width, window.height);

	glBindTexture(GL_TEXTURE_3D, voxelTex.ID);
	voxelMipmapShader.Activate();
	int currentSize = voxelGridSize;
	int mipLevels = 1 + (int)std::floor(std::log2(voxelGridSize));
	for (int i = 0; i < mipLevels - 1; ++i) {
		int nextSize = currentSize / 2;
		if (nextSize < 1) nextSize = 1;
		glBindImageTexture(0, voxelTex.ID, i, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8);
		glBindImageTexture(1, voxelTex.ID, i + 1, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
		GLuint groups = (nextSize + 3) / 4;
		glDispatchCompute(groups, groups, groups);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		currentSize = nextSize;
	}
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}