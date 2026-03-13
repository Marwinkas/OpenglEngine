#include<filesystem>
namespace fs = std::filesystem;
#include <windows.h>
#include <string>
#include"Model.h"
#include"GameObject.h"
#include "LitShader.h"
#include "ShadowShader.h"
#include "PostProcessingShader.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include "Window.h"
#include <map>
#include <nlohmann/json.hpp>
#include "UI.h"
#include "Render.h"
#include "Serializer.h"
#include "CullingShader.h"
#include "PhysicsEngine.h" 
#include "DefferedShader.h"
#include <TextureStreamer.h>
using namespace entt;
std::vector <GameObject> Objects;
std::vector <Material> material;
std::vector <Mesh> mesh;
std::string getExecutablePaths()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    std::string path = exePath.parent_path().string();

    // Лёша, это поможет нам понять, не "врет" ли путь
    std::cout << "[DEBUG] Путь к EXE: " << path << std::endl;

    return path;
}
double crntTime = 0.0;
glm::vec3 GetMouseRay(Window& window, Camera& camera) {
	double mouseX, mouseY;
	glfwGetCursorPos(window.window, &mouseX, &mouseY);
	glm::vec4 viewport = glm::vec4(0, 0, window.width, window.height);
	glm::mat4 view = glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up);
	glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);
		glm::vec3 nearPos = glm::unProject(glm::vec3(mouseX, window.height - mouseY, 0.0f), view, proj, viewport);
		glm::vec3 farPos = glm::unProject(glm::vec3(mouseX, window.height - mouseY, 1.0f), view, proj, viewport);
	return glm::normalize(farPos - nearPos);
}
TextureStreamer* globalTextureStreamer = nullptr;
int main()
{
    setlocale(LC_ALL, "ru_RU.UTF-8"); // Говорим С-библиотекам про UTF-8
    SetConsoleOutputCP(65001);       // Говорим Windows про UTF-8
    try {
        // Устанавливаем кодировку вывода в UTF-8 (код 65001)

        Window window = Window();
        Objects.reserve(1000);
     
        // Проверяем пути
        std::string exePath = getExecutablePaths();
        std::string projectFolder = exePath + "/project";
        std::string sceneFile = projectFolder + "/level1.bhscene";

        std::cout << "Loading scene: " << sceneFile << std::endl;

        // Загрузка сцены
        Objects = Serializer::LoadScene(sceneFile, projectFolder);

        UI ui(window, projectFolder, exePath);
        CullingShader cullingshader;

        PostProcessingShader postprocessingshader(window);
        ShadowShader shadowshader;
        DefferedShader deferredshader;
        Camera camera(window.width, window.height, glm::vec3(0.0f, 0.0f, 2.0f));
        Render render;
        render.InitGBuffer(window.width, window.height);
        render.UpdateClusterGrid(camera, window, cullingshader);
        TextureStreamer texStreamer;
        globalTextureStreamer = &texStreamer;
        std::cout << "Initializing Shaders..." << std::endl;
        LitShader litshader; // Вот тут скорее всего падает из-за blue_noise.png

        PhysicsEngine physicsEngine;
        physicsEngine.Init();
        physicsEngine.CreateTestScene();
        
        float lastFrame = glfwGetTime();
        physicsEngine.RegisterObjects(Objects);

        entt::registry registry;

        while (!glfwWindowShouldClose(window.window))
        {
            if (texStreamer.Update()) {
                render.isSceneDirty = true; // Текстура догрузилась? Обновляем SSBO!
            }
            float currentFrame = glfwGetTime();
            float deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            if (deltaTime > 0.1f) deltaTime = 0.1f;

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            camera.Inputs(window.window,deltaTime);
            camera.taaFrameIndex++;
            camera.updateMatrix(45.0f, 0.1f, 1000);
            static bool f5Pressed = false;
            if (glfwGetKey(window.window, GLFW_KEY_F5) == GLFW_PRESS) {
                if (!f5Pressed) {
                    camera.TogglePlayMode(physicsEngine);
                    f5Pressed = true;
                }
            }
            else {
                f5Pressed = false;
            }
            camera.UpdatePhysics(deltaTime);
            physicsEngine.SyncTransforms(Objects);
            physicsEngine.Update(deltaTime);

            render.Draw(registry, litshader, shadowshader, postprocessingshader, window, camera, glfwGetTime(), ui, cullingshader, deferredshader);
            ui.Draw(window, camera, registry, render);
            
            physicsEngine.ApplyUIChanges(Objects);
            glfwSwapBuffers(window.window);
            glfwPollEvents();
        }

        glDeleteFramebuffers(1, &postprocessingshader.FBO);
        glDeleteFramebuffers(1, &postprocessingshader.compositeFBO);
        glfwDestroyWindow(window.window);
        glfwTerminate();
        physicsEngine.Cleanup();
    }
    catch (int e) {
        std::cerr << "\n--- КРИТИЧЕСКАЯ ОШИБКА ---" << std::endl;
        std::cerr << "Код ошибки (errno): " << e << std::endl;
        std::cerr << "Скорее всего, не найден файл! Проверь папки project и resources." << std::endl;
        system("pause"); // Чтобы консоль не закрылась
    }
    catch (const std::exception& e) {
        std::cerr << "\n--- СТАНДАРТНОЕ ИСКЛЮЧЕНИЕ ---" << std::endl;
        std::cerr << e.what() << std::endl;
        system("pause");
    }

    return 0;
}