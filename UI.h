#ifndef UI_CLASS_H
#define UI_CLASS_H
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <GLFW/glfw3.h>
#include <iostream>
#include "Serializer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h" 
#include "ImGuizmo.h"
#include "Window.h"
#include "GameObject.h"
#include "Camera.h"
#include "Render.h"
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stb_image.h> 
#include <algorithm> 
#include <windows.h>
#include <shellapi.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

class UI {
public:
    int selectedObjectIndex = 0;
    glm::mat4 model = glm::mat4(1.0f);

    fs::path projectDirectory;
    fs::path ExeDirectory;
    fs::path currentDirectory;

    // --- НАВИГАЦИЯ И БРАУЗЕР ---
    std::vector<fs::path> dirHistory;
    int dirHistoryIndex = -1;
    std::vector<std::string> selectedAssets;
    int lastClickedIndex = -1;
    char searchBuffer[256] = "";

    // Буфер обмена и переименование
    std::vector<std::string> clipboardPaths;
    bool isCut = false;
    std::string renamingPath = "";
    char inlineRenameBuf[256] = "";
    bool focusRename = false;

    // Материалы
    char editAlbedo[256] = ""; char editNormal[256] = ""; char editMetallic[256] = "";
    char editRoughness[256] = ""; char editHeight[256] = ""; char editAO[256] = "";
    std::unordered_map<std::string, GLuint> imageThumbnails;

    // Состояния окон
    bool showOutliner = true; bool showInspector = true;
    bool showProperties = true; bool showContentBrowser = true;
    bool resetLayout = false; bool showAboutModal = false;

    // --- СИСТЕМА ОТМЕНЫ ДЕЙСТВИЙ (UNDO / REDO) ---
    struct SceneSnapshot {
        std::vector<glm::vec3> pos, rot, scl;
    };
    std::vector<SceneSnapshot> undoStack;
    std::vector<SceneSnapshot> redoStack;
    bool wasUsingGizmo = false;

    void SaveState(const std::vector<GameObject>& Objects) {
        SceneSnapshot snap;
        for (const auto& obj : Objects) {
            snap.pos.push_back(obj.transform.position);
            snap.rot.push_back(obj.transform.rotation);
            snap.scl.push_back(obj.transform.scale);
        }
        undoStack.push_back(snap);
        redoStack.clear(); // Очищаем историю возвратов при новом действии
        if (undoStack.size() > 50) undoStack.erase(undoStack.begin()); // Храним только последние 50 шагов
    }

    void Undo(std::vector<GameObject>& Objects) {
        if (undoStack.empty()) return;

        // Сначала сохраняем текущее состояние в Redo
        SceneSnapshot curr;
        for (const auto& obj : Objects) {
            curr.pos.push_back(obj.transform.position); curr.rot.push_back(obj.transform.rotation); curr.scl.push_back(obj.transform.scale);
        }
        redoStack.push_back(curr);

        // Применяем предыдущее состояние
        SceneSnapshot snap = undoStack.back();
        undoStack.pop_back();
        for (size_t i = 0; i < Objects.size() && i < snap.pos.size(); ++i) {
            Objects[i].transform.position = snap.pos[i];
            Objects[i].transform.rotation = snap.rot[i];
            Objects[i].transform.scale = snap.scl[i];
            Objects[i].transform.updatematrix = true;
        }
    }

    void Redo(std::vector<GameObject>& Objects) {
        if (redoStack.empty()) return;

        SceneSnapshot curr;
        for (const auto& obj : Objects) {
            curr.pos.push_back(obj.transform.position); curr.rot.push_back(obj.transform.rotation); curr.scl.push_back(obj.transform.scale);
        }
        undoStack.push_back(curr);

        SceneSnapshot snap = redoStack.back();
        redoStack.pop_back();
        for (size_t i = 0; i < Objects.size() && i < snap.pos.size(); ++i) {
            Objects[i].transform.position = snap.pos[i];
            Objects[i].transform.rotation = snap.rot[i];
            Objects[i].transform.scale = snap.scl[i];
            Objects[i].transform.updatematrix = true;
        }
    }

    struct PostProcessSettings {
        bool enableSSAO = true; float ssaoRadius = 0.5f; float ssaoBias = 0.025f; float ssaoIntensity = 2.0f; float ssaoPower = 2.0f;
        bool enableSSGI = true; int ssgiRayCount = 8; float ssgiStepSize = 0.4f; float ssgiThickness = 0.5f;
        int blurRange = 4; float gamma = 2.2f;
        bool autoExposure = true; float manualExposure = 1.0f; float exposureCompensation = 1.0f; float minBrightness = 0.5f; float maxBrightness = 3.0f;
        float contrast = 1.0f; float saturation = 1.0f;
        bool enableVignette = false; float vignetteIntensity = 0.5f;
        bool enableChromaticAberration = false; float caIntensity = 0.005f;
        bool enableBloom = true; float bloomThreshold = 1.0f; float bloomIntensity = 1.5f; int bloomBlurIterations = 10;
        bool enableLensFlares = true; float flareIntensity = 0.5f; float ghostDispersal = 0.3f; int ghosts = 4;
        float currentExposure = 1.0f; float temperature = 8000.0f;
        bool enableDoF = false; float focusDistance = 10.0f; float focusRange = 3.0f; float bokehSize = 2.0f;
        bool enableMotionBlur = false; float mbStrength = 0.5f;
        bool enableGodRays = false; float godRaysIntensity = 1.0f;
        bool enableFilmGrain = false; float grainIntensity = 0.05f;
        bool enableSharpen = false; float sharpenIntensity = 0.5f;
        bool enableFog = true; float fogDensity = 0.02f; float fogHeightFalloff = 0.2f; float fogBaseHeight = 0.0f;
        glm::vec3 fogColor = glm::vec3(0.5f, 0.6f, 0.7f); glm::vec3 inscatterColor = glm::vec3(1.0f, 0.8f, 0.5f); float inscatterPower = 8.0f; float inscatterIntensity = 1.0f;
    } ppSettings;

    // ==========================================
    // ФИОЛЕТОВАЯ ТЕМА BURNHOPE
    // ==========================================
    void SetupBurnhopeTheme() {
        ImGuiStyle& style = ImGui::GetStyle(); ImVec4* colors = style.Colors;
        style.WindowRounding = 6.0f; style.ChildRounding = 4.0f; style.FrameRounding = 4.0f; style.PopupRounding = 6.0f; style.TabRounding = 6.0f;
        style.WindowBorderSize = 1.0f; style.FrameBorderSize = 0.0f; style.PopupBorderSize = 1.0f; style.ItemSpacing = ImVec2(8, 6);

        // Глубокий темно-фиолетовый фон
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.08f, 0.11f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.10f, 0.13f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);

