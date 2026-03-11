#include "Render.h"
#include "UI.h" 
#include "CullingShader.h"
#include "DefferedShader.h"
void UpdateMatrices(entt::registry& registry, entt::entity entity, glm::mat4 parentMatrix) {
	auto& transformComp = registry.get<TransformComponent>(entity);
	glm::mat4 localMatrix = glm::mat4(1.0f);

	ImGuizmo::RecomposeMatrixFromComponents(
		glm::value_ptr(transformComp.transform.position),
		glm::value_ptr(transformComp.transform.rotation),
		glm::value_ptr(transformComp.transform.scale),
		glm::value_ptr(localMatrix)
	);

	transformComp.transform.matrix = parentMatrix * localMatrix;

	// Если у сущности есть дети, обновляем их тоже
	if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
		for (entt::entity child : hierarchy->children) {
			UpdateMatrices(registry, child, transformComp.transform.matrix);
		}
	}
}
Render::Render() {
	materialBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(MaterialGPUData), nullptr, GL_DYNAMIC_DRAW, 2);
	clusterBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(ClusterAABB), nullptr, GL_STATIC_COPY, 3);
	lightGridBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(LightGrid), nullptr, GL_DYNAMIC_DRAW, 4);
	globalLightIndexBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * 100 * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW, 5);
	atomicCounterBuffer = GLBuffer(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW, 6);

	uboLights = GLBuffer(GL_UNIFORM_BUFFER, sizeof(LightUBOBlock), NULL, GL_DYNAMIC_DRAW, 0);

};
void Render::InitGBuffer(int width, int height) {
	glGenFramebuffers(1, &gBufferFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);

	// 1. Позиция (RGB) + Metallic (A) - 16-битный float для точности координат
	glGenTextures(1, &gPositionMetallic);
	glBindTexture(GL_TEXTURE_2D, gPositionMetallic);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPositionMetallic, 0);

	// 2. Нормаль (RGB) + Roughness (A) - 16-битный float для точных векторов
	glGenTextures(1, &gNormalRoughness);
	glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormalRoughness, 0);

	// 3. Цвет (RGB) + AO (A) - Обычный 8-битный формат, тут float не нужен
	glGenTextures(1, &gAlbedoAO);
	glBindTexture(GL_TEXTURE_2D, gAlbedoAO);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoAO, 0);

	// Указываем OpenGL, что рисуем в 3 текстуры одновременно
	GLuint attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
	glDrawBuffers(3, attachments);

	// Буфер глубины (Z-Buffer)
	glGenRenderbuffers(1, &rboDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "G-Buffer FBO Error!" << std::endl;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void Render::Draw(entt::registry& registry, LitShader& geometryShader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime, UI& ui, CullingShader& cullingshader, DefferedShader& defferedShader) {
	glEnable(GL_CULL_FACE);

	UpdateTransforms(registry);

	GLuint zero = 0;
	atomicCounterBuffer.UpdateData(0, sizeof(GLuint), &zero);
	sunDir = SetupLightsAndUBO(registry, shadowshader);

	cullingshader.lightCullingShader.Activate();
	glUniformMatrix4fv(glGetUniformLocation(cullingshader.lightCullingShader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(camera.GetViewMatrix()));

	// Быстрый подсчет источников света (Entt делает это мгновенно)
	auto lightView = registry.view<LightComponent>();
	int lightCount = 0;
	for (auto entity : lightView) {
		if (lightView.get<LightComponent>(entity).light.enable) lightCount++;
	}

	glUniform1ui(glGetUniformLocation(cullingshader.lightCullingShader.ID, "totalLights"), lightCount);
	glDispatchCompute(1, 1, 24);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// --- ФОРМИРУЕМ БАТЧИ ---
	std::map<Mesh*, std::vector<entt::entity>> shadowBatches;
	std::map<std::pair<Mesh*, Material*>, std::vector<entt::entity>> mainBatches;

	// Берем только сущности с Мешем и Координатами
	auto renderView = registry.view<MeshComponent, TransformComponent>();

	for (auto entity : renderView) {
		auto& meshComp = renderView.get<MeshComponent>(entity);

		if (meshComp.isVisible) {
			for (auto& subMesh : meshComp.renderer.subMeshes) {
				if (meshComp.castShadow) {
					shadowBatches[subMesh.mesh].push_back(entity);
				}
				mainBatches[{subMesh.mesh, subMesh.material}].push_back(entity);
			}
		}
	}

	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	std::vector<glm::mat4> sunMatrices = RenderCSM(camera, shadowBatches, registry, shadowshader, cullingshader, window);
	RenderAtlasShadows(camera, registry, shadowBatches, shadowshader, cullingshader, window);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glViewport(0, 0, window.width, window.height);

	RenderMainPass(camera, window, mainBatches, registry, sunMatrices, geometryShader, defferedShader, shadowshader, cullingshader, postprocessingshader);

	glDisable(GL_CULL_FACE);
	sky.Draw(camera, sunDir);
	postprocessingshader.Update(window, crntTime, camera, ui, sunDir, uboLights.ID, shadowshader);
}
void Render::UpdateTransforms(entt::registry& registry) {
	// Ищем все объекты, у которых ЕСТЬ Transform, но НЕТ HierarchyComponent (или он не является чьим-то ребенком)
	// Для простоты пока просто переберем все Transform, у которых parent == entt::null
	auto view = registry.view<TransformComponent>();

	for (auto entity : view) {
		bool isRoot = true;
		if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
			if (hierarchy->parent != entt::null) {
				isRoot = false; // Это ребенок, его обновит родитель
			}
		}

		if (isRoot) {
			UpdateMatrices(registry, entity, glm::mat4(1.0f));
		}
	}
}
glm::vec3 Render::SetupLightsAndUBO(entt::registry& registry, ShadowShader& shadowshader) {
	LightUBOBlock lightBlock;
	lightBlock.activeLightsCount = 0;

	static glm::vec3 lastSunDir = glm::vec3(0.0f);
	glm::vec3 currentSunDir = glm::vec3(0.0f, -1.0f, 0.0f);

	// МАГИЯ ENTT: Получаем только те сущности, у которых есть И Свет, И Координаты!
	auto view = registry.view<LightComponent, TransformComponent>();

	for (auto entity : view) {
		auto& lightComp = view.get<LightComponent>(entity);
		auto& transformComp = view.get<TransformComponent>(entity);

		Light& l = lightComp.light;
		Transform& t = transformComp.transform;

		if (!l.enable || l.type == LightType::None) continue;

		if (l.type == LightType::Directional) {
			glm::quat qRot = glm::quat(glm::radians(t.rotation));
			currentSunDir = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));

			if (glm::distance(currentSunDir, lastSunDir) > 0.0001f) {
				shadowshader.staticShadowsDirty = true;
				lastSunDir = currentSunDir;
			}
		}

		int i = lightBlock.activeLightsCount;
		float lightType = (float)l.type;

		lightBlock.lights[i].posType = glm::vec4(t.position, lightType);
		lightBlock.lights[i].colorInt = glm::vec4(l.color, l.intensity);

		glm::quat qRot = glm::quat(glm::radians(t.rotation));
		glm::vec3 direction = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));
		lightBlock.lights[i].dirRadius = glm::vec4(direction, l.radius);

		if (l.type == LightType::Spot) {
			glm::mat4 lightProj = glm::perspective(glm::radians(l.outerCone * 2.0f), 1.0f, 0.1f, 1000.0f);
			glm::mat4 lightView = glm::lookAt(t.position, t.position + direction, glm::vec3(0.0f, 1.0f, 0.0f));
			l.lightSpaceMatrix = lightProj * lightView;
		}

		float inner = glm::cos(glm::radians(l.innerCone));
		float outer = glm::cos(glm::radians(l.outerCone));
		float castsShadows = l.castShadows ? 1.0f : 0.0f;
		float shadowSlot = (float)l.shadowSlot;

		lightBlock.lights[i].shadowParams = glm::vec4(inner, outer, castsShadows, shadowSlot);
		lightBlock.lights[i].lightSpaceMatrix = l.lightSpaceMatrix;

		lightBlock.activeLightsCount++;
		if (lightBlock.activeLightsCount >= 100) break;
	}

	uboLights.UpdateData(0, sizeof(LightUBOBlock), &lightBlock);
	return currentSunDir;
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
}
void Render::DispatchGPUCulling(Mesh* mesh, std::vector<entt::entity>& batchObjects, entt::registry& registry, const glm::mat4& viewProj, const glm::vec3& camPos, bool isShadowPass, CullingShader& cullingshader, Window& window, GLuint hiZTex) {
	if (batchObjects.empty()) return;

	std::vector<glm::mat4> matrices;
	std::vector<BoundingSphere> spheres;
	std::vector<DrawCommand> commands;

	for (int i = 0; i < batchObjects.size(); ++i) {
		// Достаем Transform прямо из реестра по ID сущности
		entt::entity entity = batchObjects[i];
		auto& transform = registry.get<TransformComponent>(entity).transform;

		matrices.push_back(transform.matrix);
		float maxScale = std::max({ transform.scale.x, transform.scale.y, transform.scale.z });
		spheres.push_back({ glm::vec4(transform.position, mesh->boundingRadius * maxScale) });

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
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cullingshader.ssboObjects.ID);

	cullingshader.ssboCommands.SetData(commands.size() * sizeof(DrawCommand), commands.data());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cullingshader.ssboCommands.ID);

	cullingshader.Activate();
	cullingshader.Set(cullingshader.loc.camPos, camPos);
	cullingshader.Set(cullingshader.loc.isShadowPass, isShadowPass ? 1 : 0);

	float lodDist[2] = { 50.0f, 1000.0f };
	cullingshader.Set(cullingshader.loc.lodDistances, 2, lodDist);

	for (int i = 0; i < 3; i++) {
		unsigned int count = (i < mesh->lods.size()) ? mesh->lods[i].count : mesh->lods[0].count;
		unsigned int offset = (i < mesh->lods.size()) ? mesh->lods[i].firstIndex : mesh->lods[0].firstIndex;

		cullingshader.Set(cullingshader.loc.lodCount[i], count);
		cullingshader.Set(cullingshader.loc.lodOffset[i], offset);
	}

	auto planes = cullingshader.ExtractFrustumPlanes(viewProj);
	for (int i = 0; i < 6; i++) {
		cullingshader.Set(cullingshader.loc.frustumPlanes[i], planes[i].normal.x, planes[i].normal.y, planes[i].normal.z, planes[i].distance);
	}
	cullingshader.Set(cullingshader.loc.objectCount, (unsigned int)batchObjects.size());
	cullingshader.Set(cullingshader.loc.viewProjection, viewProj);

	if (!isShadowPass) {
		cullingshader.Set(cullingshader.loc.screenSize, (float)window.width, (float)window.height);
		cullingshader.Set(cullingshader.loc.hiZTexture, hiZTex, 0);
	}

	GLuint workGroups = (batchObjects.size() + 255) / 256;
	glDispatchCompute(workGroups, 1, 1);
	glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
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
std::vector<glm::mat4> Render::RenderCSM(Camera& camera, std::map<Mesh*, std::vector<entt::entity>>& shadowBatches, entt::registry& registry, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window) {
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
		bool matrixUpdated = (i == 0) ||
			(i == 1 && frameCounter % 2 == 0) ||
			(i == 2 && frameCounter % 3 == 0) ||
			(i == 3 && frameCounter % 4 == 0);

		if (!matrixUpdated) continue;

		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowshader.staticSunShadowArray, 0, i);
		glClear(GL_DEPTH_BUFFER_BIT);

		for (auto& batch : shadowBatches) {
			std::vector<entt::entity> statics;
			for (auto e : batch.second) {
				if (registry.get<MeshComponent>(e).isStatic) statics.push_back(e);
			}
			if (!statics.empty()) {
				DispatchGPUCulling(batch.first, statics, registry, sunMatrices[i], camera.Position, true, cullingshader, window, 0);
				shadowshader.Activate();
				shadowshader.Set(shadowshader.loc.lightProjection, sunMatrices[i]);
				batch.first->VAO.Bind();
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
				glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, statics.size(), sizeof(DrawCommand));
			}
		}

		glCopyImageSubData(
			shadowshader.staticSunShadowArray, GL_TEXTURE_2D_ARRAY, 0, 0, 0, i,
			shadowshader.sunShadowArray, GL_TEXTURE_2D_ARRAY, 0, 0, 0, i,
			shadowshader.sunShadowSize, shadowshader.sunShadowSize, 1
		);

		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowshader.sunShadowArray, 0, i);
		for (auto& batch : shadowBatches) {
			std::vector<entt::entity> dynamics;
			for (auto e : batch.second) {
				if (!registry.get<MeshComponent>(e).isStatic) dynamics.push_back(e);
			}
			if (!dynamics.empty()) {
				DispatchGPUCulling(batch.first, dynamics, registry, sunMatrices[i], camera.Position, true, cullingshader, window, 0);
				shadowshader.Activate();
				shadowshader.Set(shadowshader.loc.lightProjection, sunMatrices[i]);
				batch.first->VAO.Bind();
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
				glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, dynamics.size(), sizeof(DrawCommand));
			}
		}
	}

	shadowshader.staticShadowsDirty = false;
	glDisable(GL_DEPTH_CLAMP);
	return sunMatrices;
}
void Render::RenderAtlasShadows(Camera& camera, entt::registry& registry, std::map<Mesh*, std::vector<entt::entity>>& shadowBatches, ShadowShader& shadowshader, CullingShader& cullingshader, Window& window) {
	glBindFramebuffer(GL_FRAMEBUFFER, shadowshader.atlasFBO);
	glEnable(GL_SCISSOR_TEST);
	int currentSlot = 0;
	int gridCount = shadowshader.atlasResolution / shadowshader.tileSize;

	// Магия EnTT: берем только те сущности, которые реально излучают свет
	auto lightView = registry.view<LightComponent, TransformComponent>();

	for (auto entity : lightView) {
		auto& light = lightView.get<LightComponent>(entity).light;
		auto& transform = lightView.get<TransformComponent>(entity).transform;

		if (!light.enable || !light.castShadows) continue;
		if (light.type == LightType::Directional) continue;

		// Если у тебя есть enum LightMobility в структуре Light, эта проверка сработает:
		if (light.mobility == LightMobility::Static && light.hasBakedShadows) {
			currentSlot += (light.type == LightType::Point) ? 6 : 1;
			continue;
		}

		if (light.type == LightType::Spot) {
			if (currentSlot >= gridCount * gridCount) break;
			int gridX = currentSlot % gridCount;
			int gridY = currentSlot / gridCount;
			glViewport(gridX * shadowshader.tileSize, gridY * shadowshader.tileSize, shadowshader.tileSize, shadowshader.tileSize);
			glScissor(gridX * shadowshader.tileSize, gridY * shadowshader.tileSize, shadowshader.tileSize, shadowshader.tileSize);
			glClear(GL_DEPTH_BUFFER_BIT);

			for (auto& batch : shadowBatches) {
				Mesh* mesh = batch.first;
				std::vector<entt::entity> filteredObjects;
				for (auto e : batch.second) {
					bool isMeshStatic = registry.get<MeshComponent>(e).isStatic;
					if (light.mobility == LightMobility::Static && !isMeshStatic) continue;
					filteredObjects.push_back(e);
				}
				if (filteredObjects.empty()) continue;

				DispatchGPUCulling(mesh, filteredObjects, registry, light.lightSpaceMatrix, camera.Position, true, cullingshader, window, 0);

				shadowshader.Activate();
				shadowshader.Set(shadowshader.loc.lightProjection, light.lightSpaceMatrix);
				mesh->VAO.Bind();
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
				glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, filteredObjects.size(), sizeof(DrawCommand));
			}
			if (light.mobility == LightMobility::Static) light.hasBakedShadows = true;
			currentSlot++;
		}
		else if (light.type == LightType::Point) {
			if (currentSlot + 5 >= gridCount * gridCount) break;
			light.shadowSlot = currentSlot;
			glm::vec3 pos = transform.position;
			glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, light.radius);
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
					std::vector<entt::entity> filteredObjects;
					for (auto e : batch.second) {
						bool isMeshStatic = registry.get<MeshComponent>(e).isStatic;
						if (light.mobility == LightMobility::Static && !isMeshStatic) continue;
						filteredObjects.push_back(e);
					}
					if (filteredObjects.empty()) continue;

					DispatchGPUCulling(mesh, filteredObjects, registry, shadowTransforms[face], camera.Position, true, cullingshader, window, 0);
					shadowshader.Activate();
					shadowshader.Set(shadowshader.loc.lightProjection, shadowTransforms[face]);
					mesh->VAO.Bind();
					glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
					glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, filteredObjects.size(), sizeof(DrawCommand));
				}
			}
			if (light.mobility == LightMobility::Static) light.hasBakedShadows = true;
			currentSlot += 6;
		}
	}
	glDisable(GL_SCISSOR_TEST);
}


