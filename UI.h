#ifndef UI_CLASS_H
#define UI_CLASS_H
#include<glm/glm.hpp>
#include<glm/gtc/quaternion.hpp>
#include<glad/glad.h>
#include<vector>
#include <GLFW/glfw3.h>
#include <iostream>
#include "tinyfiledialogs.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"
#include "Window.h"
#include "Render.h"
#include <map>
#include <nlohmann/json.hpp>
class UI {
public:
	int selectedObjectIndex = 0;
	glm::mat4 model = glm::mat4(1.0f);
	UI(Window& window) {
		// После создания окна GLFW и контекста OpenGL
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuizmo::Enable(true); // Включаем ImGuizmo

		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Поддержка клавиатуры

		// Стиль
		ImGui::StyleColorsDark();

		// Настройка для Viewports
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags) {
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		// Инициализация backend'ов
		ImGui_ImplGlfw_InitForOpenGL(window.window, true); // window — твой GLFW window
		ImGui_ImplOpenGL3_Init("#version 330"); // версию GLSL под OpenGL
	}
	void Draw(Window& window, Camera camera, std::vector<GameObject>& Objects, Render& render) {
		// Начало ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Настроим ImGuizmo
		ImGuizmo::BeginFrame();
		ImGuizmo::SetOrthographic(false); // Используем перспективу
		ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
		ImGuizmo::SetRect(0, 0, window.width, window.height);

		// Редактирование трансформации
		static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
		static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;
		static bool snapEnabled = false;
		static float snapValue[3] = { 1.0f, 1.0f, 1.0f };

		if (ImGui::IsKeyPressed(ImGuiKey_Q)) currentGizmoOperation = ImGuizmo::TRANSLATE; // Z
		if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation = ImGuizmo::ROTATE;    // E
		if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::SCALE;     // R

		glm::mat4 view = glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up);
		glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);

		if (snapEnabled) {
			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
				currentGizmoOperation, currentGizmoMode, glm::value_ptr(model), nullptr, snapValue);
		}
		else {
			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
				currentGizmoOperation, currentGizmoMode, glm::value_ptr(model));
		}

		ImGuiIO& io = ImGui::GetIO();
		ImGuizmo::SetRect(0, 0, window.width, window.height);
		if (!io.WantCaptureMouse) {
			camera.Inputs(window.window);
		}

		// Главное окно управления
		ImGui::Begin("Gizmo Controls");
		ImGui::Text("Press Q/E/R to switch mode");
		ImGui::Text("Select Object:");
		for (int i = 0; i < Objects.size(); i++) {
			char buf[32];
			sprintf_s(buf, "Object %d", i);
			if (ImGui::Selectable(buf, selectedObjectIndex == i)) {
				selectedObjectIndex = i;

				ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(Objects[selectedObjectIndex].transform.position), glm::value_ptr(Objects[selectedObjectIndex].transform.rotation), glm::value_ptr(Objects[selectedObjectIndex].transform.scale),
					glm::value_ptr(model));
			}
		}

		// Выбор операции
		ImGui::Text("Operation:");
		ImGui::RadioButton("Translate", (int*)&currentGizmoOperation, (int)ImGuizmo::TRANSLATE);
		ImGui::SameLine();
		ImGui::RadioButton("Rotate", (int*)&currentGizmoOperation, (int)ImGuizmo::ROTATE);
		ImGui::SameLine();
		ImGui::RadioButton("Scale", (int*)&currentGizmoOperation, (int)ImGuizmo::SCALE);

		// Выбор режима
		ImGui::Text("Mode:");
		ImGui::RadioButton("Local", (int*)&currentGizmoMode, (int)ImGuizmo::LOCAL);
		ImGui::SameLine();
		ImGui::RadioButton("World", (int*)&currentGizmoMode, (int)ImGuizmo::WORLD);

		// Включение snap
		ImGui::Checkbox("Enable Snap", &snapEnabled);
		if (snapEnabled) {
			ImGui::InputFloat3("Snap Value", snapValue);
		}

		// Сброс трансформации
		if (ImGui::Button("Reset Transform")) {
			model = glm::mat4(1.0f);
		}

		// Отображение текущей матрицы объекта

		// Позиция, вращение и масштаб (декомпозиция матрицы)
		glm::vec3 translation, rotation, scale;
		ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), glm::value_ptr(translation),
			glm::value_ptr(rotation), glm::value_ptr(scale));

		ImGui::Separator();
		ImGui::Text("Decomposed Transform:");
		ImGui::InputFloat3("Translation", glm::value_ptr(translation));
		ImGui::InputFloat3("Rotation", glm::value_ptr(rotation));
		ImGui::InputFloat3("Scale", glm::value_ptr(scale));

		if (Objects[selectedObjectIndex].transform.position != translation || Objects[selectedObjectIndex].transform.rotation != rotation || Objects[selectedObjectIndex].transform.scale != scale) {
			Objects[selectedObjectIndex].transform.position = translation;
			Objects[selectedObjectIndex].transform.rotation = rotation;
			Objects[selectedObjectIndex].transform.scale = scale;

			Objects[selectedObjectIndex].transform.updatematrix = true;
			render.lightupdate = false;
		}

		// Применяем обратно, если юзер что-то ввел вручную
		ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(translation), glm::value_ptr(rotation), glm::value_ptr(scale),
			glm::value_ptr(model));

		ImGui::End();

		// Отрисовка ImGui
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
};

#endif