        // Поля ввода и фоны
        colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.22f, 0.50f, 1.00f);

        // Яркие фиолетовые акценты для кнопок
        colors[ImGuiCol_Button] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.22f, 0.50f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.48f, 0.30f, 0.68f, 1.00f);

        // Заголовки (Collapsing headers)
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.16f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.20f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.25f, 0.55f, 1.00f);

        // Вкладки
        colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.12f, 0.17f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.20f, 0.38f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);

        // Ползунки и галочки
        colors[ImGuiCol_CheckMark] = ImVec4(0.68f, 0.45f, 0.95f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.35f, 0.85f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.70f, 0.50f, 1.00f, 1.00f);

        // Системные цвета
        colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.08f, 0.11f, 1.00f);
    }

    UI(Window& window, const std::string& projectPath, const std::string& exePath) {
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuizmo::Enable(true);
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // --- ПОДДЕРЖКА КИРИЛЛИЦЫ ---
        // Загружаем стандартный шрифт Windows, который умеет читать русский язык
        ImFontConfig font_cfg;
        font_cfg.OversampleH = 2; font_cfg.OversampleV = 2;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());

        SetupBurnhopeTheme();
        ImGui_ImplGlfw_InitForOpenGL(window.window, true); ImGui_ImplOpenGL3_Init("#version 330");

        projectDirectory = projectPath; currentDirectory = projectPath; ExeDirectory = exePath;
        dirHistory.push_back(currentDirectory); dirHistoryIndex = 0;
        LoadPostProcessSettings();
    }
    // ==========================================
    // Вспомогательные функции для UI и файлов
    // ==========================================
    std::string TruncateText(const std::string& text, float maxWidth) {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
        std::string res = text;
        while (res.length() > 0 && ImGui::CalcTextSize((res + "...").c_str()).x > maxWidth) res.pop_back();
        return res + "...";
    }

    std::string GetFileTypeName(const std::string& ext, bool isDir) {
        if (isDir) return "Folder";
        if (ext == ".bhmat") return "Material";
        if (ext == ".bhscene") return "Scene";
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return "Image";
        if (ext == ".obj" || ext == ".fbx") return "Model";
        return "File";
    }

    void MoveToRecycleBin(const std::string& path) {
        std::string winPath = fs::absolute(path).string();
        std::replace(winPath.begin(), winPath.end(), '/', '\\');
        winPath.push_back('\0'); winPath.push_back('\0');
        SHFILEOPSTRUCTA fileOp = { 0 }; fileOp.wFunc = FO_DELETE; fileOp.pFrom = winPath.c_str();
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        SHFileOperationA(&fileOp);
    }

    bool IsSelected(const std::string& path) {
        return std::find(selectedAssets.begin(), selectedAssets.end(), path) != selectedAssets.end();
    }

    std::string GetPrimarySelection() { return selectedAssets.empty() ? "" : selectedAssets.back(); }

    void NavigateTo(const fs::path& target) {
        if (currentDirectory == target) return;
        if (dirHistoryIndex < dirHistory.size() - 1) dirHistory.erase(dirHistory.begin() + dirHistoryIndex + 1, dirHistory.end());
        dirHistory.push_back(target); dirHistoryIndex++; currentDirectory = target;
        selectedAssets.clear(); renamingPath = ""; lastClickedIndex = -1;
    }

    void StartRename(const std::string& path) {
        renamingPath = path;
        strcpy_s(inlineRenameBuf, sizeof(inlineRenameBuf), fs::path(path).stem().string().c_str());
        focusRename = true;
    }

    void ApplyRename() {
        if (!renamingPath.empty() && strlen(inlineRenameBuf) > 0) {
            fs::path oldP(renamingPath);
            std::string newName = std::string(inlineRenameBuf) + oldP.extension().string();
            fs::path newP = oldP.parent_path() / newName;
            if (oldP != newP && !fs::exists(newP)) {
                fs::rename(oldP, newP);
                auto it = std::find(selectedAssets.begin(), selectedAssets.end(), renamingPath);
                if (it != selectedAssets.end()) *it = newP.string();
            }
        }
        renamingPath = "";
    }

    void PasteCopiedItems(const fs::path& targetDir) {
        if (clipboardPaths.empty()) return;
        for (const auto& cbPath : clipboardPaths) {
            if (!fs::exists(cbPath)) continue;
            fs::path src(cbPath); fs::path dst = targetDir / src.filename();
            int copyCount = 1;
            while (fs::exists(dst)) {
                dst = targetDir / (src.stem().string() + " " + std::to_string(copyCount) + src.extension().string());
                copyCount++;
            }
            if (isCut) fs::rename(src, dst);
            else fs::copy(src, dst, fs::copy_options::recursive);
        }
        if (isCut) { clipboardPaths.clear(); isCut = false; }
    }
    // Загрузка текстур из .bhmat
    void LoadMaterialToProperties(const std::string& path) {
        memset(editAlbedo, 0, sizeof(editAlbedo)); memset(editNormal, 0, sizeof(editNormal));
        memset(editHeight, 0, sizeof(editHeight)); memset(editAO, 0, sizeof(editAO));
        memset(editMetallic, 0, sizeof(editMetallic)); memset(editRoughness, 0, sizeof(editRoughness));
        std::ifstream file(path);
        if (file.is_open()) {
            json j; try { file >> j; }
            catch (...) { return; }
            if (j.contains("textures")) {
                if (j["textures"].contains("albedo")) strcpy_s(editAlbedo, j["textures"]["albedo"].get<std::string>().c_str());
                if (j["textures"].contains("normal")) strcpy_s(editNormal, j["textures"]["normal"].get<std::string>().c_str());
                if (j["textures"].contains("height")) strcpy_s(editHeight, j["textures"]["height"].get<std::string>().c_str());
                if (j["textures"].contains("ao")) strcpy_s(editAO, j["textures"]["ao"].get<std::string>().c_str());
                if (j["textures"].contains("metallic")) strcpy_s(editMetallic, j["textures"]["metallic"].get<std::string>().c_str());
                if (j["textures"].contains("roughness")) strcpy_s(editRoughness, j["textures"]["roughness"].get<std::string>().c_str());
            }
        }
    }

    // Дерево папок слева
    void DrawFolderTree(const fs::path& dir) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (currentDirectory == dir) flags |= ImGuiTreeNodeFlags_Selected;

        bool isLeaf = true;
        for (auto& entry : fs::directory_iterator(dir)) { if (entry.is_directory()) { isLeaf = false; break; } }
        if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf;

        std::string nodeName = dir == projectDirectory ? "All (Project)" : dir.filename().string();
        bool isOpen = ImGui::TreeNodeEx(nodeName.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) NavigateTo(dir);

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                for (const auto& selPath : selectedAssets) {
                    fs::path src(selPath);
                    if (src != dir) fs::rename(src, dir / src.filename());
                }
                selectedAssets.clear();
            }
            ImGui::EndDragDropTarget();
        }

        if (isOpen) {
            for (auto& entry : fs::directory_iterator(dir)) { if (entry.is_directory()) DrawFolderTree(entry.path()); }
            ImGui::TreePop();
        }
    }

    GLuint GetImageThumbnail(const std::string& fullPath) {
        if (imageThumbnails.find(fullPath) != imageThumbnails.end()) return imageThumbnails[fullPath];
        int w, h, c; unsigned char* data = stbi_load(fullPath.c_str(), &w, &h, &c, 4);
        if (!data) { imageThumbnails[fullPath] = 0; return 0; }
        GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data); imageThumbnails[fullPath] = id; return id;
    }

    GLuint GetMaterialThumbnail(const std::string& matPath) {
        std::ifstream file(matPath); if (!file.is_open()) return 0;
        json j; try { file >> j; }
        catch (...) { return 0; }
        if (!j.contains("textures") || !j["textures"].contains("albedo")) return 0;
        return GetImageThumbnail(projectDirectory.string() + "/" + j["textures"]["albedo"].get<std::string>());
    }
    // Загрузчик иконок для браузера файлов
    GLuint GetFileIcon(const std::string& ext, bool isDir) {
        if (isDir) return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_folder.png");
        if (ext == ".bhmat") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_material.png");
        if (ext == ".bhscene") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_scene.png");
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_model.png");

        // Для картинок мы и так грузим саму картинку, эта иконка нужна только если загрузка сломалась
        return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_file.png");
    }
    void DrawMainMenuBar(std::vector<GameObject>& Objects) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) { Objects.clear(); selectedObjectIndex = -1; undoStack.clear(); redoStack.clear(); }
                if (ImGui::BeginMenu("Recent Scenes")) { ImGui::MenuItem("level1.bhscene"); ImGui::EndMenu(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene")) { Serializer::SaveScene(projectDirectory.string() + "/level1.bhscene", Objects); }
                if (ImGui::MenuItem("Save All")) { SavePostProcessSettings(); Serializer::SaveScene(projectDirectory.string() + "/level1.bhscene", Objects); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                // Кнопки меню привязаны к функциям
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) { Undo(Objects); }
                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !redoStack.empty())) { Redo(Objects); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Scene Outliner", NULL, &showOutliner); ImGui::MenuItem("Scene Inspector", NULL, &showInspector);
                ImGui::MenuItem("Properties", NULL, &showProperties); ImGui::MenuItem("Content Browser", NULL, &showContentBrowser); ImGui::Separator();
                if (ImGui::MenuItem("Restore Defaults")) { resetLayout = true; } ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    // ==========================================
    // ПРОДВИНУТЫЙ CONTENT BROWSER
    // ==========================================
    void DrawContentBrowser() {
        if (!showContentBrowser) return;
        ImGui::Begin("Content Browser", &showContentBrowser);

        // ГЛОБАЛЬНЫЕ ГОРЯЧИЕ КЛАВИШИ
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !selectedAssets.empty() && renamingPath.empty()) {
                for (const auto& path : selectedAssets) MoveToRecycleBin(path);
                selectedAssets.clear(); lastClickedIndex = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2) && selectedAssets.size() == 1 && renamingPath.empty()) {
                StartRename(selectedAssets[0]);
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !selectedAssets.empty()) { clipboardPaths = selectedAssets; isCut = false; }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X) && !selectedAssets.empty()) { clipboardPaths = selectedAssets; isCut = true; }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !clipboardPaths.empty()) { PasteCopiedItems(currentDirectory); }
        }

        // --- ВЕРХНЯЯ ПАНЕЛЬ ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (dirHistoryIndex > 0) { if (ImGui::Button("<")) { dirHistoryIndex--; currentDirectory = dirHistory[dirHistoryIndex]; selectedAssets.clear(); } }
        else { ImGui::TextDisabled("<"); }
        ImGui::SameLine();
        if (dirHistoryIndex < dirHistory.size() - 1) { if (ImGui::Button(">")) { dirHistoryIndex++; currentDirectory = dirHistory[dirHistoryIndex]; selectedAssets.clear(); } }
        else { ImGui::TextDisabled(">"); }
        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
        ImGui::PopStyleColor();

        if (ImGui::Button(" + Create ")) ImGui::OpenPopup("CreateMenuPopup");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##Search", "Search all folders...", searchBuffer, sizeof(searchBuffer));
        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        // --- BREADCRUMBS И DRAG&DROP В НИХ ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button("All")) NavigateTo(projectDirectory);
        if (ImGui::BeginDragDropTarget()) { // Drop в All
            if (ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                for (const auto& selPath : selectedAssets) { fs::path src(selPath); if (src != projectDirectory) fs::rename(src, projectDirectory / src.filename()); }
                selectedAssets.clear();
            }
            ImGui::EndDragDropTarget();
        }

        fs::path rel = fs::relative(currentDirectory, projectDirectory); fs::path accum = projectDirectory;
        if (rel.string() != ".") {
            for (auto it = rel.begin(); it != rel.end(); ++it) {
                ImGui::SameLine(); ImGui::Text(">"); ImGui::SameLine(); accum /= *it;
                if (ImGui::Button(it->string().c_str())) NavigateTo(accum);

                // Drop в конкретную папку в пути!
                if (ImGui::BeginDragDropTarget()) {
                    if (ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                        for (const auto& selPath : selectedAssets) { fs::path src(selPath); if (src != accum) fs::rename(src, accum / src.filename()); }
                        selectedAssets.clear();
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        }
        ImGui::PopStyleColor();
        ImGui::Separator();

        // --- МЕНЮ CREATE ---
        // ИСПОЛЬЗУЕМ fs::path ДЛЯ ПРАВИЛЬНЫХ СЛЭШЕЙ
        if (ImGui::BeginPopup("CreateMenuPopup")) {
            if (ImGui::MenuItem("New Folder")) {
                fs::path newPath = currentDirectory / "New Folder"; int count = 1;
                while (fs::exists(newPath)) { newPath = currentDirectory / ("New Folder " + std::to_string(count)); count++; }
                fs::create_directory(newPath);
                selectedAssets.clear(); selectedAssets.push_back(newPath.string()); StartRename(newPath.string());
            }
            if (ImGui::MenuItem("Material (.bhmat)")) {
                fs::path newPath = currentDirectory / "New Material.bhmat"; int count = 1;
                while (fs::exists(newPath)) { newPath = currentDirectory / ("New Material " + std::to_string(count) + ".bhmat"); count++; }
                json j; j["name"] = "New Material"; j["textures"] = { {"albedo", ""}, {"normal", ""}, {"height", ""}, {"ao", ""}, {"metallic", ""}, {"roughness", ""} };
                std::ofstream file(newPath); file << j.dump(4);
                selectedAssets.clear(); selectedAssets.push_back(newPath.string()); LoadMaterialToProperties(newPath.string()); StartRename(newPath.string());
            }
            ImGui::EndPopup();
        }

        ImGui::Columns(2, "CB_Columns", true);
        if (ImGui::GetColumnWidth() == ImGui::GetContentRegionAvail().x) ImGui::SetColumnWidth(0, 200.0f);
        ImGui::SetColumnWidth(0, 200.0f);
        // === ЛЕВАЯ ПАНЕЛЬ ===
        ImGui::BeginChild("LeftTreePanel");
        DrawFolderTree(projectDirectory);
        ImGui::EndChild();

        ImGui::NextColumn();

        // === ПРАВАЯ ПАНЕЛЬ ===
        ImGui::BeginChild("RightGridPanel");

        std::vector<fs::directory_entry> items;
        std::string searchStr(searchBuffer); std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        if (!searchStr.empty()) {
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                std::string lowerName = entry.path().filename().string(); std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName.find(searchStr) != std::string::npos) items.push_back(entry);
            }
        }
        else {
            for (auto& entry : fs::directory_iterator(currentDirectory)) items.push_back(entry);
        }

        std::sort(items.begin(), items.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            if (a.is_directory() && !b.is_directory()) return true;
            if (!a.is_directory() && b.is_directory()) return false;
            return a.path().filename().string() < b.path().filename().string();
            });

        if (ImGui::BeginPopupContextWindow("CB_Bg_Context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New Folder")) {
                fs::path p = currentDirectory / "New Folder"; int c = 1; while (fs::exists(p)) { p = currentDirectory / ("New Folder " + std::to_string(c++)); }
                fs::create_directory(p); selectedAssets.clear(); selectedAssets.push_back(p.string()); StartRename(p.string());
            }
            if (ImGui::MenuItem("Material (.bhmat)")) {
                fs::path p = currentDirectory / "New Material.bhmat"; int c = 1; while (fs::exists(p)) { p = currentDirectory / ("New Material " + std::to_string(c++) + ".bhmat"); }
                json j; j["name"] = "New Material"; j["textures"] = { {"albedo", ""}, {"normal", ""}, {"height", ""}, {"ao", ""}, {"metallic", ""}, {"roughness", ""} };
                std::ofstream file(p); file << j.dump(4); selectedAssets.clear(); selectedAssets.push_back(p.string()); LoadMaterialToProperties(p.string()); StartRename(p.string());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardPaths.empty())) { PasteCopiedItems(currentDirectory); }
            ImGui::EndPopup();
        }

        // --- МАТЕМАТИКА ИДЕАЛЬНОЙ СЕТКИ ---
        float padding = 16.0f;
        float thumbnailSize = 64.0f;
        // Выделяем место: иконка + отступы сбоку + место для 2 строк текста снизу
        float itemWidth = thumbnailSize + 16.0f;
        float itemHeight = thumbnailSize + 45.0f;
        float cellSize = itemWidth + padding;

        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize); if (columnCount < 1) columnCount = 1;
        ImGui::Columns(columnCount, 0, false);

        for (int i = 0; i < items.size(); ++i) {
            auto& entry = items[i];
            const auto& path = entry.path(); std::string pathStr = path.string(); std::string ext = path.extension().string();
            bool isDir = entry.is_directory();
            std::string nameNoExt = path.stem().string();
            std::string typeStr = GetFileTypeName(ext, isDir);

            ImGui::PushID(pathStr.c_str());

            // 1. Центрируем элемент внутри колонки
            float colWidth = ImGui::GetColumnWidth();
            float offsetX = (colWidth - itemWidth) / 2.0f;
            if (offsetX < 0) offsetX = 0; // Защита от слишком узких колонок

            ImVec2 startPos = ImGui::GetCursorScreenPos();
            // Точка старта нашего безопасного "пузыря" для элемента
            ImVec2 itemPos = ImVec2(startPos.x + offsetX, startPos.y + padding / 2.0f);

            bool isSel = IsSelected(pathStr);
            bool isHovered = ImGui::IsMouseHoveringRect(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight));

            // РИСУЕМ КРАСИВЫЙ ФОН
            if (isSel) ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(36, 112, 204, 150), 8.0f);
            else if (isHovered) ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(60, 70, 85, 120), 8.0f);

            // 2. Невидимая кнопка поверх этого "пузыря"
            ImGui::SetCursorScreenPos(itemPos);
            ImGui::InvisibleButton("##hitbox", ImVec2(itemWidth, itemHeight));

            // ЛОГИКА ВЫДЕЛЕНИЯ
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseClicked(0)) {
                    if (ImGui::GetIO().KeyCtrl) {
                        if (isSel) selectedAssets.erase(std::remove(selectedAssets.begin(), selectedAssets.end(), pathStr), selectedAssets.end());
                        else { selectedAssets.push_back(pathStr); if (ext == ".bhmat") LoadMaterialToProperties(pathStr); }
                        lastClickedIndex = i;
                    }
                    else if (ImGui::GetIO().KeyShift && lastClickedIndex != -1) {
                        selectedAssets.clear();
                        int start = std::min(i, lastClickedIndex); int end = std::max(i, lastClickedIndex);
                        for (int j = start; j <= end; j++) { selectedAssets.push_back(items[j].path().string()); }
                    }
                    else {
                        if (!isSel) {
                            selectedAssets.clear(); selectedAssets.push_back(pathStr);
                            if (ext == ".bhmat") LoadMaterialToProperties(pathStr);
                        }
                        lastClickedIndex = i;
                    }
                }
                if (ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) {
                    if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && isSel && selectedAssets.size() > 1) {
                        selectedAssets.clear(); selectedAssets.push_back(pathStr);
                        if (ext == ".bhmat") LoadMaterialToProperties(pathStr);
                    }
                }
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (isDir) NavigateTo(path);
                }
            }

            // Drag & Drop
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                if (!isSel) { selectedAssets.clear(); selectedAssets.push_back(pathStr); }
                ImGui::SetDragDropPayload("CB_ITEMS", nullptr, 0);
                ImGui::Text("Move %d items", (int)selectedAssets.size()); ImGui::EndDragDropSource();
            }
            if (isDir && ImGui::BeginDragDropTarget()) {
                if (ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                    for (const auto& selPath : selectedAssets) {
                        fs::path src(selPath); if (src != path) fs::rename(src, path / src.filename());
                    }
                    selectedAssets.clear();
                }
                ImGui::EndDragDropTarget();
            }

            // Контекстное меню
            if (ImGui::BeginPopupContextItem("ItemContext")) {
                if (!isSel) { selectedAssets.clear(); selectedAssets.push_back(pathStr); if (ext == ".bhmat") LoadMaterialToProperties(pathStr); }
                if (ImGui::MenuItem("Open")) { if (isDir) NavigateTo(path); } ImGui::Separator();
                if (ImGui::MenuItem("Cut", "Ctrl+X")) { clipboardPaths = selectedAssets; isCut = true; }
                if (ImGui::MenuItem("Copy", "Ctrl+C")) { clipboardPaths = selectedAssets; isCut = false; } ImGui::Separator();
                if (ImGui::MenuItem("Rename", "F2", false, selectedAssets.size() == 1)) { StartRename(pathStr); }
                if (ImGui::MenuItem("Delete", "Del")) { for (auto& p : selectedAssets) MoveToRecycleBin(p); selectedAssets.clear(); }
                ImGui::EndPopup();
            }

            // === 3. ОТРИСОВКА ИКОНКИ (Строго по центру) ===
            ImGui::SetCursorScreenPos(ImVec2(itemPos.x + (itemWidth - thumbnailSize) / 2.0f, itemPos.y + 6.0f));

            GLuint texID = 0;
            if (!isDir && (ext == ".png" || ext == ".jpg" || ext == ".jpeg")) {
                texID = GetImageThumbnail(pathStr); // Для картинок показываем их самих
            }
            else {
                texID = GetFileIcon(ext, isDir); // Для остальных берем наши новые иконки из Resources
            }

            if (texID != 0) {
                ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(thumbnailSize, thumbnailSize));
            }
            else {
                // Если картинка иконки не найдена, рисуем красивую заглушку
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.22f, 0.25f, 1.0f));
                ImGui::Button(isDir ? "DIR" : "FILE", ImVec2(thumbnailSize, thumbnailSize));
                ImGui::PopStyleColor();
            }

            // === 4. ОТРИСОВКА ТЕКСТА ===
            if (renamingPath == pathStr) {
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + 4, itemPos.y + thumbnailSize + 10.0f));
                ImGui::SetNextItemWidth(itemWidth - 8);
                if (focusRename) { ImGui::SetKeyboardFocusHere(); focusRename = false; ImGui::SetScrollHereY(); }
                if (ImGui::InputText("##rename", inlineRenameBuf, sizeof(inlineRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) { ApplyRename(); }
                if (!ImGui::IsItemActive() && !focusRename && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()) { ApplyRename(); }
            }
            else {
                // Центрируем ИМЯ
                std::string truncName = TruncateText(nameNoExt, itemWidth - 8.0f);
                float textOffset = (itemWidth - ImGui::CalcTextSize(truncName.c_str()).x) / 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + textOffset, itemPos.y + thumbnailSize + 10.0f));
                ImGui::Text("%s", truncName.c_str());

                // Центрируем ТИП
                float typeOffset = (itemWidth - ImGui::CalcTextSize(typeStr.c_str()).x) / 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + typeOffset, itemPos.y + thumbnailSize + 26.0f));
                ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 1.0f), "%s", typeStr.c_str());
            }

            ImGui::NextColumn(); ImGui::PopID();
        }
        ImGui::Columns(1); ImGui::EndChild(); ImGui::Columns(1);
        ImGui::End();
    }



    // ... (Функции SavePostProcessSettings, LoadPostProcessSettings, DrawPostProcessContent, DrawTextureProperty, DrawPropertiesWindow, TestRayOBB, GetMouseRay остаются как в прошлом коде) ...
    // Вставь их сюда, они работают идеально!

    void SavePostProcessSettings() {
        std::string path = projectDirectory.string() + "/postprocess.json"; json j;
        j["enableSSAO"] = ppSettings.enableSSAO; j["ssaoRadius"] = ppSettings.ssaoRadius; j["ssaoBias"] = ppSettings.ssaoBias; j["ssaoIntensity"] = ppSettings.ssaoIntensity; j["ssaoPower"] = ppSettings.ssaoPower;
        j["enableSSGI"] = ppSettings.enableSSGI; j["ssgiRayCount"] = ppSettings.ssgiRayCount; j["ssgiStepSize"] = ppSettings.ssgiStepSize; j["ssgiThickness"] = ppSettings.ssgiThickness;
        j["blurRange"] = ppSettings.blurRange; j["gamma"] = ppSettings.gamma;
        j["enableBloom"] = ppSettings.enableBloom; j["bloomThreshold"] = ppSettings.bloomThreshold; j["bloomIntensity"] = ppSettings.bloomIntensity; j["bloomBlurIterations"] = ppSettings.bloomBlurIterations;
        j["enableLensFlares"] = ppSettings.enableLensFlares; j["flareIntensity"] = ppSettings.flareIntensity; j["ghostDispersal"] = ppSettings.ghostDispersal; j["ghosts"] = ppSettings.ghosts;
        std::ofstream file(path); file << j.dump(4);
    }

    void LoadPostProcessSettings() {
        std::string path = projectDirectory.string() + "/postprocess.json"; std::ifstream file(path);
        if (!file.is_open()) { SavePostProcessSettings(); return; }
        json j; try { file >> j; }
        catch (...) { return; }
        if (j.contains("enableSSAO")) ppSettings.enableSSAO = j["enableSSAO"]; if (j.contains("ssaoRadius")) ppSettings.ssaoRadius = j["ssaoRadius"]; if (j.contains("ssaoBias")) ppSettings.ssaoBias = j["ssaoBias"]; if (j.contains("ssaoIntensity")) ppSettings.ssaoIntensity = j["ssaoIntensity"]; if (j.contains("ssaoPower")) ppSettings.ssaoPower = j["ssaoPower"];
        if (j.contains("enableSSGI")) ppSettings.enableSSGI = j["enableSSGI"]; if (j.contains("ssgiRayCount")) ppSettings.ssgiRayCount = j["ssgiRayCount"]; if (j.contains("ssgiStepSize")) ppSettings.ssgiStepSize = j["ssgiStepSize"]; if (j.contains("ssgiThickness")) ppSettings.ssgiThickness = j["ssgiThickness"];
        if (j.contains("blurRange")) ppSettings.blurRange = j["blurRange"]; if (j.contains("gamma")) ppSettings.gamma = j["gamma"];
        if (j.contains("enableBloom")) ppSettings.enableBloom = j["enableBloom"]; if (j.contains("bloomThreshold")) ppSettings.bloomThreshold = j["bloomThreshold"]; if (j.contains("bloomIntensity")) ppSettings.bloomIntensity = j["bloomIntensity"]; if (j.contains("bloomBlurIterations")) ppSettings.bloomBlurIterations = j["bloomBlurIterations"];
        if (j.contains("enableLensFlares")) ppSettings.enableLensFlares = j["enableLensFlares"]; if (j.contains("flareIntensity")) ppSettings.flareIntensity = j["flareIntensity"]; if (j.contains("ghostDispersal")) ppSettings.ghostDispersal = j["ghostDispersal"]; if (j.contains("ghosts")) ppSettings.ghosts = j["ghosts"];
    }

    void DrawPostProcessContent() {
        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Post Processing Settings"); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::BeginTabBar("PP_Tabs")) {
            if (ImGui::BeginTabItem("Lighting")) {
                if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Checkbox("Enable SSAO", &ppSettings.enableSSAO); ImGui::SliderFloat("Radius##ssao", &ppSettings.ssaoRadius, 0.1f, 3.0f); ImGui::SliderFloat("Intensity##ssao", &ppSettings.ssaoIntensity, 0.1f, 10.0f); ImGui::SliderFloat("Power##ssao", &ppSettings.ssaoPower, 1.0f, 8.0f); }
                if (ImGui::CollapsingHeader("SSGI", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Checkbox("Enable SSGI", &ppSettings.enableSSGI); ImGui::SliderInt("Ray Count", &ppSettings.ssgiRayCount, 1, 32); ImGui::SliderFloat("Step Size", &ppSettings.ssgiStepSize, 0.05f, 2.0f); }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Camera & Fog")) {
                if (ImGui::CollapsingHeader("Depth of Field")) { ImGui::Checkbox("Enable DoF", &ppSettings.enableDoF); ImGui::SliderFloat("Focus Dist", &ppSettings.focusDistance, 0.1f, 100.0f); ImGui::SliderFloat("Focus Range", &ppSettings.focusRange, 0.1f, 50.0f); ImGui::SliderFloat("Bokeh Size", &ppSettings.bokehSize, 0.0f, 10.0f); }
                if (ImGui::CollapsingHeader("Atmospheric Fog")) { ImGui::Checkbox("Enable Fog", &ppSettings.enableFog); ImGui::ColorEdit3("Fog Color", glm::value_ptr(ppSettings.fogColor)); ImGui::SliderFloat("Density", &ppSettings.fogDensity, 0.0f, 0.05f, "%.4f"); ImGui::SliderFloat("Falloff", &ppSettings.fogHeightFalloff, 0.001f, 1.0f); }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bloom / FX")) {
                if (ImGui::CollapsingHeader("Bloom Settings", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Checkbox("Enable Bloom", &ppSettings.enableBloom); ImGui::SliderFloat("Threshold##bloom", &ppSettings.bloomThreshold, 0.0f, 5.0f); ImGui::SliderFloat("Intensity##bloom", &ppSettings.bloomIntensity, 0.0f, 5.0f); ImGui::SliderInt("Blur Iterations", &ppSettings.bloomBlurIterations, 1, 15); }
                if (ImGui::CollapsingHeader("Lens Flares", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Checkbox("Enable Lens Flares", &ppSettings.enableLensFlares); if (ppSettings.enableLensFlares) { ImGui::SliderFloat("Flare Intensity", &ppSettings.flareIntensity, 0.0f, 5.0f); ImGui::SliderFloat("Ghost Dispersal", &ppSettings.ghostDispersal, 0.01f, 1.0f); ImGui::SliderInt("Ghosts Count", &ppSettings.ghosts, 1, 10); } }
                if (ImGui::CollapsingHeader("Cinematic FX")) { ImGui::Checkbox("God Rays", &ppSettings.enableGodRays); if (ppSettings.enableGodRays) ImGui::SliderFloat("Rays Power", &ppSettings.godRaysIntensity, 0.0f, 3.0f); ImGui::Checkbox("Film Grain", &ppSettings.enableFilmGrain); if (ppSettings.enableFilmGrain) ImGui::SliderFloat("Grain Strength", &ppSettings.grainIntensity, 0.0f, 0.2f); }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Color Grading")) {
                ImGui::Text("Exposure Control"); ImGui::Checkbox("Auto Exposure", &ppSettings.autoExposure);
                if (ppSettings.autoExposure) ImGui::SliderFloat("Compensation", &ppSettings.exposureCompensation, 0.1f, 5.0f); else ImGui::SliderFloat("Manual Exp", &ppSettings.manualExposure, 0.1f, 10.0f);
                ImGui::Separator(); ImGui::SliderFloat("Contrast", &ppSettings.contrast, 0.5f, 2.0f); ImGui::SliderFloat("Saturation", &ppSettings.saturation, 0.0f, 2.0f); ImGui::SliderFloat("Color Temp", &ppSettings.temperature, 2000.0f, 12000.0f); ImGui::SliderFloat("Gamma", &ppSettings.gamma, 1.0f, 2.8f);
                ImGui::Checkbox("Sharpening", &ppSettings.enableSharpen); if (ppSettings.enableSharpen) ImGui::SliderFloat("Sharpness", &ppSettings.sharpenIntensity, 0.0f, 1.0f);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Spacing(); ImGui::Separator();
        if (ImGui::Button("💾 SAVE SETTINGS", ImVec2(-1, 40))) SavePostProcessSettings();
    }

    void DrawTextureProperty(const char* label, char* pathBuffer, size_t bufferSize) {
        ImGui::PushID(label); ImGui::Text("%s", label); GLuint texID = 0;
        if (strlen(pathBuffer) > 0) texID = GetImageThumbnail(projectDirectory.string() + "/" + pathBuffer);
        if (texID != 0) ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(64, 64)); else ImGui::Button("NO TEX", ImVec2(64, 64));
        ImGui::SameLine(); ImGui::BeginGroup(); ImGui::TextWrapped("%s", strlen(pathBuffer) > 0 ? pathBuffer : "None");
        if (ImGui::Button("Select Texture...")) ImGui::OpenPopup("TexturePickerPopup"); ImGui::EndGroup();

        if (ImGui::BeginPopup("TexturePickerPopup")) {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Available Textures:"); ImGui::Separator();
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                        std::string relPath = fs::relative(entry.path(), projectDirectory).string(); std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        if (ImGui::Selectable(relPath.c_str())) strcpy_s(pathBuffer, bufferSize, relPath.c_str());
                    }
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    void DrawPropertiesWindow() {
        if (!showProperties) return;
        ImGui::Begin("Properties", &showProperties);

        std::string activeAsset = GetPrimarySelection();

        // Окно свойств показывает данные только если выделен 1 элемент, ИЛИ это json пост-обработки
        if (selectedAssets.size() > 1) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Multiple items selected (%d)", (int)selectedAssets.size());
            ImGui::End(); return;
        }
        if (activeAsset.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an asset in Content Browser");
            ImGui::End(); return;
        }

        fs::path p(activeAsset); std::string ext = p.extension().string(); std::string filename = p.filename().string();

        if (filename == "postprocess.json") DrawPostProcessContent();
        else if (ext == ".png" || ext == ".jpg") {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Image: %s", filename.c_str()); ImGui::Separator();
            GLuint texID = GetImageThumbnail(activeAsset); if (texID != 0) ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(200.0f, 200.0f));
        }
        else if (ext == ".bhmat") {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Material Settings: %s", filename.c_str()); ImGui::Separator(); ImGui::Spacing();

            // Если пути текстур пустые, но в материале они есть, значит мы пропустили клик. Вызываем загрузку принудительно (страховка)
            DrawTextureProperty("Albedo Map", editAlbedo, sizeof(editAlbedo)); ImGui::Spacing();
            DrawTextureProperty("Normal Map", editNormal, sizeof(editNormal)); ImGui::Spacing();
            DrawTextureProperty("Height Map", editHeight, sizeof(editHeight)); ImGui::Spacing();
            DrawTextureProperty("Metallic Map", editMetallic, sizeof(editMetallic)); ImGui::Spacing();
            DrawTextureProperty("Roughness Map", editRoughness, sizeof(editRoughness)); ImGui::Spacing();
            DrawTextureProperty("AO Map", editAO, sizeof(editAO));
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (ImGui::Button("💾 SAVE MATERIAL", ImVec2(-1, 40))) {
                std::ifstream fileIn(activeAsset); json j;
                if (fileIn.is_open()) { try { fileIn >> j; } catch (...) {} fileIn.close(); }

                // Записываем новые пути
                j["textures"]["albedo"] = editAlbedo; j["textures"]["normal"] = editNormal;
                j["textures"]["height"] = editHeight; j["textures"]["ao"] = editAO;
                j["textures"]["metallic"] = editMetallic; j["textures"]["roughness"] = editRoughness;

                std::ofstream fileOut(activeAsset); fileOut << j.dump(4); fileOut.close();

                Material* activeMat = nullptr;
                for (auto& pair : Serializer::loadedMaterials) { if (fs::path(pair.first) == fs::path(activeAsset)) { activeMat = pair.second; break; } }
                if (activeMat != nullptr) {
                    if (strlen(editAlbedo) > 0) activeMat->setAlbedo((projectDirectory.string() + "/" + editAlbedo).c_str());
                    if (strlen(editNormal) > 0) activeMat->setNormal((projectDirectory.string() + "/" + editNormal).c_str());
                    if (strlen(editHeight) > 0) activeMat->setHeight((projectDirectory.string() + "/" + editHeight).c_str());
                    if (strlen(editAO) > 0) activeMat->setAO((projectDirectory.string() + "/" + editAO).c_str());
                    if (strlen(editMetallic) > 0) activeMat->setMetallic((projectDirectory.string() + "/" + editMetallic).c_str());
                    if (strlen(editRoughness) > 0) activeMat->setRoughness((projectDirectory.string() + "/" + editRoughness).c_str());
                }
            }
        }
        else ImGui::Text("File: %s", filename.c_str());
        ImGui::End();
    }

    bool DrawAssetPicker(const char* label, std::string& outPath, const std::vector<std::string>& extensions) {
        bool changed = false; ImGui::PushID(label); ImGui::Text("%s", label); ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", outPath.empty() ? "None" : outPath.c_str());
        if (ImGui::Button("Select Asset...")) ImGui::OpenPopup("AssetPickerPopup"); ImGui::EndGroup();

        if (ImGui::BeginPopup("AssetPickerPopup")) {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Available Assets:"); ImGui::Separator();
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string(); bool match = false;
                    for (const auto& e : extensions) { if (ext == e) match = true; }
                    if (match) {
                        std::string relPath = fs::relative(entry.path(), projectDirectory).string(); std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        if (ImGui::Selectable(relPath.c_str())) { outPath = relPath; changed = true; }
                    }
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID(); return changed;
    }

    bool TestRayOBB(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::mat4 modelMatrix, float& tOutput) {
        glm::mat4 invModel = glm::inverse(modelMatrix);
        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f)); glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));
        float tMin = -100000.0f; float tMax = 100000.0f; glm::vec3 boxMin = glm::vec3(-1.0f); glm::vec3 boxMax = glm::vec3(1.0f);
        for (int i = 0; i < 3; i++) {
            float invD = 1.0f / localDir[i]; float t1 = (boxMin[i] - localOrigin[i]) * invD; float t2 = (boxMax[i] - localOrigin[i]) * invD;
            if (invD < 0.0f) std::swap(t1, t2); tMin = t1 > tMin ? t1 : tMin; tMax = t2 < tMax ? t2 : tMax;
            if (tMin > tMax) return false;
        }
        tOutput = tMin; return tMax > 0;
    }

    glm::vec3 GetMouseRay(Window& window, Camera& camera) {
        double mouseX, mouseY; glfwGetCursorPos(window.window, &mouseX, &mouseY);
        float x = (2.0f * (float)mouseX) / window.width - 1.0f; float y = 1.0f - (2.0f * (float)mouseY) / window.height;
        glm::mat4 invProj = glm::inverse(camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f)); glm::mat4 invView = glm::inverse(glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up));
        glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f); glm::vec4 rayEye = invProj * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f); return glm::normalize(glm::vec3(invView * rayEye));
    }

    void DrawSceneInspector(std::vector<GameObject>& Objects) {
        if (!showInspector) return;
        ImGui::Begin("Scene Inspector", &showInspector);
        if (Objects.empty() || selectedObjectIndex < 0 || selectedObjectIndex >= Objects.size()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an object in the scene"); ImGui::End(); return;
        }

        GameObject& obj = Objects[selectedObjectIndex];
        char nameBuf[128]; strcpy_s(nameBuf, sizeof(nameBuf), obj.name.c_str());
        ImGui::PushItemWidth(-1); if (ImGui::InputText("##ObjectName", nameBuf, sizeof(nameBuf))) obj.name = nameBuf; ImGui::PopItemWidth(); ImGui::Spacing();

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Static", &obj.isStatic); bool transformChanged = false;
            glm::vec3 pos = obj.transform.position; glm::vec3 rot = obj.transform.rotation; glm::vec3 scl = obj.transform.scale;

            // Если начали тянуть за слайдер - сохраняем шаг для Undo!
            if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.1f)) transformChanged = true;
            if (ImGui::IsItemActivated()) SaveState(Objects);

            if (ImGui::DragFloat3("Rotation", glm::value_ptr(rot), 1.0f)) transformChanged = true;
            if (ImGui::IsItemActivated()) SaveState(Objects);

            if (ImGui::DragFloat3("Scale", glm::value_ptr(scl), 0.05f)) transformChanged = true;
            if (ImGui::IsItemActivated()) SaveState(Objects);

            if (transformChanged) {
                obj.transform.position = pos; obj.transform.rotation = rot; obj.transform.scale = scl; obj.transform.updatematrix = true;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(obj.transform.position), glm::value_ptr(obj.transform.rotation), glm::value_ptr(obj.transform.scale), glm::value_ptr(model));
            }
        }

        if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (DrawAssetPicker("Model", obj.modelPath, { ".fbx", ".obj" })) {
                std::string fullModelPath = projectDirectory.string() + "/" + obj.modelPath;
                if (Serializer::loadedModels.find(fullModelPath) == Serializer::loadedModels.end()) Serializer::loadedModels[fullModelPath] = new Model(fullModelPath);
                Model* newModel = Serializer::loadedModels[fullModelPath]; obj.renderer.subMeshes.clear();
                for (int i = 0; i < newModel->meshes.size(); i++) {
                    Material* mat = (i < obj.materialPaths.size()) ? Serializer::LoadMaterial(projectDirectory.string() + "/" + obj.materialPaths[i], projectDirectory.string()) : new Material();
                    obj.renderer.AddSubMesh(&newModel->meshes[i], mat);
                }
            }
        }

        if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (obj.materialPaths.size() != obj.renderer.subMeshes.size()) obj.materialPaths.resize(obj.renderer.subMeshes.size(), "");
            for (int i = 0; i < obj.renderer.subMeshes.size(); i++) {
                char slotName[32]; sprintf_s(slotName, "Material %d", i);
                if (DrawAssetPicker(slotName, obj.materialPaths[i], { ".bhmat" })) {
                    std::string fullMatPath = projectDirectory.string() + "/" + obj.materialPaths[i];
                    Material* newMat = Serializer::LoadMaterial(fullMatPath, projectDirectory.string()); obj.renderer.subMeshes[i].material = newMat;
                }
            }
        }

        if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Checkbox("Visible", &obj.isVisible); ImGui::Checkbox("Cast Shadow", &obj.castShadow); }

        if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Light", &obj.light.enable);
            if (obj.light.enable) {
                const char* lightTypes[] = { "Directional", "Point", "Spot", "Rect", "Sky" }; int currentType = (int)obj.light.type;
                if (ImGui::Combo("Type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes))) obj.light.type = (LightType)currentType;
                const char* mobilityTypes[] = { "Static", "Movable" }; int currentMob = (int)obj.light.mobility;
                if (ImGui::Combo("Mobility", &currentMob, mobilityTypes, IM_ARRAYSIZE(mobilityTypes))) {
                    obj.light.mobility = (LightMobility)currentMob; if (obj.light.mobility == LightMobility::Static) obj.light.needsShadowUpdate = true;
                }
                ImGui::Separator(); ImGui::ColorEdit3("Color", glm::value_ptr(obj.light.color)); ImGui::DragFloat("Intensity", &obj.light.intensity, 0.1f, 0.0f, 1000.0f);
                if (obj.light.type == LightType::Point || obj.light.type == LightType::Spot) ImGui::DragFloat("Radius", &obj.light.radius, 0.5f, 0.1f, 500.0f);
                if (obj.light.type == LightType::Spot) {
                    ImGui::DragFloat("Inner Angle", &obj.light.innerCone, 0.5f, 0.0f, obj.light.outerCone); ImGui::DragFloat("Outer Angle", &obj.light.outerCone, 0.5f, obj.light.innerCone, 90.0f);
                }
                ImGui::Separator(); ImGui::Checkbox("Cast Shadows", &obj.light.castShadows);
            }
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("💾 SAVE SCENE", ImVec2(-1, 40))) Serializer::SaveScene(projectDirectory.string() + "/level1.bhscene", Objects);
        ImGui::End();
    }

    void DrawSceneOutliner(std::vector<GameObject>& Objects, ImGuiIO& io) {
        if (!showOutliner) return;
        ImGui::Begin("Scene Outliner", &showOutliner);
        ImGui::TextColored(ImVec4(0.68f, 0.45f, 0.95f, 1.0f), "FPS: %.1f", io.Framerate); ImGui::Separator();
        for (int i = 0; i < Objects.size(); i++) {
            char buf[128]; std::string objName = Objects[i].name.empty() ? "GameObject" : Objects[i].name;
            sprintf_s(buf, sizeof(buf), " %s ##%d", objName.c_str(), i);
            if (ImGui::Selectable(buf, selectedObjectIndex == i)) {
                selectedObjectIndex = i;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(Objects[selectedObjectIndex].transform.position), glm::value_ptr(Objects[selectedObjectIndex].transform.rotation), glm::value_ptr(Objects[selectedObjectIndex].transform.scale), glm::value_ptr(model));
            }
        }
        ImGui::End();
    }

    void Draw(Window& window, Camera& camera, std::vector<GameObject>& Objects, Render& render) {
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        // 1. Верхнее меню
        DrawMainMenuBar(Objects);

        // 2. ГЛОБАЛЬНЫЕ ГОРЯЧИЕ КЛАВИШИ UNDO / REDO
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) { Undo(Objects); }
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) { Redo(Objects); }

        // 3. DockSpace
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize); ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
        ImGui::Begin("MainDockSpace_Window", nullptr, window_flags); ImGui::PopStyleVar(3);
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        static bool first_time = true;
        if (first_time || resetLayout) {
            first_time = false; resetLayout = false;
            ImGui::DockBuilderRemoveNode(dockspace_id); ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace); ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
            auto dock_main = dockspace_id;
            auto dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.15f, nullptr, &dock_main);
            auto dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25f, nullptr, &dock_main);
            auto dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);
            ImGui::DockBuilderDockWindow("Scene Outliner", dock_left); ImGui::DockBuilderDockWindow("Content Browser", dock_bottom);
            ImGui::DockBuilderDockWindow("Scene Inspector", dock_right); ImGui::DockBuilderDockWindow("Properties", dock_right);
            ImGui::DockBuilderFinish(dockspace_id);
            showOutliner = true; showInspector = true; showProperties = true; showContentBrowser = true;
        }
        ImGui::End();

        // 4. Использование ГИЗМО
        ImGuizmo::BeginFrame(); ImGuizmo::SetOrthographic(false); ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList()); ImGuizmo::SetRect(0, 0, (float)window.width, (float)window.height);
        static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE; static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD; static bool snapEnabled = false; static float snapValue[3] = { 1.0f, 1.0f, 1.0f };
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) currentGizmoOperation = ImGuizmo::TRANSLATE; if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation = ImGuizmo::ROTATE; if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::SCALE;

        glm::mat4 view = glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up); glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);
        if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && !ImGuizmo::IsOver()) {
            glm::vec3 rayOrigin = camera.Position; glm::vec3 rayDir = GetMouseRay(window, camera);
            float closestT = 1000.0f; int hitIndex = -1;
            for (int i = 0; i < Objects.size(); i++) {
                if (Objects[i].renderer.subMeshes.size() < 1) continue; float t;
                if (TestRayOBB(rayOrigin, rayDir, Objects[i].transform.matrix, t)) { if (t < closestT) { closestT = t; hitIndex = i; } }
            }
            if (hitIndex != -1) {
                selectedObjectIndex = hitIndex;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(Objects[selectedObjectIndex].transform.position), glm::value_ptr(Objects[selectedObjectIndex].transform.rotation), glm::value_ptr(Objects[selectedObjectIndex].transform.scale), glm::value_ptr(model));
            }
        }

        if (Objects.size() > 0 && selectedObjectIndex < Objects.size() && !ImGuizmo::IsUsing()) {
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(Objects[selectedObjectIndex].transform.position), glm::value_ptr(Objects[selectedObjectIndex].transform.rotation), glm::value_ptr(Objects[selectedObjectIndex].transform.scale), glm::value_ptr(model));
        }

        if (snapEnabled) ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), currentGizmoOperation, currentGizmoMode, glm::value_ptr(model), nullptr, snapValue);
        else ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), currentGizmoOperation, currentGizmoMode, glm::value_ptr(model));

        // --- ЛОГИКА СОХРАНЕНИЯ ДЛЯ UNDO ПРИ ИСПОЛЬЗОВАНИИ ГИЗМО ---
        bool isUsingGizmo = ImGuizmo::IsUsing();
        if (isUsingGizmo && !wasUsingGizmo) {
            SaveState(Objects); // Пользователь только что начал тянуть объект
        }
        wasUsingGizmo = isUsingGizmo;

        if (!io.WantCaptureMouse && !isUsingGizmo) camera.Inputs(window.window);

        if (isUsingGizmo && Objects.size() > 0 && selectedObjectIndex < Objects.size()) {
            glm::vec3 translation, rotation, scale; ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), glm::value_ptr(translation), glm::value_ptr(rotation), glm::value_ptr(scale));
            Objects[selectedObjectIndex].transform.position = translation; Objects[selectedObjectIndex].transform.rotation = rotation; Objects[selectedObjectIndex].transform.scale = scale; Objects[selectedObjectIndex].transform.updatematrix = true;
        }

        DrawSceneOutliner(Objects, io); DrawSceneInspector(Objects); DrawContentBrowser(); DrawPropertiesWindow();
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
};
#endif