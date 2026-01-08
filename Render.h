#ifndef RENDER_CLASS_H
#define RENDER_CLASS_H
#include "Mesh.h"
#include "LitShader.h"
#include "ShadowShader.h"
#include "GameObject.h"
#include "PostProcessingShader.h"
#include "Window.h"
#include <unordered_map>
class Render {
public:
	Render() {}
public:
	bool lit = true;
	std::vector<std::vector<std::vector<GameObject*>>> grouped;

	void sort(std::vector<GameObject>& Objects) {
		int maxMatId = 0;
		int maxMeshId = 0;

		for (const auto& obj : Objects) {
			if (obj.light.enable) continue;
			if (obj.material->ID > maxMatId) maxMatId = obj.material->ID;
			if (obj.mesh->ID > maxMeshId) maxMeshId = obj.mesh->ID;
		}

		grouped = std::vector<std::vector<std::vector<GameObject*>>>(maxMatId + 1,
			std::vector<std::vector<GameObject*>>(maxMeshId + 1));

		for (auto& obj : Objects) {
			if (obj.light.enable) continue;
			grouped[obj.material->ID][obj.mesh->ID].push_back(&obj);
		}
	}
	std::vector<glm::mat4> modelMatrices;
	bool lightupdate = false;
	bool shadow = false;
	void Draw(std::vector <GameObject>& Objects, LitShader& litshader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera,double crntTime){
		
		if (!lightupdate) {
			for (size_t matId = 0; matId < grouped.size(); ++matId) {
				bool hasObjects = false;

				for (size_t meshId = 0; meshId < grouped[matId].size(); ++meshId) {
					if (!grouped[matId][meshId].empty()) {
						hasObjects = true;
						break;
					}
				}
				if (!hasObjects) continue;

				for (size_t meshId = 0; meshId < grouped[matId].size(); ++meshId) {
					if (grouped[matId][meshId].empty()) continue;

					GameObject* first = grouped[matId][meshId][0];
					first->mesh->VAO.Bind();

					modelMatrices = std::vector<glm::mat4>();
					modelMatrices.reserve(grouped[matId][meshId].size());
					for (auto& obj : grouped[matId][meshId]) {
						if (obj->transform.updatematrix) {
							ImGuizmo::RecomposeMatrixFromComponents(
								glm::value_ptr(obj->transform.position),
								glm::value_ptr(obj->transform.rotation),
								glm::value_ptr(obj->transform.scale),
								glm::value_ptr(obj->transform.matrix)
							);
							obj->transform.updatematrix = false;
						}
						modelMatrices.push_back(obj->transform.matrix);
					}

					// обновляем instance VBO матрицами
					glBindBuffer(GL_ARRAY_BUFFER, first->mesh->VBOS);
					glBufferSubData(GL_ARRAY_BUFFER, 0, modelMatrices.size() * sizeof(glm::mat4), modelMatrices.data());

				}
			}
			lightupdate = true;
		}

		shadowshader.Update(Objects[1]);
		if (shadow) {
			shadowshader.shaderCube.Activate();
			for (size_t matId = 0; matId < grouped.size(); ++matId) {
				for (size_t meshId = 0; meshId < grouped[matId].size(); ++meshId) {
					GameObject* first = grouped[matId][meshId][0];
					first->mesh->VAO.Bind();
					size_t indices = first->mesh->indices.size();

					glDrawElementsInstanced(GL_TRIANGLES, indices, GL_UNSIGNED_INT, 0, modelMatrices.size());
				}
			}
		}


		postprocessingshader.Bind(window);
		litshader.Update(Objects[1], camera, shadowshader.depthCubemap);

		for (size_t matId = 0; matId < grouped.size(); ++matId) {
			grouped[matId][0][0]->material->Activate(litshader.shader);
			for (size_t meshId = 0; meshId < grouped[matId].size(); ++meshId) {
				GameObject* first = grouped[matId][meshId][0];
				first->mesh->VAO.Bind();
				size_t indices = first->mesh->indices.size();	

				glDrawElementsInstanced(GL_TRIANGLES, indices, GL_UNSIGNED_INT, 0, grouped[matId][meshId].size());
			}
		}


		postprocessingshader.Update(window, crntTime);
	}

};
#endif