void Render::RenderMainPass(Camera& camera, Window& window, std::map<std::pair<Mesh*, Material*>, std::vector<entt::entity>>& mainBatches, entt::registry& registry, std::vector<glm::mat4>& sunMatrices, LitShader& geometryShader, DefferedShader& deferredShader, ShadowShader& shadowshader, CullingShader& cullingshader, PostProcessingShader& ppShader) {

	// ==========================================
	// ФАЗА 1: GEOMETRY PASS 
	// ==========================================
	glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

	geometryShader.Activate();
	geometryShader.Set(geometryShader.loc.camPos, camera.Position);
	geometryShader.Set(geometryShader.loc.camMatrix, camera.GetViewProjectionMatrix());
	geometryShader.Set(geometryShader.loc.view, camera.GetViewMatrix());

	for (auto& batch : mainBatches) {
		Mesh* mesh = batch.first.first;
		Material* mat = batch.first.second;

		// Передаем registry
		DispatchGPUCulling(mesh, batch.second, registry, camera.GetViewProjectionMatrix(), camera.Position, false, cullingshader, window, ppShader.hiZTexture);

		geometryShader.Activate();
		geometryShader.Set(geometryShader.loc.materialID, materialToIndex[mat]);

		mesh->VAO.Bind();
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands.ID);
		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, batch.second.size(), sizeof(DrawCommand));
	}

	// ==========================================
	// ФАЗА 1.5: КОПИРОВАНИЕ ГЛУБИНЫ
	// ==========================================
	glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ppShader.FBO);
	glBlitFramebuffer(0, 0, window.width, window.height, 0, 0, window.width, window.height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	// ==========================================
	// ФАЗА 2: LIGHTING PASS
	// ==========================================
	ppShader.Bind(window);
	glClear(GL_COLOR_BUFFER_BIT);

	deferredShader.Activate();
	deferredShader.Set(deferredShader.loc.camPos, camera.Position);
	deferredShader.Set(deferredShader.loc.view, camera.GetViewMatrix());
	deferredShader.Set(deferredShader.loc.screenSize, (float)window.width, (float)window.height);

	deferredShader.Set(deferredShader.loc.sunLightSpaceMatrices, sunMatrices);
	deferredShader.Set(deferredShader.loc.cascadeSplits, shadowshader.cascadeSplits);
	deferredShader.Set(deferredShader.loc.shadowAtlas, shadowshader.shadowAtlas, 12);
	deferredShader.Set(deferredShader.loc.sunShadowMap, shadowshader.sunShadowArray, 13, GL_TEXTURE_2D_ARRAY);
	deferredShader.Set(deferredShader.loc.noiseTexture, deferredShader.blueNoiseTexture, 0);

	glActiveTexture(GL_TEXTURE14);
	glBindTexture(GL_TEXTURE_2D, gPositionMetallic);
	deferredShader.Set(deferredShader.loc.gPositionMetallic, 14);

	glActiveTexture(GL_TEXTURE15);
	glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
	deferredShader.Set(deferredShader.loc.gNormalRoughness, 15);

	glActiveTexture(GL_TEXTURE16);
	glBindTexture(GL_TEXTURE_2D, gAlbedoAO);
	deferredShader.Set(deferredShader.loc.gAlbedoAO, 16);

	glDrawArrays(GL_TRIANGLES, 0, 3);
}