#include "Render.h"
#include "UI.h" // <--- ПОДКЛЮЧАЕМ ТОЛЬКО В .CPP ФАЙЛЕ!
#include "CullingShader.h"
#include <cstring> // Обязательно добавляем для работы std::memcpy

void Render::Draw(std::vector<GameObject>& Objects, LitShader& litshader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime,
    std::vector<glm::mat4>& boneTransforms, UI& ui, unsigned int uboLights) {

    glEnable(GL_CULL_FACE);

    // 1. Пересчитываем локальные матрицы ВСЕХ объектов
    for (auto& obj : Objects) {
        if (obj.transform.updatematrix) {
            ImGuizmo::RecomposeMatrixFromComponents(
                glm::value_ptr(obj.transform.position),
                glm::value_ptr(obj.transform.rotation),
                glm::value_ptr(obj.transform.scale),
                glm::value_ptr(obj.transform.matrix)
            );
            obj.transform.updatematrix = false;
        }
    }

    // =======================================================
    // АВТОМАТИЧЕСКАЯ ГРУППИРОВКА (BATCHING)
    // =======================================================
    // Корзинка для теней (группируем только по Мешу)
    std::map<Mesh*, std::vector<glm::mat4>> shadowBatches;
    std::map<std::pair<Mesh*, Material*>, std::vector<glm::mat4>> mainBatches;

    for (const auto& obj : Objects) {
        if (!obj.isVisible) continue;
        for (const auto& subMesh : obj.renderer.subMeshes) {
            if (obj.castShadow) shadowBatches[subMesh.mesh].push_back(obj.transform.matrix);
            mainBatches[{subMesh.mesh, subMesh.material}].push_back(obj.transform.matrix);
        }
    }
    // =======================================================

    // --- 2. ОТРИСОВКА ТЕНЕЙ (Shadow Pass) ---
    glCullFace(GL_FRONT);
    shadowshader.atlasShader.Activate();
    glBindFramebuffer(GL_FRAMEBUFFER, shadowshader.atlasFBO);
    glClear(GL_DEPTH_BUFFER_BIT);

    int currentSlot = 0;
    int gridCount = shadowshader.atlasResolution / shadowshader.tileSize;

    // В тенях мы просто копируем матрицы в начало, так как группируем только по Мешу
    for (auto& obj : Objects) {
        if (obj.light.enable && obj.light.castShadows) {

            // --- СОЛНЦЕ И ФОНАРИК (1 квадратик) ---
            if (obj.light.type == LightType::Directional || obj.light.type == LightType::Spot) {
                if (currentSlot >= gridCount * gridCount) break;

                int gridX = currentSlot % gridCount;
                int gridY = currentSlot / gridCount;
                int tileSize = shadowshader.tileSize;

                glViewport(gridX * tileSize, gridY * tileSize, tileSize, tileSize);

                glm::mat4 rotMat(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.z), glm::vec3(0, 0, 1));
                rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.y), glm::vec3(0, 1, 0));
                rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.x), glm::vec3(1, 0, 0));
                glm::vec3 direction = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));

                glm::mat4 lightProj, lightView;
                if (obj.light.type == LightType::Directional) {
                    float size = 35.0f;
                    lightProj = glm::ortho(-size, size, -size, size, 0.1f, 150.0f);
                    glm::vec3 sunPos = camera.Position - direction * 50.0f;
                    lightView = glm::lookAt(sunPos, camera.Position, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                else {
                    lightProj = glm::perspective(glm::radians(obj.light.outerCone * 2.0f), 1.0f, 0.1f, 100.0f);
                    lightView = glm::lookAt(obj.transform.position, obj.transform.position + direction, glm::vec3(0.0f, 1.0f, 0.0f));
                }

                obj.light.lightSpaceMatrix = lightProj * lightView;
                obj.light.shadowSlot = currentSlot;
                glUniformMatrix4fv(glGetUniformLocation(shadowshader.atlasShader.ID, "lightProjection"), 1, GL_FALSE, glm::value_ptr(obj.light.lightSpaceMatrix));

                for (auto& batch : shadowBatches) {
                    Mesh* mesh = batch.first;
                    auto& matrices = batch.second;

                    // ИСПОЛЬЗУЕМ ПРЯМОЕ КОПИРОВАНИЕ
                    std::memcpy(mesh->mappedInstanceVBO, matrices.data(), matrices.size() * sizeof(glm::mat4));

                    mesh->VAO.Bind();
                    glDrawElementsInstanced(GL_TRIANGLES, mesh->indices.size(), GL_UNSIGNED_INT, 0, matrices.size());
                }
                currentSlot++;
            }

            // --- ЛАМПОЧКА POINT (6 квадратиков!) ---
            else if (obj.light.type == LightType::Point) {
                if (currentSlot + 5 >= gridCount * gridCount) break; // Защита места

                obj.light.shadowSlot = currentSlot;
                glm::vec3 pos = obj.transform.position;

                // FOV ровно 90 градусов, чтобы 6 картинок сомкнулись в идеальный куб
                glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, obj.light.radius);

                // 6 направлений взгляда
                glm::mat4 shadowTransforms[6] = {
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)),
                    shadowProj * glm::lookAt(pos, pos + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0))
                };

                int tileSize = shadowshader.tileSize;

                for (int face = 0; face < 6; ++face) {
                    int gridX = (currentSlot + face) % gridCount;
                    int gridY = (currentSlot + face) / gridCount;

                    glViewport(gridX * tileSize, gridY * tileSize, tileSize, tileSize);
                    glUniformMatrix4fv(glGetUniformLocation(shadowshader.atlasShader.ID, "lightProjection"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[face]));

                    for (auto& batch : shadowBatches) {
                        Mesh* mesh = batch.first;
                        auto& matrices = batch.second;

                        // ИСПОЛЬЗУЕМ ПРЯМОЕ КОПИРОВАНИЕ ТУТ ТОЖЕ
                        std::memcpy(mesh->mappedInstanceVBO, matrices.data(), matrices.size() * sizeof(glm::mat4));

                        mesh->VAO.Bind();
                        glDrawElementsInstanced(GL_TRIANGLES, mesh->indices.size(), GL_UNSIGNED_INT, 0, matrices.size());
                    }
                }
                currentSlot += 6; // Заняли 6 мест!
            }
        }
    }

    // Возвращаем вьюпорт для основной отрисовки экрана
    glViewport(0, 0, window.width, window.height);
    glCullFace(GL_BACK);

    glm::vec3 sunDir = glm::vec3(0.0f, -1.0f, 0.0f);

    for (auto& obj : Objects) {
        if (obj.light.type == LightType::Directional) {
            // Используем встроенную функцию GLM! Никаких угадываний.
            glm::vec3 rotRadians = glm::radians(obj.transform.rotation);
            glm::quat qRot = glm::quat(rotRadians);

            // Крутим наш вектор
            sunDir = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));
            break;
        }
    }
    // 1. Собираем все уникальные материалы со сцены
    std::vector<MaterialGPUData> gpuMaterials;
    // Допустим, мы пробегаемся по твоим объектам и собираем их (я покажу простой пример)
    // Важно, чтобы индекс материала в этом векторе совпадал с material->ID!

    // Пример заполнения (тебе нужно будет адаптировать под свою логику):
    for (auto& batch : mainBatches) {
        Material* mat = batch.first.second;

        // Убедимся, что вектор достаточно большой
        if (mat->ID >= gpuMaterials.size()) {
            gpuMaterials.resize(mat->ID + 1);
        }
        // Записываем данные материала на его законное место
        gpuMaterials[mat->ID] = mat->getGPUData();
    }

    // 2. Отправляем весь массив на видеокарту одним махом!
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, gpuMaterials.size() * sizeof(MaterialGPUData), gpuMaterials.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    // --- 3. ОСНОВНАЯ ОТРИСОВКА (ЦВЕТ И СВЕТ) ---
    postprocessingshader.Bind(window);

    litshader.Update(Objects, camera, shadowshader.shadowAtlas, boneTransforms, uboLights);



    // Рисуем из корзинок цвета!
    for (auto& batch : mainBatches) {
        Mesh* mesh = batch.first.first;       // Достаем Меш
        Material* mat = batch.first.second;   // Достаем Материал
        auto& matrices = batch.second;        // Достаем список позиций (матриц)

        // ИСПОЛЬЗУЕМ ПРЯМОЕ КОПИРОВАНИЕ ДЛЯ ГЛАВНОЙ ОТРИСОВКИ
        std::memcpy(mesh->mappedInstanceVBO, matrices.data(), matrices.size() * sizeof(glm::mat4));

        mat->Activate(litshader.shader);
        mesh->VAO.Bind();
        glDrawElementsInstanced(GL_TRIANGLES, mesh->indices.size(), GL_UNSIGNED_INT, 0, matrices.size());
    }

    glDisable(GL_CULL_FACE);

    // Тут добавь код, чтобы вытащить реальный direction твоей лампочки "Солнце"
    // (мы это делали через кватернионы в LitShader)

    sky.Draw(camera, sunDir);

    // 4. ПОСТ-ОБРАБОТКА (И НАШ SSGI)
    postprocessingshader.Update(window, crntTime, camera, ui);
}