#include "Render.h"
#include "UI.h" // <--- ПОДКЛЮЧАЕМ ТОЛЬКО В .CPP ФАЙЛЕ!
#include "CullingShader.h"
static GLuint materialSSBO = 1;

Render::Render() {
    glGenBuffers(1, &materialSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(MaterialGPUData), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    // 1. Создаем сетку (16x9x24 кластера)
    glGenBuffers(1, &clusterSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(ClusterAABB), nullptr, GL_STATIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, clusterSSBO);
    // 2. Создаем картотеку (смещение и количество)
    glGenBuffers(1, &lightGridSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightGridSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * sizeof(LightGrid), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, lightGridSSBO);
    // 3. Создаем глобальный список индексов (максимум по 100 ламп на каждый кластер)
    glGenBuffers(1, &globalLightIndexListSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, globalLightIndexListSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 16 * 9 * 24 * 100 * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, globalLightIndexListSSBO);
    // 4. Создаем атомный счетчик (всего 4 байта для одного числа)
    glGenBuffers(1, &atomicCounterSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, atomicCounterSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, atomicCounterSSBO);
};
void Render::UpdateClusterGrid(Camera& camera, Window& window, CullingShader& cullingshader) {
    cullingshader.clusterGridShader.Activate();
    // 1. Передаем параметры камеры
    // Используй те же значения, что и в основном рендере!
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    glUniform1f(glGetUniformLocation(cullingshader.clusterGridShader.ID, "zNear"), nearPlane);
    glUniform1f(glGetUniformLocation(cullingshader.clusterGridShader.ID, "zFar"), farPlane);
    // 2. Передаем размеры нашей сетки
    glUniform1ui(glGetUniformLocation(cullingshader.clusterGridShader.ID, "gridDimX"), 16);
    glUniform1ui(glGetUniformLocation(cullingshader.clusterGridShader.ID, "gridDimY"), 9);
    glUniform1ui(glGetUniformLocation(cullingshader.clusterGridShader.ID, "gridDimZ"), 24);
    // 3. Передаем матрицу проекции
    glm::mat4 projection = camera.GetProjectionMatrix(45.0f, nearPlane, farPlane);
    glUniformMatrix4fv(glGetUniformLocation(cullingshader.clusterGridShader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    // 4. Запускаем расчет (по одному потоку на каждый кубик)
    // Мы передаем 16, 9, 24 как группы, потому что в шейдере прописали local_size = 1
    glDispatchCompute(16, 9, 24);
    // 5. Ждем завершения, чтобы сетка точно записалась в SSBO
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    std::cout << "Сетка кластеров успешно построена на GPU!" << std::endl;
}
void Render::Draw(std::vector<GameObject>& Objects, LitShader& litshader, ShadowShader& shadowshader, PostProcessingShader& postprocessingshader, Window& window, Camera& camera, double crntTime,
    std::vector<glm::mat4>& boneTransforms, UI& ui, unsigned int uboLights, CullingShader& cullingshader) {
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
    // --- ПОДГОТОВКА КЛАСТЕРНОГО СВЕТА ---
// 1. Обнуляем счетчик (очень важно! Без этого список ламп будет расти бесконечно до краша)
    GLuint zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, atomicCounterSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &zero);
    // 2. Считаем, сколько у нас всего ламп (нужно собрать их из всех GameObject)
    std::vector<GameObject*> lightsOnly;
    for (auto& obj : Objects) {
        if (obj.light.enable) lightsOnly.push_back(&obj);
    }
    // 3. Запускаем Куллинг Света
    cullingshader.lightCullingShader.Activate();
    // Передаем матрицу вида (View), чтобы лампы считались относительно камеры
    glUniformMatrix4fv(glGetUniformLocation(cullingshader.lightCullingShader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(camera.GetLookAtMatrix()));
    // Передаем общее количество ламп на сцене
    glUniform1ui(glGetUniformLocation(cullingshader.lightCullingShader.ID, "totalLights"), (GLuint)lightsOnly.size());
    // Запускаем расчет (16x9 мы прописали внутри шейдера, поэтому тут только 24 слоя вглубь)
    glDispatchCompute(1, 1, 24);
    // Ждем, пока GPU закончит раскидывать лампы по кубикам
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    // --- ДАЛЬШЕ ИДЕТ ТВОЯ ОБЫЧНАЯ ОТРИСОВКА ---
    // 
    // =======================================================
    // Корзинка для теней (группируем только по Мешу)
    std::map<Mesh*, std::vector<GameObject*>> shadowBatches;
    // ИЗМЕНЕНИЕ: Корзинка для цвета теперь хранит УКАЗАТЕЛИ НА ОБЪЕКТЫ, 
    // чтобы мы могли взять их радиус и позицию для Culling Shader!
    std::map<std::pair<Mesh*, Material*>, std::vector<GameObject*>> mainBatches;
    // Пробегаемся по всем объектам и раскладываем по корзинкам
    for (auto& obj : Objects) {
        for (auto& subMesh : obj.renderer.subMeshes) {
            if (obj.castShadow && obj.isVisible) shadowBatches[subMesh.mesh].push_back(&obj);
            if (obj.isVisible) mainBatches[{subMesh.mesh, subMesh.material}].push_back(&obj);
        }
    }
    // =======================================================
    // --- 2. ОТРИСОВКА ТЕНЕЙ ------
    glDisable(GL_CULL_FACE);
    shadowshader.atlasShader.Activate();
    glBindFramebuffer(GL_FRAMEBUFFER, shadowshader.atlasFBO);
    // УДАЛЯЕМ ГЛОБАЛЬНЫЙ CLEAR! Теперь мы не стираем весь атлас разом.
    // glClear(GL_DEPTH_BUFFER_BIT); 
    // Включаем тест ножниц: OpenGL сможет очищать и рисовать только в заданной зоне
    glEnable(GL_SCISSOR_TEST);
    int currentSlot = 0;
    int gridCount = shadowshader.atlasResolution / shadowshader.tileSize;
    for (auto& obj : Objects) {
        if (obj.light.enable && obj.light.castShadows) {
            // === МАГИЯ СТАТИЧНОГО СВЕТА ===
            if (obj.light.mobility == LightMobility::Static && obj.light.hasBakedShadows) {
                // Свет уже нарисовал тени в прошлых кадрах. 
                // Просто резервируем за ним его слоты в Атласе и идем дальше!
                if (obj.light.type == LightType::Point) currentSlot += 6;
                else currentSlot += 1;
                continue;
            }
            // --- СОЛНЦЕ И ФОНАРИК (1 квадратик) ---
            if (obj.light.type == LightType::Directional || obj.light.type == LightType::Spot) {
                if (currentSlot >= gridCount * gridCount) break;
                int gridX = currentSlot % gridCount;
                int gridY = currentSlot / gridCount;
                int tileSize = shadowshader.tileSize;
                glViewport(gridX * tileSize, gridY * tileSize, tileSize, tileSize);
                // НОВОЕ: Накладываем ножницы на этот квадратик и стираем только его!
                glScissor(gridX * tileSize, gridY * tileSize, tileSize, tileSize);
                glClear(GL_DEPTH_BUFFER_BIT);
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
                // GPU FRUSTUM CULLING ДЛЯ ТЕНЕЙ
                for (auto& batch : shadowBatches) {
                    Mesh* mesh = batch.first;
                    auto& batchObjects = batch.second;
                    // --- ОТСЕВ СТАТИЧНЫХ ОБЪЕКТОВ ---
                    // Если свет статичный, рисуем ТОЛЬКО статичные объекты.
                    // (Мы не хотим запекать динамического персонажа в статический свет насовсем)
                    std::vector<GameObject*> filteredObjects;
                    for (auto* o : batchObjects) {
                        if (obj.light.mobility == LightMobility::Static && !o->isStatic) continue;
                        filteredObjects.push_back(o);
                    }
                    if (filteredObjects.empty()) continue;
                    std::vector<glm::mat4> matrices;
                    std::vector<BoundingSphere> spheres;
                    std::vector<DrawCommand> commands;
                    for (int i = 0; i < filteredObjects.size(); ++i) {
                        GameObject* o = filteredObjects[i];
                        matrices.push_back(o->transform.matrix);
                        float maxScale = std::max({ o->transform.scale.x, o->transform.scale.y, o->transform.scale.z });
                        spheres.push_back({ glm::vec4(o->transform.position, mesh->boundingRadius * maxScale) });
                        DrawCommand cmd;
                        cmd.count = mesh->indices.size();
                        cmd.instanceCount = 0;
                        cmd.firstIndex = 0;
                        cmd.baseVertex = 0;
                        cmd.baseInstance = i;
                        commands.push_back(cmd);
                    }
                    glBindBuffer(GL_ARRAY_BUFFER, mesh->VBOS);
                    glBufferData(GL_ARRAY_BUFFER, matrices.size() * sizeof(glm::mat4), matrices.data(), GL_DYNAMIC_DRAW);
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, cullingshader.ssboObjects);
                    glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(BoundingSphere), spheres.data(), GL_DYNAMIC_DRAW);
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cullingshader.ssboObjects); // <--- ДОБАВИТЬ!
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, cullingshader.ssboCommands);
                    glBufferData(GL_SHADER_STORAGE_BUFFER, commands.size() * sizeof(DrawCommand), commands.data(), GL_DYNAMIC_DRAW);
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cullingshader.ssboCommands); // <--- ДОБАВИТЬ!
                    cullingshader.shader.Activate();
                    glUniform1i(glGetUniformLocation(cullingshader.shader.ID, "isShadowPass"), 1);
                    // Передаем камеру и дистанции для расчета LOD в тенях
                    glUniform3fv(glGetUniformLocation(cullingshader.shader.ID, "camPos"), 1, glm::value_ptr(camera.Position));
                    float lodDist[2] = { 50.0f, 100.0f };
                    glUniform1fv(glGetUniformLocation(cullingshader.shader.ID, "lodDistances"), 2, lodDist);

                    // Передаем размеры текущего меша! Без этого Compute Shader не знает, сколько рисовать.
                    for (int i = 0; i < 3; i++) {
                        unsigned int count = (i < mesh->lods.size()) ? mesh->lods[i].count : mesh->lods[0].count;
                        unsigned int offset = (i < mesh->lods.size()) ? mesh->lods[i].firstIndex : mesh->lods[0].firstIndex;

                        glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, ("meshLODs[" + std::to_string(i) + "].count").c_str()), count);
                        glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, ("meshLODs[" + std::to_string(i) + "].firstIndex").c_str()), offset);
                    }
                    auto planes = cullingshader.ExtractFrustumPlanes(obj.light.lightSpaceMatrix);
                    for (int i = 0; i < 6; i++) {
                        std::string name = "frustumPlanes[" + std::to_string(i) + "]";
                        glUniform4f(glGetUniformLocation(cullingshader.shader.ID, name.c_str()), planes[i].normal.x, planes[i].normal.y, planes[i].normal.z, planes[i].distance);
                    }
                    glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, "objectCount"), filteredObjects.size());
                    GLuint workGroups = (filteredObjects.size() + 255) / 256;
                    glDispatchCompute(workGroups, 1, 1);
                    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
                    shadowshader.atlasShader.Activate();
                    glUniformMatrix4fv(glGetUniformLocation(shadowshader.atlasShader.ID, "lightProjection"), 1, GL_FALSE, glm::value_ptr(obj.light.lightSpaceMatrix));
                    mesh->VAO.Bind();
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands);
                    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, filteredObjects.size(), sizeof(DrawCommand));
                    
                }
                // Запоминаем, что статика отрисована
                if (obj.light.mobility == LightMobility::Static) obj.light.hasBakedShadows = true;
                currentSlot++;
            }
            // --- ЛАМПОЧКА POINT (6 квадратиков!) ---
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
                int tileSize = shadowshader.tileSize;
                for (int face = 0; face < 6; ++face) {
                    int gridX = (currentSlot + face) % gridCount;
                    int gridY = (currentSlot + face) / gridCount;
                    glViewport(gridX * tileSize, gridY * tileSize, tileSize, tileSize);
                    // НОВОЕ: Очищаем только грань куба
                    glScissor(gridX * tileSize, gridY * tileSize, tileSize, tileSize);
                    glClear(GL_DEPTH_BUFFER_BIT);
                    for (auto& batch : shadowBatches) {
                        Mesh* mesh = batch.first;
                        auto& batchObjects = batch.second;
                        // Фильтр статики
                        std::vector<GameObject*> filteredObjects;
                        for (auto* o : batchObjects) {
                            if (obj.light.mobility == LightMobility::Static && !o->isStatic) continue;
                            filteredObjects.push_back(o);
                        }
                        if (filteredObjects.empty()) continue;
                        std::vector<glm::mat4> matrices;
                        std::vector<BoundingSphere> spheres;
                        std::vector<DrawCommand> commands;
                        for (int i = 0; i < filteredObjects.size(); ++i) {
                            GameObject* o = filteredObjects[i];
                            matrices.push_back(o->transform.matrix);
                            float maxScale = std::max({ o->transform.scale.x, o->transform.scale.y, o->transform.scale.z });
                            spheres.push_back({ glm::vec4(o->transform.position, mesh->boundingRadius * maxScale) });
                            DrawCommand cmd = {};
                            cmd.count = mesh->indices.size();
                            cmd.instanceCount = 0;
                            cmd.firstIndex = 0;
                            cmd.baseVertex = 0;
                            cmd.baseInstance = i;
                            commands.push_back(cmd);
                        }
                        glBindBuffer(GL_ARRAY_BUFFER, mesh->VBOS);
                        glBufferData(GL_ARRAY_BUFFER, matrices.size() * sizeof(glm::mat4), matrices.data(), GL_DYNAMIC_DRAW);
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, cullingshader.ssboObjects);
                        glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(BoundingSphere), spheres.data(), GL_DYNAMIC_DRAW);
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cullingshader.ssboObjects); // <--- ВАЖНО

                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, cullingshader.ssboCommands);
                        glBufferData(GL_SHADER_STORAGE_BUFFER, commands.size() * sizeof(DrawCommand), commands.data(), GL_DYNAMIC_DRAW);
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cullingshader.ssboCommands); // <--- ВАЖНО

                        cullingshader.shader.Activate();
                        glUniform1i(glGetUniformLocation(cullingshader.shader.ID, "isShadowPass"), 1);

                        // 3. ПЕРЕДАЕМ РАЗМЕРЫ ТЕКУЩЕГО МЕША В ШЕЙДЕР ТЕНЕЙ
                        // Берем LOD 0 (оригинал), чтобы тени не дырявились из-за упрощения
                        for (int i = 0; i < 3; i++) {
                            unsigned int count = mesh->lods[0].count;
                            unsigned int offset = mesh->lods[0].firstIndex;

                            glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, ("meshLODs[" + std::to_string(i) + "].count").c_str()), count);
                            glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, ("meshLODs[" + std::to_string(i) + "].firstIndex").c_str()), offset);
                        }

                        // Передаем плоскости отсечения и запускаем куллинг...
                        auto planes = cullingshader.ExtractFrustumPlanes(shadowTransforms[face]);
                        for (int i = 0; i < 6; i++) {
                            std::string name = "frustumPlanes[" + std::to_string(i) + "]";
                            glUniform4f(glGetUniformLocation(cullingshader.shader.ID, name.c_str()), planes[i].normal.x, planes[i].normal.y, planes[i].normal.z, planes[i].distance);
                        }
                        glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, "objectCount"), filteredObjects.size());
                        glDispatchCompute((filteredObjects.size() + 255) / 256, 1, 1);
                        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
                        shadowshader.atlasShader.Activate();
                        glUniformMatrix4fv(glGetUniformLocation(shadowshader.atlasShader.ID, "lightProjection"), 1, GL_FALSE, glm::value_ptr(shadowTransforms[face]));
                        mesh->VAO.Bind();
                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands);
                        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, filteredObjects.size(), sizeof(DrawCommand));
                        
                    }
                }
                // Запоминаем статику
                if (obj.light.mobility == LightMobility::Static) obj.light.hasBakedShadows = true;
                currentSlot += 6;
            }
        }
    }
    // ВАЖНО: Обязательно выключаем ножницы, чтобы основная отрисовка экрана не обрезалась!
    glDisable(GL_SCISSOR_TEST);
    // Возвращаем вьюпорт для основной отрисовки экрана
    glViewport(0, 0, window.width, window.height);
    glCullFace(GL_BACK);
    glm::vec3 sunDir = glm::vec3(0.0f, -1.0f, 0.0f);
    for (auto& obj : Objects) {
        if (obj.light.type == LightType::Directional) {
            glm::vec3 rotRadians = glm::radians(obj.transform.rotation);
            glm::quat qRot = glm::quat(rotRadians);
            sunDir = glm::normalize(qRot * glm::vec3(0.0f, -1.0f, 0.0f));
            break;
        }
    }
    // --- 3. ОСНОВНАЯ ОТРИСОВКА (ЦВЕТ И СВЕТ) ---
    postprocessingshader.Bind(window);
    std::vector<MaterialGPUData> materialDataArray;
    std::map<Material*, int> materialToIndex;
    for (auto& batch : mainBatches) {
        Material* mat = batch.first.second;
        if (materialToIndex.find(mat) == materialToIndex.end()) {
            materialToIndex[mat] = materialDataArray.size();
            materialDataArray.push_back(mat->getGPUData());
        }
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, materialDataArray.size() * sizeof(MaterialGPUData), materialDataArray.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, materialSSBO); // Биндим на слот 2
    // Загружаем Uniforms для PBR и Света (Один раз на весь кадр)
    litshader.shader.Activate();
    unsigned int lightIndex = glGetUniformBlockIndex(litshader.shader.ID, "LightBlock");
    glUniformBlockBinding(litshader.shader.ID, lightIndex, 0);
    // 1. Создаем локальную копию нашего чемодана
    LightUBOBlock lightBlock;
    lightBlock.activeLightsCount = 0;
    // 2. Упаковываем АБСОЛЮТНО ВСЕ данные ламп в один пакет
    for (auto& obj : Objects) {
        if (obj.light.enable && obj.light.type != LightType::None) {
            int i = lightBlock.activeLightsCount;
            // Позиция и тип (0=Dir, 1=Point, 2=Spot, 3=Rect, 4=Sky)
            float lightType = (float)obj.light.type;
            lightBlock.lights[i].posType = glm::vec4(obj.transform.position, lightType);
            // Цвет и яркость
            lightBlock.lights[i].colorInt = glm::vec4(obj.light.color, obj.light.intensity);
            // Направление (через матрицы вращения, как у тебя было)
            glm::mat4 rotMat(1.0f);
            rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.z), glm::vec3(0, 0, 1));
            rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.y), glm::vec3(0, 1, 0));
            rotMat = glm::rotate(rotMat, glm::radians(obj.transform.rotation.x), glm::vec3(1, 0, 0));
            glm::vec3 direction = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
            // Направление и радиус
            lightBlock.lights[i].dirRadius = glm::vec4(direction, obj.light.radius);
            // --- ПАРАМЕТРЫ ТЕНЕЙ (Упаковываем в один vec4) ---
            float inner = glm::cos(glm::radians(obj.light.innerCone));
            float outer = glm::cos(glm::radians(obj.light.outerCone));
            float castsShadows = obj.light.castShadows ? 1.0f : 0.0f;
            float shadowSlot = (float)obj.light.shadowSlot;
            lightBlock.lights[i].shadowParams = glm::vec4(inner, outer, castsShadows, shadowSlot);
            // Матрица для теневого атласа
            lightBlock.lights[i].lightSpaceMatrix = obj.light.lightSpaceMatrix;
            lightBlock.activeLightsCount++;
            // Защита от переполнения (ровно 100 ламп)
            if (lightBlock.activeLightsCount >= 100) break;
        }
    }
    // 3. ОТПРАВЛЯЕМ ВЕСЬ ПАКЕТ НА ВИДЕОКАРТУ ОДНИМ БРОСКОМ!
    glBindBuffer(GL_UNIFORM_BUFFER, uboLights);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightUBOBlock), &lightBlock);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboLights);
    // 4. Обычные uniform-переменные (Камера, Атлас, Шум)
    glUniform3f(glGetUniformLocation(litshader.shader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
    camera.Matrix(litshader.shader, "camMatrix");
    glUniform1f(glGetUniformLocation(litshader.shader.ID, "farPlane"), 100.0f);
    glUniform1f(glGetUniformLocation(litshader.shader.ID, "atlasResolution"), 8192.0f);
    glUniform1f(glGetUniformLocation(litshader.shader.ID, "tileSize"), 2048.0f);
    // Привязываем текстуры
    glActiveTexture(GL_TEXTURE0 + 12);
    glBindTexture(GL_TEXTURE_2D, shadowshader.shadowAtlas);
    glUniform1i(glGetUniformLocation(litshader.shader.ID, "shadowAtlas"), 12);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, litshader.noiseTexture);
    glUniform1i(glGetUniformLocation(litshader.shader.ID, "noiseTexture"), 0);
    glUniformMatrix4fv(glGetUniformLocation(litshader.shader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(camera.GetLookAtMatrix()));
    glUniform1f(glGetUniformLocation(litshader.shader.ID, "zNear"), 0.1f);
    glUniform1f(glGetUniformLocation(litshader.shader.ID, "zFar"), 100.0f);
    glUniform2f(glGetUniformLocation(litshader.shader.ID, "screenSize"), (float)window.width, (float)window.height);
    // Привязываем буферы, чтобы фрагментный шейдер мог их прочитать



    // =======================================================
    // GPU FRUSTUM CULLING + INDIRECT DRAWING
    // =======================================================
    for (auto& batch : mainBatches) {
        Mesh* mesh = batch.first.first;
        Material* mat = batch.first.second;
        auto& batchObjects = batch.second; // Это теперь std::vector<GameObject*>
        // 3.1. Подготавливаем данные для Culling Shader
        std::vector<glm::mat4> matrices;
        std::vector<BoundingSphere> spheres;
        std::vector<DrawCommand> commands;
        for (int i = 0; i < batchObjects.size(); ++i) {
            GameObject* obj = batchObjects[i];
            matrices.push_back(obj->transform.matrix);
            // Радиус. Если у тебя есть obj.boundingRadius, напиши его вместо 10.0f
            // Выбираем самую большую ось масштабирования (вдруг объект вытянут)
            float maxScale = std::max({ obj->transform.scale.x, obj->transform.scale.y, obj->transform.scale.z });
            // Финальный радиус = родной размер меша * масштаб объекта
            float finalRadius = mesh->boundingRadius * maxScale;
            // Грузим в видеокарту!
            spheres.push_back({ glm::vec4(obj->transform.position, finalRadius) });
            DrawCommand cmd;
            cmd.count = mesh->indices.size(); // Сколько вершин рисовать
            cmd.instanceCount = 0;            // Compute Shader поставит тут 1, если объект видно
            cmd.firstIndex = 0;
            cmd.baseVertex = 0;
            cmd.baseInstance = i;             // ВАЖНО: Это указывает видеокарте, какую матрицу брать из VBO!
            commands.push_back(cmd);
        }
        // 3.2. Грузим матрицы в обычный VBO для отрисовки
        // Прямое и невероятно быстрое копирование матриц прямо в видеопамять!
        glBindBuffer(GL_ARRAY_BUFFER, mesh->VBOS); // Используй отдельный буфер для матриц!
        glBufferData(GL_ARRAY_BUFFER, matrices.size() * sizeof(glm::mat4), matrices.data(), GL_DYNAMIC_DRAW);
        
        // 3.3. Грузим Сферы и Команды в SSBO
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, cullingshader.ssboObjects);
        glBufferData(GL_SHADER_STORAGE_BUFFER, spheres.size() * sizeof(BoundingSphere), spheres.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cullingshader.ssboObjects);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, cullingshader.ssboCommands);
        glBufferData(GL_SHADER_STORAGE_BUFFER, commands.size() * sizeof(DrawCommand), commands.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cullingshader.ssboCommands);
        // 3.4. ЗАПУСКАЕМ COMPUTE SHADER (Ядра RTX 3090 полетели!)
        cullingshader.shader.Activate();
        // Говорим шейдеру: это экран, применяем Occlusion Culling
        glUniform1i(glGetUniformLocation(cullingshader.shader.ID, "isShadowPass"), 0);
        glUniform3fv(glGetUniformLocation(cullingshader.shader.ID, "camPos"), 1, glm::value_ptr(camera.Position));
        for (int i = 0; i < 3; i++) {
            // Если у меша реально есть LOD уровни, берем их, иначе дублируем LOD 0
            unsigned int count = (i < mesh->lods.size()) ? mesh->lods[i].count : mesh->lods[0].count;
            unsigned int offset = (i < mesh->lods.size()) ? mesh->lods[i].firstIndex : mesh->lods[0].firstIndex;
            glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, ("meshLODs[" + std::to_string(i) + "].count").c_str()), count);
            glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, ("meshLODs[" + std::to_string(i) + "].firstIndex").c_str()), offset);
        }
        float lodDist[2] = { 50.0f, 100.0f };
        glUniform1fv(glGetUniformLocation(cullingshader.shader.ID, "lodDistances"), 2, lodDist);
        // Передаем плоскости
        auto planes = cullingshader.ExtractFrustumPlanes(camera.GetViewMatrix());
        for (int i = 0; i < 6; i++) {
            std::string name = "frustumPlanes[" + std::to_string(i) + "]";
            glUniform4f(glGetUniformLocation(cullingshader.shader.ID, name.c_str()), planes[i].normal.x, planes[i].normal.y, planes[i].normal.z, planes[i].distance);
        }
        glUniform1ui(glGetUniformLocation(cullingshader.shader.ID, "objectCount"), batchObjects.size());
        // --- ДАННЫЕ ДЛЯ OCCLUSION CULLING ---
        // Передаем одну готовую матрицу камеры
        glUniformMatrix4fv(glGetUniformLocation(cullingshader.shader.ID, "viewProjection"), 1, GL_FALSE, glm::value_ptr(camera.GetViewMatrix()));
        // Разрешение экрана
        glUniform2f(glGetUniformLocation(cullingshader.shader.ID, "screenSize"), (float)window.width, (float)window.height);
        // Текстура Пирамиды Глубины (Hi-Z)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, postprocessingshader.hiZTexture);
        glUniform1i(glGetUniformLocation(cullingshader.shader.ID, "hiZTexture"), 0);
        // ------------------------------------
        GLuint workGroups = (batchObjects.size() + 255) / 256;
        glDispatchCompute(workGroups, 1, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        // 3.5. ОТРИСОВКА! (Возвращаем обычный шейдер)
        litshader.shader.Activate();
        // Опять передаем чистую View-матрицу
        glUniformMatrix4fv(glGetUniformLocation(litshader.shader.ID, "view"),
            1, GL_FALSE, glm::value_ptr(camera.GetLookAtMatrix()));
        // А твою готовую PV-матрицу (cameraMatrix) продолжаем использовать для координат вершин, как и раньше
        glUniformMatrix4fv(glGetUniformLocation(litshader.shader.ID, "camMatrix"),
            1, GL_FALSE, glm::value_ptr(camera.GetViewMatrix()));
        int matIdx = materialToIndex[mat];
        glUniform1i(glGetUniformLocation(litshader.shader.ID, "materialID"), matIdx);
        mesh->VAO.Bind();
        // МАГИЯ INDIRECT DRAWING: Видеокарта сама прочитает команды и проигнорирует невидимое!
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, cullingshader.ssboCommands);
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, batchObjects.size(), sizeof(DrawCommand));
        
    }
    // =======================================================
    glDisable(GL_CULL_FACE);
    sky.Draw(camera, sunDir);
    // 4. ПОСТ-ОБРАБОТКА (И НАШ SSGI)
    postprocessingshader.Update(window, crntTime, camera, ui, sunDir);
}