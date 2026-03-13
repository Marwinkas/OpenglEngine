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
#include "Components.h" // НАШ НОВЫЙ ФАЙЛ С КОМПОНЕНТАМИ!
#include "Camera.h"
#include "Render.h"
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>
#include "stb_image.h" 
#include <algorithm> 
#include <windows.h>
#include <shellapi.h>
#include <memory>
#include <TextureImporter.h>
#include <ModelImporter.h>

class Render;
using json = nlohmann::json;
namespace fs = std::filesystem;

class UI {
public:
    entt::entity selectedEntity = entt::null;
    glm::mat4 model = glm::mat4(1.0f);

    fs::path projectDirectory;
    fs::path ExeDirectory;
    fs::path currentDirectory;
    std::vector<fs::path> dirHistory;
    int dirHistoryIndex = -1;
    std::vector<std::string> selectedAssets;
    int lastClickedIndex = -1;
    char searchBuffer[256] = "";
    std::vector<std::string> clipboardPaths;
    bool isCut = false;
    std::string renamingPath = "";
    char inlineRenameBuf[256] = "";
    bool focusRename = false;
    char editAlbedo[256] = ""; char editNormal[256] = ""; char editMetallic[256] = "";
    char editRoughness[256] = ""; char editHeight[256] = ""; char editAO[256] = "";
    std::unordered_map<std::string, GLuint> imageThumbnails;
    bool showOutliner = true; bool showInspector = true;
    bool showProperties = true; bool showContentBrowser = true;
    bool resetLayout = false; bool showAboutModal = false;

    // --- СИСТЕМА СОХРАНЕНИЯ СОСТОЯНИЙ (ДЛЯ ENTT) ---
    struct SceneSnapshot {
        std::shared_ptr<entt::registry> regCopy;
        entt::entity selectedEntity;
    };
    std::vector<SceneSnapshot> undoStack;
    std::vector<SceneSnapshot> redoStack;
    bool wasUsingGizmo = false;

    // Хелпер для полного копирования реестра (нужен для Undo/Redo)
    void CopyRegistry(entt::registry& src, entt::registry& dst) {
        dst.clear();
        // Используем безопасный обход по TagComponent
        src.view<TagComponent>().each([&](entt::entity entity, TagComponent& tag) {
            entt::entity newEnt = dst.create(entity); // Создаем с тем же ID
            dst.emplace<TagComponent>(newEnt, tag);
            if (src.all_of<TransformComponent>(entity)) dst.emplace<TransformComponent>(newEnt, src.get<TransformComponent>(entity));
            if (src.all_of<MeshComponent>(entity)) dst.emplace<MeshComponent>(newEnt, src.get<MeshComponent>(entity));
            if (src.all_of<LightComponent>(entity)) dst.emplace<LightComponent>(newEnt, src.get<LightComponent>(entity));
            if (src.all_of<HierarchyComponent>(entity)) dst.emplace<HierarchyComponent>(newEnt, src.get<HierarchyComponent>(entity));
            if (src.all_of<PhysicsComponent>(entity)) dst.emplace<PhysicsComponent>(newEnt, src.get<PhysicsComponent>(entity));
            });
    }

    void SaveState(entt::registry& registry) {
        auto snapReg = std::make_shared<entt::registry>();
        CopyRegistry(registry, *snapReg);
        undoStack.push_back({ snapReg, selectedEntity });
        redoStack.clear();
        if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
    }

    void Undo(entt::registry& registry) {
        if (undoStack.empty()) return;
        auto snapReg = std::make_shared<entt::registry>();
        CopyRegistry(registry, *snapReg);
        redoStack.push_back({ snapReg, selectedEntity });

        SceneSnapshot snap = undoStack.back();
        undoStack.pop_back();
        CopyRegistry(*snap.regCopy, registry);
        selectedEntity = snap.selectedEntity;

        if (registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity)) {
            auto& t = registry.get<TransformComponent>(selectedEntity).transform;
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
        }
    }

    void Redo(entt::registry& registry) {
        if (redoStack.empty()) return;
        auto snapReg = std::make_shared<entt::registry>();
        CopyRegistry(registry, *snapReg);
        undoStack.push_back({ snapReg, selectedEntity });

        SceneSnapshot snap = redoStack.back();
        redoStack.pop_back();
        CopyRegistry(*snap.regCopy, registry);
        selectedEntity = snap.selectedEntity;

        if (registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity)) {
            auto& t = registry.get<TransformComponent>(selectedEntity).transform;
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
        }
    }

    // --- ИЕРАРХИЯ И УДАЛЕНИЕ ---
    entt::entity CloneHierarchy(entt::registry& registry, entt::entity source, entt::entity newParent) {
        entt::entity copy = registry.create();

        if (registry.all_of<TagComponent>(source)) {
            auto tag = registry.get<TagComponent>(source);
            tag.name += " (Copy)";
            registry.emplace<TagComponent>(copy, tag);
        }
        if (registry.all_of<TransformComponent>(source)) registry.emplace<TransformComponent>(copy, registry.get<TransformComponent>(source));
        if (registry.all_of<MeshComponent>(source)) registry.emplace<MeshComponent>(copy, registry.get<MeshComponent>(source));
        if (registry.all_of<LightComponent>(source)) registry.emplace<LightComponent>(copy, registry.get<LightComponent>(source));
        if (registry.all_of<PhysicsComponent>(source)) {
            auto phys = registry.get<PhysicsComponent>(source);
            phys.pxActor = nullptr; // Сброс физики
            phys.rebuildPhysics = true;
            registry.emplace<PhysicsComponent>(copy, phys);
        }

        auto& hc = registry.emplace<HierarchyComponent>(copy);
        hc.parent = newParent;

        if (registry.all_of<HierarchyComponent>(source)) {
            for (entt::entity child : registry.get<HierarchyComponent>(source).children) {
                entt::entity newChild = CloneHierarchy(registry, child, copy);
                hc.children.push_back(newChild);
            }
        }
        return copy;
    }

    void DeleteEntityRecursive(entt::registry& registry, entt::entity target) {
        if (registry.all_of<HierarchyComponent>(target)) {
            // Копируем массив детей, так как мы будем их удалять
            auto children = registry.get<HierarchyComponent>(target).children;
            for (entt::entity child : children) {
                DeleteEntityRecursive(registry, child);
            }
        }
        if (selectedEntity == target) selectedEntity = entt::null;
        registry.destroy(target);
    }

    void DeleteGameObject(entt::registry& registry, entt::entity target) {
        SaveState(registry);

        if (registry.all_of<HierarchyComponent>(target)) {
            entt::entity parent = registry.get<HierarchyComponent>(target).parent;
            if (parent != entt::null && registry.all_of<HierarchyComponent>(parent)) {
                auto& siblings = registry.get<HierarchyComponent>(parent).children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), target), siblings.end());
            }
        }
        DeleteEntityRecursive(registry, target);
    }

    bool IsDescendant(entt::registry& registry, entt::entity potentialChild, entt::entity potentialParent) {
        entt::entity curr = potentialChild;
        while (curr != entt::null && registry.all_of<HierarchyComponent>(curr)) {
            if (curr == potentialParent) return true;
            curr = registry.get<HierarchyComponent>(curr).parent;
        }
        return false;
    }

    // --- ПОСТ ПРОЦЕССИНГ ---
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
        float fogColor[3] = { 0.5f, 0.6f, 0.7f };         float inscatterColor[3] = { 1.0f, 0.8f, 0.5f };         float inscatterPower = 8.0f;             float inscatterIntensity = 1.0f;
        bool enableContactShadows = true; float contactShadowLength = 0.05f; float contactShadowThickness = 0.1f; int contactShadowSteps = 16;
    } ppSettings;

    void SetupBurnhopeTheme() {
        ImGuiStyle& style = ImGui::GetStyle(); ImVec4* colors = style.Colors;
        style.WindowRounding = 6.0f; style.ChildRounding = 4.0f; style.FrameRounding = 4.0f; style.PopupRounding = 6.0f; style.TabRounding = 6.0f;
        style.WindowBorderSize = 1.0f; style.FrameBorderSize = 0.0f; style.PopupBorderSize = 1.0f; style.ItemSpacing = ImVec2(8, 6);
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.08f, 0.11f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.10f, 0.13f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.22f, 0.50f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.22f, 0.50f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.48f, 0.30f, 0.68f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.16f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.20f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.25f, 0.55f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.12f, 0.17f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.20f, 0.38f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.68f, 0.45f, 0.95f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.35f, 0.85f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.70f, 0.50f, 1.00f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.08f, 0.11f, 1.00f);
    }

    UI(Window& window, const std::string& projectPath, const std::string& exePath) {
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuizmo::Enable(true);
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImFontConfig font_cfg; font_cfg.OversampleH = 2; font_cfg.OversampleV = 2;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
        SetupBurnhopeTheme();
        ImGui_ImplGlfw_InitForOpenGL(window.window, true); ImGui_ImplOpenGL3_Init("#version 330");
        projectDirectory = projectPath; currentDirectory = projectPath; ExeDirectory = exePath;
        dirHistory.push_back(currentDirectory); dirHistoryIndex = 0;
        LoadPostProcessSettings();
    }

    // --- МЕЛКИЕ ХЕЛПЕРЫ ДЛЯ ФАЙЛОВ ---
    std::string TruncateText(const std::string& text, float maxWidth) {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
        std::string res = text;
        while (res.length() > 0 && ImGui::CalcTextSize((res + "...").c_str()).x > maxWidth) res.pop_back();
        return res + "...";
    }
    std::string GetFileTypeName(const std::string& ext, bool isDir) {
        if (isDir) return "Folder";
        if (ext == ".bhmat") return "Material";
        if (ext == ".bhtex") return "Texture";
        if (ext == ".bhscene") return "Scene";
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return "Image";
        if (ext == ".bhtex") return "Texture";
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
    bool IsSelected(const std::string& path) { return std::find(selectedAssets.begin(), selectedAssets.end(), path) != selectedAssets.end(); }
    std::string GetPrimarySelection() { return selectedAssets.empty() ? "" : selectedAssets.back(); }
    void NavigateTo(const fs::path& target) {
        if (currentDirectory == target) return;
        if (dirHistoryIndex < dirHistory.size() - 1) dirHistory.erase(dirHistory.begin() + dirHistoryIndex + 1, dirHistory.end());
        dirHistory.push_back(target); dirHistoryIndex++; currentDirectory = target;
        selectedAssets.clear(); renamingPath = ""; lastClickedIndex = -1;
    }
    void StartRename(const std::string& path) {
        renamingPath = path; strcpy_s(inlineRenameBuf, sizeof(inlineRenameBuf), fs::path(path).stem().string().c_str()); focusRename = true;
    }
    void ApplyRename() {
        if (!renamingPath.empty() && strlen(inlineRenameBuf) > 0) {
            fs::path oldP(renamingPath); std::string newName = std::string(inlineRenameBuf) + oldP.extension().string();
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
            while (fs::exists(dst)) { dst = targetDir / (src.stem().string() + " " + std::to_string(copyCount) + src.extension().string()); copyCount++; }
            if (isCut) fs::rename(src, dst); else fs::copy(src, dst, fs::copy_options::recursive);
        }
        if (isCut) { clipboardPaths.clear(); isCut = false; }
    }

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

    GLuint GetImageThumbnail(const std::string& fullPath) {
        if (imageThumbnails.find(fullPath) != imageThumbnails.end()) return imageThumbnails[fullPath];
        int w, h, c; unsigned char* data = stbi_load(fullPath.c_str(), &w, &h, &c, 4);
        if (!data) { imageThumbnails[fullPath] = 0; return 0; }
        GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data); imageThumbnails[fullPath] = id; return id;
    }
    GLuint GetFileIcon(const std::string& ext, bool isDir) {
        if (isDir) return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_folder.png");
        if (ext == ".bhmat") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_material.png");
        if (ext == ".bhscene") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_scene.png");
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_model.png");
        return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_file.png");
    }

    // --- ОТРИСОВКА ОКНА: OUTLINER ---
    void DrawOutlinerNode(entt::registry& registry, entt::entity entity) {
        if (!registry.valid(entity)) return;
        auto& tag = registry.get<TagComponent>(entity);

        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap;
        if (selectedEntity == entity) nodeFlags |= ImGuiTreeNodeFlags_Selected;

        auto* hc = registry.try_get<HierarchyComponent>(entity);
        if (!hc || hc->children.empty()) nodeFlags |= ImGuiTreeNodeFlags_Leaf;

        bool nodeOpen = ImGui::TreeNodeEx((void*)(uintptr_t)entity, nodeFlags, tag.name.c_str());

        if (ImGui::IsItemClicked()) {
            selectedEntity = entity;
            if (registry.all_of<TransformComponent>(entity)) {
                auto& t = registry.get<TransformComponent>(entity).transform;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
            }
        }

        if (ImGui::BeginPopupContextItem()) {
            selectedEntity = entity;
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                SaveState(registry);
                entt::entity parent = hc ? hc->parent : entt::null;
                entt::entity newEnt = CloneHierarchy(registry, entity, parent);
                if (parent != entt::null) registry.get<HierarchyComponent>(parent).children.push_back(newEnt);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Create Child...")) {
                if (ImGui::MenuItem("Empty Object")) {
                    SaveState(registry);
                    entt::entity newE = registry.create();
                    registry.emplace<TagComponent>(newE, "Empty");
                    registry.emplace<TransformComponent>(newE);
                    registry.emplace<HierarchyComponent>(newE).parent = entity;
                    registry.get_or_emplace<HierarchyComponent>(entity).children.push_back(newE);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) DeleteGameObject(registry, entity);
            ImGui::EndPopup();
        }

        float iconSize = 16.0f;
        float iconX = ImGui::GetWindowContentRegionMax().x - iconSize - 5.0f;
        auto DrawIcon = [&](const std::string& fileName) {
            GLuint texID = GetImageThumbnail(ExeDirectory.string() + "/Resources/" + fileName);
            if (texID != 0) { ImGui::SameLine(iconX); ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(iconSize, iconSize)); iconX -= (iconSize + 4.0f); }
            };

        if (registry.all_of<LightComponent>(entity)) DrawIcon("icon_light.png");
        if (registry.all_of<MeshComponent>(entity))  DrawIcon("icon_model.png");
        if (!registry.all_of<MeshComponent>(entity) && !registry.all_of<LightComponent>(entity)) DrawIcon("icon_folder.png");

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("OUTLINER_NODE", &entity, sizeof(entt::entity));
            ImGui::Text("Move %s", tag.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE")) {
                entt::entity dragged = *(const entt::entity*)payload->Data;
                if (dragged != entity && !IsDescendant(registry, entity, dragged)) {
                    SaveState(registry);
                    auto& draggedHc = registry.get_or_emplace<HierarchyComponent>(dragged);
                    if (draggedHc.parent != entt::null) {
                        auto& oldParentHc = registry.get<HierarchyComponent>(draggedHc.parent);
                        oldParentHc.children.erase(std::remove(oldParentHc.children.begin(), oldParentHc.children.end(), dragged), oldParentHc.children.end());
                    }
                    draggedHc.parent = entity;
                    registry.get_or_emplace<HierarchyComponent>(entity).children.push_back(dragged);
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (nodeOpen) {
            if (hc) {
                // Копия, чтобы безопасно итерироваться, если массив изменится
                auto children = hc->children;
                for (entt::entity child : children) DrawOutlinerNode(registry, child);
            }
            ImGui::TreePop();
        }
    }

    void DrawSceneOutliner(entt::registry& registry, ImGuiIO& io) {
        if (!showOutliner) return;
        ImGui::Begin("Scene Outliner", &showOutliner);

        if (ImGui::Button("+ Add", ImVec2(60, 25))) ImGui::OpenPopup("GlobalCreateMenu");
        ImGui::SameLine();
        if (ImGui::Button("Unparent", ImVec2(80, 25)) && selectedEntity != entt::null) {
            SaveState(registry);
            auto* hc = registry.try_get<HierarchyComponent>(selectedEntity);
            if (hc && hc->parent != entt::null) {
                auto& parentHc = registry.get<HierarchyComponent>(hc->parent);
                parentHc.children.erase(std::remove(parentHc.children.begin(), parentHc.children.end(), selectedEntity), parentHc.children.end());
                hc->parent = entt::null;
            }
        }

        if (ImGui::BeginPopup("GlobalCreateMenu")) {
            if (ImGui::MenuItem("Empty Object")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Empty"); registry.emplace<TransformComponent>(e); }
            if (ImGui::MenuItem("Mesh Object")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Mesh"); registry.emplace<TransformComponent>(e); registry.emplace<MeshComponent>(e); }
            if (ImGui::MenuItem("Light Source")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Light"); registry.emplace<TransformComponent>(e); registry.emplace<LightComponent>(e); }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        ImGui::BeginChild("OutlinerList", ImVec2(0, -20));
        registry.view<TagComponent>().each([&](entt::entity entity, TagComponent& tag) {
            auto* hc = registry.try_get<HierarchyComponent>(entity);
            if (!hc || hc->parent == entt::null) {
                DrawOutlinerNode(registry, entity);
            }
            });

        if (ImGui::BeginPopupContextWindow("EmptySpaceMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("Create...")) {
                if (ImGui::MenuItem("Empty Object")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Empty"); registry.emplace<TransformComponent>(e); }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE")) {
                entt::entity dragged = *(const entt::entity*)payload->Data;
                SaveState(registry);
                auto* hc = registry.try_get<HierarchyComponent>(dragged);
                if (hc && hc->parent != entt::null) {
                    auto& parentHc = registry.get<HierarchyComponent>(hc->parent);
                    parentHc.children.erase(std::remove(parentHc.children.begin(), parentHc.children.end(), dragged), parentHc.children.end());
                    hc->parent = entt::null;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();
        ImGui::End();
    }

  

    void DrawSceneInspector(entt::registry& registry, Render& render) {
        if (!showInspector) return;
        ImGui::Begin("Scene Inspector", &showInspector);

        if (selectedEntity == entt::null || !registry.valid(selectedEntity)) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an object in the scene"); ImGui::End(); return;
        }

        auto& tag = registry.get<TagComponent>(selectedEntity);
        char nameBuf[128]; strcpy_s(nameBuf, sizeof(nameBuf), tag.name.c_str());
        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##ObjectName", nameBuf, sizeof(nameBuf))) { tag.name = nameBuf; }
        ImGui::PopItemWidth(); ImGui::Spacing();

        if (registry.all_of<TransformComponent>(selectedEntity)) {
            auto& tComp = registry.get<TransformComponent>(selectedEntity);
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool transformChanged = false;
                if (ImGui::DragFloat3("Position", glm::value_ptr(tComp.transform.position), 0.1f)) transformChanged = true;
                if (ImGui::IsItemActivated()) SaveState(registry);
                if (ImGui::DragFloat3("Rotation", glm::value_ptr(tComp.transform.rotation), 1.0f)) transformChanged = true;
                if (ImGui::IsItemActivated()) SaveState(registry);
                if (ImGui::DragFloat3("Scale", glm::value_ptr(tComp.transform.scale), 0.05f)) transformChanged = true;
                if (ImGui::IsItemActivated()) SaveState(registry);

                if (transformChanged) {
                    tComp.transform.updatematrix = true;
                    if (registry.all_of<PhysicsComponent>(selectedEntity)) registry.get<PhysicsComponent>(selectedEntity).updatePhysicsTransform = true;
                    ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(tComp.transform.position), glm::value_ptr(tComp.transform.rotation), glm::value_ptr(tComp.transform.scale), glm::value_ptr(model));
                }
            }
        }

        if (registry.all_of<MeshComponent>(selectedEntity)) {
            auto& meshComp = registry.get<MeshComponent>(selectedEntity);
            bool removeMesh = false;
            if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap)) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                if (ImGui::Button("X##RM_MESH")) removeMesh = true;

                ImGui::Checkbox("Static", &meshComp.isStatic); ImGui::SameLine();
                ImGui::Checkbox("Visible", &meshComp.isVisible); ImGui::SameLine();
                ImGui::Checkbox("Cast Shadow", &meshComp.castShadow);

                if (DrawAssetPicker("Model", meshComp.modelPath, { ".bhmesh" })) {
                    SaveState(registry);
                    render.isSceneDirty = true;
                    fs::path absProjectDir = fs::absolute(projectDirectory);
                    std::string fullModelPath = (absProjectDir / meshComp.modelPath).string();

                    if (Serializer::loadedModels.find(fullModelPath) == Serializer::loadedModels.end()) {
                        Serializer::loadedModels[fullModelPath] = new Model(fullModelPath, projectDirectory.string());
                    }
                    Model* newModel = Serializer::loadedModels[fullModelPath];

                    if (newModel && !newModel->meshes.empty()) {
                        meshComp.materialPaths.clear();
                        for (const std::string& rawMatPath : newModel->loadedMaterialPaths) {
                            if (rawMatPath.empty()) meshComp.materialPaths.push_back("");
                            else {
                                fs::path p(rawMatPath);
                                if (p.is_absolute()) meshComp.materialPaths.push_back(fs::relative(p, absProjectDir).generic_string());
                                else meshComp.materialPaths.push_back(p.generic_string());
                            }
                        }

                        meshComp.renderer.subMeshes.clear();
                        for (int i = 0; i < newModel->meshes.size(); i++) {
                            std::string relMatPath = (i < meshComp.materialPaths.size()) ? meshComp.materialPaths[i] : "";
                            Material* mat = nullptr;
                            if (!relMatPath.empty()) {
                                std::string fullMatPath = (absProjectDir / relMatPath).string();
                                mat = Serializer::LoadMaterial(fullMatPath, projectDirectory.string());
                            }
                            if (mat == nullptr) mat = new Material();
                            meshComp.renderer.AddSubMesh(&newModel->meshes[i], mat);
                        }
                    }
                    if (registry.all_of<PhysicsComponent>(selectedEntity)) registry.get<PhysicsComponent>(selectedEntity).rebuildPhysics = true;
                    
                }

                if (!meshComp.renderer.subMeshes.empty()) {
                    ImGui::Separator(); ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.9f, 1.0f), "Material Slots:");
                    if (meshComp.materialPaths.size() != meshComp.renderer.subMeshes.size()) meshComp.materialPaths.resize(meshComp.renderer.subMeshes.size(), "");

                    for (int i = 0; i < meshComp.renderer.subMeshes.size(); i++) {
                        std::string mName = meshComp.renderer.subMeshes[i].mesh ? meshComp.renderer.subMeshes[i].mesh->name : "Mesh";
                        char slotName[512]; sprintf_s(slotName, "Slot [%d] %s", i, mName.c_str());

                        if (DrawAssetPicker(slotName, meshComp.materialPaths[i], { ".bhmat" })) {
                            SaveState(registry);
                            render.isSceneDirty = true;
                            std::string fullMatPath = (projectDirectory / meshComp.materialPaths[i]).string();
                            Material* newMat = Serializer::LoadMaterial(fullMatPath, projectDirectory.string());
                            if (newMat) meshComp.renderer.subMeshes[i].material = newMat;
                        }
                    }
                }
            }
            if (removeMesh) { SaveState(registry); registry.erase<MeshComponent>(selectedEntity); render.isSceneDirty = true;
            }
        }

        if (registry.all_of<LightComponent>(selectedEntity)) {
            auto& lComp = registry.get<LightComponent>(selectedEntity).light;
            bool removeLight = false;
            if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap)) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                if (ImGui::Button("X##RM_LIGHT")) removeLight = true;

                ImGui::Checkbox("Enable Light", &lComp.enable);
                const char* lightTypes[] = { "Directional", "Point", "Spot", "Rect", "Sky" }; int currentType = (int)lComp.type;
                if (ImGui::Combo("Type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes))) { SaveState(registry); lComp.type = (LightType)currentType; }

                const char* lightTypes2[] = { "Static", "Movable"}; int currentType2 = (int)lComp.mobility;
                if (ImGui::Combo("Type Move", &currentType2, lightTypes2, IM_ARRAYSIZE(lightTypes2))) { SaveState(registry); lComp.mobility = (LightMobility)currentType2; }


                ImGui::ColorEdit3("Color", glm::value_ptr(lComp.color));
                ImGui::DragFloat("Intensity", &lComp.intensity, 0.1f, 0.0f, 1000.0f);
                if (lComp.type == LightType::Point || lComp.type == LightType::Spot) ImGui::DragFloat("Radius", &lComp.radius, 0.5f, 0.1f, 500.0f);
                if (lComp.type == LightType::Spot) {
                    ImGui::DragFloat("Inner Angle", &lComp.innerCone, 0.5f, 0.0f, lComp.outerCone);
                    ImGui::DragFloat("Outer Angle", &lComp.outerCone, 0.5f, lComp.innerCone, 90.0f);
                }
                ImGui::Checkbox("Cast Shadows", &lComp.castShadows);
            }
            if (removeLight) { SaveState(registry); registry.erase<LightComponent>(selectedEntity); }
        }

        if (registry.all_of<PhysicsComponent>(selectedEntity)) {
            auto& phys = registry.get<PhysicsComponent>(selectedEntity);
            bool removePhysics = false;
            if (ImGui::CollapsingHeader("Physics Component", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap)) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                if (ImGui::Button("X##RM_PHYSICS")) removePhysics = true;

                bool rebuild = false;
                const char* rbTypes[] = { "Static", "Dynamic" }; int currentRbType = (int)phys.bodyType;
                if (ImGui::Combo("Body Type", &currentRbType, rbTypes, IM_ARRAYSIZE(rbTypes))) { phys.bodyType = (RigidBodyType)currentRbType; rebuild = true; }

                if (ImGui::DragFloat("Mass", &phys.mass, 0.1f, 0.0f, 1000.0f)) rebuild = true;
                if (ImGui::DragFloat("Friction", &phys.friction, 0.01f, 0.0f, 1.0f)) rebuild = true;
                if (ImGui::DragFloat("Restitution", &phys.restitution, 0.01f, 0.0f, 1.0f)) rebuild = true;

                ImGui::Separator();
                const char* colTypes[] = { "Box", "Sphere", "Plane" }; int currentColType = (int)phys.colliderType;
                if (ImGui::Combo("Shape", &currentColType, colTypes, IM_ARRAYSIZE(colTypes))) { phys.colliderType = (ColliderType)currentColType; rebuild = true; }

                if (phys.colliderType == ColliderType::Box) {
                    if (ImGui::DragFloat3("Extents", glm::value_ptr(phys.extents), 0.1f)) rebuild = true;
                }
                else if (phys.colliderType == ColliderType::Sphere) {
                    if (ImGui::DragFloat("Radius", &phys.radius, 0.1f)) rebuild = true;
                }

                if (rebuild) { SaveState(registry); phys.rebuildPhysics = true; }
            }
            if (removePhysics) { SaveState(registry); registry.erase<PhysicsComponent>(selectedEntity); }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Add Component", ImVec2(-1, 30))) ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!registry.all_of<MeshComponent>(selectedEntity) && ImGui::MenuItem("Mesh Renderer")) { SaveState(registry); registry.emplace<MeshComponent>(selectedEntity); }
            if (!registry.all_of<LightComponent>(selectedEntity) && ImGui::MenuItem("Light Component")) { SaveState(registry); registry.emplace<LightComponent>(selectedEntity); }
            if (!registry.all_of<PhysicsComponent>(selectedEntity) && ImGui::MenuItem("Physics Component")) { SaveState(registry); auto& p = registry.emplace<PhysicsComponent>(selectedEntity); p.rebuildPhysics = true; }
            ImGui::EndPopup();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // ВАЖНО: Тебе нужно будет обновить Serializer, чтобы он читал из EnTT, а не из вектора!
        // Пока можно просто выводить текст или закомментить функцию сохранения, пока не перепишем Serializer.
        if (ImGui::Button("💾 SAVE SCENE", ImVec2(-1, 40))) { std::cout << "Need to update Serializer for EnTT!\n"; }
        ImGui::End();
    }
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
    GLuint GetMaterialThumbnail(const std::string& matPath) {
        std::ifstream file(matPath); if (!file.is_open()) return 0;
        json j; try { file >> j; }
        catch (...) { return 0; }
        if (!j.contains("textures") || !j["textures"].contains("albedo")) return 0;
        return GetImageThumbnail(projectDirectory.string() + "/" + j["textures"]["albedo"].get<std::string>());
    }
  
                void DrawContentBrowser() {
        if (!showContentBrowser) return;
        ImGui::Begin("Content Browser", &showContentBrowser);
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
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button("All")) NavigateTo(projectDirectory);
        if (ImGui::BeginDragDropTarget()) {             if (ImGui::AcceptDragDropPayload("CB_ITEMS")) {
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
                ImGui::BeginChild("LeftTreePanel");
        DrawFolderTree(projectDirectory);
        ImGui::EndChild();
        ImGui::NextColumn();
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
            for (auto& entry : fs::directory_iterator(currentDirectory)) {
                items.push_back(entry);

                // --- АВТО-КОНВЕРТАЦИЯ ПРИ ОБНАРУЖЕНИИ ---
                // --- АВТО-КОНВЕРТАЦИЯ ПРИ ОБНАРУЖЕНИИ ---
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".fbx" || ext == ".obj") {
                        fs::path compressedPath = entry.path();
                        compressedPath.replace_extension(".bhmesh");

                        if (!fs::exists(compressedPath)) {
                            std::cout << "Detected new model, compiling: " << entry.path().filename() << "...\n";

                            if (ModelImporter::ImportModel(entry.path().string(), compressedPath.string(), projectDirectory.string())) {
                                try {
                                    fs::remove(entry.path());
                                    std::cout << "Deleted original model: " << entry.path().filename() << "\n";
                                }
                                catch (...) {}
                                continue;
                            }
                        }
                    }
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                        fs::path compressedPath = entry.path();
                        compressedPath.replace_extension(".bhtex");

                        if (!fs::exists(compressedPath)) {
                            std::cout << "Detected new texture, compiling: " << entry.path().filename() << "...\n";

                            // Если конвертация прошла успешно...
                            if (TextureImporter::ImportTexture(entry.path().string(), compressedPath.string())) {

                                // УДАЛЯЕМ СТАРУЮ КАРТИНКУ С ЖЕСТКОГО ДИСКА!
                                try {
                                    fs::remove(entry.path());
                                    std::cout << "Deleted original file: " << entry.path().filename() << "\n";
                                }
                                catch (const fs::filesystem_error& e) {
                                    std::cerr << "Cannot delete file: " << e.what() << "\n";
                                }

                                // Пропускаем добавление старого файла в UI, 
                                // так как мы его только что стерли.
                                continue;
                            }
                        }
                    }
                }
            }
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
                float padding = 16.0f;
        float thumbnailSize = 64.0f;
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
                        float colWidth = ImGui::GetColumnWidth();
            float offsetX = (colWidth - itemWidth) / 2.0f;
            if (offsetX < 0) offsetX = 0;             ImVec2 startPos = ImGui::GetCursorScreenPos();
                        ImVec2 itemPos = ImVec2(startPos.x + offsetX, startPos.y + padding / 2.0f);
            bool isSel = IsSelected(pathStr);
            bool isHovered = ImGui::IsMouseHoveringRect(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight));
                        if (isSel) ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(36, 112, 204, 150), 8.0f);
            else if (isHovered) ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(60, 70, 85, 120), 8.0f);
                        ImGui::SetCursorScreenPos(itemPos);
            ImGui::InvisibleButton("##hitbox", ImVec2(itemWidth, itemHeight));
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
                        if (ImGui::BeginPopupContextItem("ItemContext")) {
                if (!isSel) { selectedAssets.clear(); selectedAssets.push_back(pathStr); if (ext == ".bhmat") LoadMaterialToProperties(pathStr); }
                if (ImGui::MenuItem("Open")) { if (isDir) NavigateTo(path); } ImGui::Separator();
                if (ImGui::MenuItem("Cut", "Ctrl+X")) { clipboardPaths = selectedAssets; isCut = true; }
                if (ImGui::MenuItem("Copy", "Ctrl+C")) { clipboardPaths = selectedAssets; isCut = false; } ImGui::Separator();
                if (ImGui::MenuItem("Rename", "F2", false, selectedAssets.size() == 1)) { StartRename(pathStr); }
                if (ImGui::MenuItem("Delete", "Del")) { for (auto& p : selectedAssets) MoveToRecycleBin(p); selectedAssets.clear(); }
                ImGui::EndPopup();
            }
                        ImGui::SetCursorScreenPos(ImVec2(itemPos.x + (itemWidth - thumbnailSize) / 2.0f, itemPos.y + 6.0f));
            GLuint texID = 0;
            if (!isDir && (ext == ".png" || ext == ".jpg" || ext == ".jpeg")) {
                texID = GetImageThumbnail(pathStr);             }
            else {
                texID = GetFileIcon(ext, isDir);             }
            if (texID != 0) {
                ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(thumbnailSize, thumbnailSize));
            }
            else {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.22f, 0.25f, 1.0f));
                ImGui::Button(isDir ? "DIR" : "FILE", ImVec2(thumbnailSize, thumbnailSize));
                ImGui::PopStyleColor();
            }
                        if (renamingPath == pathStr) {
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + 4, itemPos.y + thumbnailSize + 10.0f));
                ImGui::SetNextItemWidth(itemWidth - 8);
                if (focusRename) { ImGui::SetKeyboardFocusHere(); focusRename = false; ImGui::SetScrollHereY(); }
                if (ImGui::InputText("##rename", inlineRenameBuf, sizeof(inlineRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) { ApplyRename(); }
                if (!ImGui::IsItemActive() && !focusRename && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()) { ApplyRename(); }
            }
            else {
                                std::string truncName = TruncateText(nameNoExt, itemWidth - 8.0f);
                float textOffset = (itemWidth - ImGui::CalcTextSize(truncName.c_str()).x) / 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + textOffset, itemPos.y + thumbnailSize + 10.0f));
                ImGui::Text("%s", truncName.c_str());
                                float typeOffset = (itemWidth - ImGui::CalcTextSize(typeStr.c_str()).x) / 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + typeOffset, itemPos.y + thumbnailSize + 26.0f));
                ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 1.0f), "%s", typeStr.c_str());
            }
            ImGui::NextColumn(); ImGui::PopID();
        }
        ImGui::Columns(1); ImGui::EndChild(); ImGui::Columns(1);
        ImGui::End();
    }
            void SavePostProcessSettings() {
        std::string path = projectDirectory.string() + "/postprocess.json";
        json j;
                j["enableSSAO"] = ppSettings.enableSSAO; j["ssaoRadius"] = ppSettings.ssaoRadius; j["ssaoBias"] = ppSettings.ssaoBias; j["ssaoIntensity"] = ppSettings.ssaoIntensity; j["ssaoPower"] = ppSettings.ssaoPower;
        j["enableSSGI"] = ppSettings.enableSSGI; j["ssgiRayCount"] = ppSettings.ssgiRayCount; j["ssgiStepSize"] = ppSettings.ssgiStepSize; j["ssgiThickness"] = ppSettings.ssgiThickness; j["blurRange"] = ppSettings.blurRange;
                j["autoExposure"] = ppSettings.autoExposure; j["manualExposure"] = ppSettings.manualExposure; j["exposureCompensation"] = ppSettings.exposureCompensation; j["minBrightness"] = ppSettings.minBrightness; j["maxBrightness"] = ppSettings.maxBrightness;
        j["contrast"] = ppSettings.contrast; j["saturation"] = ppSettings.saturation; j["temperature"] = ppSettings.temperature; j["gamma"] = ppSettings.gamma;
                j["enableVignette"] = ppSettings.enableVignette; j["vignetteIntensity"] = ppSettings.vignetteIntensity;
        j["enableChromaticAberration"] = ppSettings.enableChromaticAberration; j["caIntensity"] = ppSettings.caIntensity;
        j["enableBloom"] = ppSettings.enableBloom; j["bloomThreshold"] = ppSettings.bloomThreshold; j["bloomIntensity"] = ppSettings.bloomIntensity; j["bloomBlurIterations"] = ppSettings.bloomBlurIterations;
        j["enableLensFlares"] = ppSettings.enableLensFlares; j["flareIntensity"] = ppSettings.flareIntensity; j["ghostDispersal"] = ppSettings.ghostDispersal; j["ghosts"] = ppSettings.ghosts;
        j["enableGodRays"] = ppSettings.enableGodRays; j["godRaysIntensity"] = ppSettings.godRaysIntensity;
        j["enableFilmGrain"] = ppSettings.enableFilmGrain; j["grainIntensity"] = ppSettings.grainIntensity;
        j["enableSharpen"] = ppSettings.enableSharpen; j["sharpenIntensity"] = ppSettings.sharpenIntensity;
                j["enableDoF"] = ppSettings.enableDoF; j["focusDistance"] = ppSettings.focusDistance; j["focusRange"] = ppSettings.focusRange; j["bokehSize"] = ppSettings.bokehSize;
        j["enableMotionBlur"] = ppSettings.enableMotionBlur; j["mbStrength"] = ppSettings.mbStrength;
        j["enableFog"] = ppSettings.enableFog; j["fogDensity"] = ppSettings.fogDensity; j["fogHeightFalloff"] = ppSettings.fogHeightFalloff; j["fogBaseHeight"] = ppSettings.fogBaseHeight;
        j["inscatterPower"] = ppSettings.inscatterPower; j["inscatterIntensity"] = ppSettings.inscatterIntensity;
                j["fogColor"] = { ppSettings.fogColor[0], ppSettings.fogColor[1], ppSettings.fogColor[2] };
        j["inscatterColor"] = { ppSettings.inscatterColor[0], ppSettings.inscatterColor[1], ppSettings.inscatterColor[2] };
        std::ofstream file(path);
        file << j.dump(4);
    }
    void LoadPostProcessSettings() {
        std::string path = projectDirectory.string() + "/postprocess.json";
        std::ifstream file(path);
        if (!file.is_open()) { SavePostProcessSettings(); return; }
        json j; try { file >> j; }
        catch (...) { return; }
                auto loadFloat = [&](const char* key, float& val) { if (j.contains(key)) val = j[key]; };
        auto loadInt = [&](const char* key, int& val) { if (j.contains(key)) val = j[key]; };
        auto loadBool = [&](const char* key, bool& val) { if (j.contains(key)) val = j[key]; };
        loadBool("enableSSAO", ppSettings.enableSSAO); loadFloat("ssaoRadius", ppSettings.ssaoRadius); loadFloat("ssaoBias", ppSettings.ssaoBias); loadFloat("ssaoIntensity", ppSettings.ssaoIntensity); loadFloat("ssaoPower", ppSettings.ssaoPower);
        loadBool("enableSSGI", ppSettings.enableSSGI); loadInt("ssgiRayCount", ppSettings.ssgiRayCount); loadFloat("ssgiStepSize", ppSettings.ssgiStepSize); loadFloat("ssgiThickness", ppSettings.ssgiThickness); loadInt("blurRange", ppSettings.blurRange);
        loadBool("autoExposure", ppSettings.autoExposure); loadFloat("manualExposure", ppSettings.manualExposure); loadFloat("exposureCompensation", ppSettings.exposureCompensation); loadFloat("minBrightness", ppSettings.minBrightness); loadFloat("maxBrightness", ppSettings.maxBrightness);
        loadFloat("contrast", ppSettings.contrast); loadFloat("saturation", ppSettings.saturation); loadFloat("temperature", ppSettings.temperature); loadFloat("gamma", ppSettings.gamma);
        loadBool("enableVignette", ppSettings.enableVignette); loadFloat("vignetteIntensity", ppSettings.vignetteIntensity);
        loadBool("enableChromaticAberration", ppSettings.enableChromaticAberration); loadFloat("caIntensity", ppSettings.caIntensity);
        loadBool("enableBloom", ppSettings.enableBloom); loadFloat("bloomThreshold", ppSettings.bloomThreshold); loadFloat("bloomIntensity", ppSettings.bloomIntensity); loadInt("bloomBlurIterations", ppSettings.bloomBlurIterations);
        loadBool("enableLensFlares", ppSettings.enableLensFlares); loadFloat("flareIntensity", ppSettings.flareIntensity); loadFloat("ghostDispersal", ppSettings.ghostDispersal); loadInt("ghosts", ppSettings.ghosts);
        loadBool("enableGodRays", ppSettings.enableGodRays); loadFloat("godRaysIntensity", ppSettings.godRaysIntensity);
        loadBool("enableFilmGrain", ppSettings.enableFilmGrain); loadFloat("grainIntensity", ppSettings.grainIntensity);
        loadBool("enableSharpen", ppSettings.enableSharpen); loadFloat("sharpenIntensity", ppSettings.sharpenIntensity);
        loadBool("enableDoF", ppSettings.enableDoF); loadFloat("focusDistance", ppSettings.focusDistance); loadFloat("focusRange", ppSettings.focusRange); loadFloat("bokehSize", ppSettings.bokehSize);
        loadBool("enableMotionBlur", ppSettings.enableMotionBlur); loadFloat("mbStrength", ppSettings.mbStrength);
        loadBool("enableFog", ppSettings.enableFog); loadFloat("fogDensity", ppSettings.fogDensity); loadFloat("fogHeightFalloff", ppSettings.fogHeightFalloff); loadFloat("fogBaseHeight", ppSettings.fogBaseHeight);
        loadFloat("inscatterPower", ppSettings.inscatterPower); loadFloat("inscatterIntensity", ppSettings.inscatterIntensity);
                if (j.contains("fogColor") && j["fogColor"].is_array()) {
            for (int i = 0; i < 3; i++) ppSettings.fogColor[i] = j["fogColor"][i];
        }
        if (j.contains("inscatterColor") && j["inscatterColor"].is_array()) {
            for (int i = 0; i < 3; i++) ppSettings.inscatterColor[i] = j["inscatterColor"][i];
        }
    }
    void DrawPostProcessContent() {
        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Post Processing Settings");
        ImGui::Separator(); ImGui::Spacing();
        if (ImGui::BeginTabBar("PP_Tabs")) {
                        if (ImGui::BeginTabItem("Lighting")) {
                if (ImGui::CollapsingHeader("SSAO (Ambient Occlusion)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable SSAO", &ppSettings.enableSSAO);
                    if (ppSettings.enableSSAO) {
                        ImGui::SliderFloat("Radius##ssao", &ppSettings.ssaoRadius, 0.1f, 3.0f);
                        ImGui::SliderFloat("Bias##ssao", &ppSettings.ssaoBias, 0.001f, 0.2f);
                        ImGui::SliderFloat("Intensity##ssao", &ppSettings.ssaoIntensity, 0.1f, 10.0f);
                        ImGui::SliderFloat("Power##ssao", &ppSettings.ssaoPower, 1.0f, 8.0f);
                    }
                }
                if (ImGui::CollapsingHeader("SSGI (Global Illumination)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable SSGI", &ppSettings.enableSSGI);
                    if (ppSettings.enableSSGI) {
                        ImGui::SliderInt("Ray Count", &ppSettings.ssgiRayCount, 1, 32);
                        ImGui::SliderFloat("Step Size", &ppSettings.ssgiStepSize, 0.05f, 2.0f);
                        ImGui::SliderFloat("Thickness", &ppSettings.ssgiThickness, 0.01f, 2.0f);
                        ImGui::SliderInt("Blur Range", &ppSettings.blurRange, 1, 10);
                    }
                }
                ImGui::EndTabItem();
            }
                        if (ImGui::BeginTabItem("Shadows")) {
                            if (ImGui::CollapsingHeader("SSCS (Contact Shadows)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::Checkbox("Enable SSCS", &ppSettings.enableContactShadows);
                                if (ppSettings.enableContactShadows) {
                                    ImGui::SliderFloat("Ray Length", &ppSettings.contactShadowLength, 0.01f, 0.5f);
                                    ImGui::SliderInt("Ray Steps", &ppSettings.contactShadowSteps, 4, 64);
                                    ImGui::SliderFloat("Ray Thickness", &ppSettings.contactShadowThickness, 0.01f, 0.5f);
                                }

                                ImGui::EndTabItem();
                            }
                        }
                        if (ImGui::BeginTabItem("Effects")) {
                if (ImGui::CollapsingHeader("Bloom & Lens Flares", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable Bloom", &ppSettings.enableBloom);
                    if (ppSettings.enableBloom) {
                        ImGui::SliderFloat("Threshold##bloom", &ppSettings.bloomThreshold, 0.0f, 5.0f);
                        ImGui::SliderFloat("Intensity##bloom", &ppSettings.bloomIntensity, 0.0f, 5.0f);
                        ImGui::SliderInt("Blur Iterations", &ppSettings.bloomBlurIterations, 1, 15);
                    }
                    ImGui::Separator();
                    ImGui::Checkbox("Enable Lens Flares", &ppSettings.enableLensFlares);
                    if (ppSettings.enableLensFlares) {
                        ImGui::SliderFloat("Flare Intensity", &ppSettings.flareIntensity, 0.0f, 5.0f);
                        ImGui::SliderFloat("Ghost Dispersal", &ppSettings.ghostDispersal, 0.01f, 1.0f);
                        ImGui::SliderInt("Ghosts Count", &ppSettings.ghosts, 1, 10);
                    }
                }
                if (ImGui::CollapsingHeader("Screen FX", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("God Rays", &ppSettings.enableGodRays);
                    if (ppSettings.enableGodRays) ImGui::SliderFloat("Rays Power", &ppSettings.godRaysIntensity, 0.0f, 3.0f);
                    ImGui::Checkbox("Film Grain", &ppSettings.enableFilmGrain);
                    if (ppSettings.enableFilmGrain) ImGui::SliderFloat("Grain Strength", &ppSettings.grainIntensity, 0.0f, 0.2f);
                    ImGui::Checkbox("Vignette", &ppSettings.enableVignette);
                    if (ppSettings.enableVignette) ImGui::SliderFloat("Vignette Intensity", &ppSettings.vignetteIntensity, 0.1f, 2.0f);
                    ImGui::Checkbox("Chromatic Aberration", &ppSettings.enableChromaticAberration);
                    if (ppSettings.enableChromaticAberration) ImGui::SliderFloat("CA Intensity", &ppSettings.caIntensity, 0.001f, 0.55f);
                }
                ImGui::EndTabItem();
            }
                        if (ImGui::BeginTabItem("Camera & Fog")) {
                if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable DoF", &ppSettings.enableDoF);
                    if (ppSettings.enableDoF) {
                        ImGui::SliderFloat("Focus Dist", &ppSettings.focusDistance, 0.1f, 100.0f);
                        ImGui::SliderFloat("Focus Range", &ppSettings.focusRange, 0.1f, 50.0f);
                        ImGui::SliderFloat("Bokeh Size", &ppSettings.bokehSize, 0.0f, 10.0f);
                    }
                }
                if (ImGui::CollapsingHeader("Atmospheric Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable Fog", &ppSettings.enableFog);
                    if (ppSettings.enableFog) {
                        ImGui::ColorEdit3("Fog Color", ppSettings.fogColor);
                        ImGui::ColorEdit3("Sun Inscatter Color", ppSettings.inscatterColor);
                        ImGui::SliderFloat("Density", &ppSettings.fogDensity, 0.001f, 0.2f);
                        ImGui::SliderFloat("Height Falloff", &ppSettings.fogHeightFalloff, 0.01f, 1.0f);
                        ImGui::SliderFloat("Base Height", &ppSettings.fogBaseHeight, -50.0f, 50.0f);
                        ImGui::SliderFloat("Sun Inscatter Power", &ppSettings.inscatterPower, 1.0f, 32.0f);
                        ImGui::SliderFloat("Sun Inscatter Int", &ppSettings.inscatterIntensity, 0.0f, 5.0f);
                    }
                }
                ImGui::EndTabItem();
            }
                        if (ImGui::BeginTabItem("Color Grading")) {
                if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Auto Exposure", &ppSettings.autoExposure);
                    if (ppSettings.autoExposure) {
                        ImGui::SliderFloat("Compensation", &ppSettings.exposureCompensation, 0.1f, 5.0f);
                        ImGui::SliderFloat("Min Brightness", &ppSettings.minBrightness, 0.01f, 2.0f);
                        ImGui::SliderFloat("Max Brightness", &ppSettings.maxBrightness, 1.0f, 10.0f);
                    }
                    else {
                        ImGui::SliderFloat("Manual Exp", &ppSettings.manualExposure, 0.1f, 10.0f);
                    }
                }
                if (ImGui::CollapsingHeader("Color Corrections", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderFloat("Contrast", &ppSettings.contrast, 0.5f, 2.0f);
                    ImGui::SliderFloat("Saturation", &ppSettings.saturation, 0.0f, 2.0f);
                    ImGui::SliderFloat("Color Temp (K)", &ppSettings.temperature, 2000.0f, 12000.0f);
                    ImGui::SliderFloat("Gamma", &ppSettings.gamma, 1.0f, 2.8f);
                }
                if (ImGui::CollapsingHeader("Sharpening", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable Sharpen", &ppSettings.enableSharpen);
                    if (ppSettings.enableSharpen) ImGui::SliderFloat("Sharpness", &ppSettings.sharpenIntensity, 0.0f, 2.0f);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("💾 SAVE SETTINGS", ImVec2(-1, 40))) SavePostProcessSettings();
    }
    void DrawTextureProperty(const char* label, char* pathBuffer, size_t bufferSize) {
        ImGui::PushID(label);
        ImGui::Text("%s", label);

        // Так как .bhtex - это сжатый бинарник видеокарты, stb_image не сможет сделать из него миниатюру.
        // Поэтому мы просто показываем красивую иконку текстуры.
        GLuint texID = GetFileIcon(".bhtex", false);

        if (texID != 0 && strlen(pathBuffer) > 0)
            ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(64, 64));
        else
            ImGui::Button("NO TEX", ImVec2(64, 64));

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextWrapped("%s", strlen(pathBuffer) > 0 ? pathBuffer : "None");

        if (ImGui::Button("Select Texture...")) ImGui::OpenPopup("TexturePickerPopup");
        ImGui::EndGroup();

        if (ImGui::BeginPopup("TexturePickerPopup")) {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Available Textures:");
            ImGui::Separator();
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();

                    // --- ТЕПЕРЬ ИЩЕМ ТОЛЬКО .bhtex ---
                    if (ext == ".bhtex") {
                        std::string relPath = fs::relative(entry.path(), projectDirectory).string();
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        if (ImGui::Selectable(relPath.c_str())) {
                            strcpy_s(pathBuffer, bufferSize, relPath.c_str());
                        }
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
        else if (ext == ".bhtex") {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Texture Asset: %s", filename.c_str());
            ImGui::Separator();

            // Загружаем миниатюру, если она есть
            GLuint texID = GetImageThumbnail(activeAsset);
            if (texID != 0) {
                ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(128.0f, 128.0f));
                ImGui::Spacing();
            }

            // 1. Читаем текущие настройки прямо из файла
            BHTexHeader header;
            if (TextureImporter::ReadHeader(activeAsset, header)) {
                ImGui::TextDisabled("Resolution: %d x %d", header.width, header.height);
                ImGui::TextDisabled("Mipmaps: %d", header.mipCount);
                ImGui::TextDisabled("Compression: %s", header.format == 1 ? "BC3 (DXT5 - Alpha)" : "BC1 (DXT1 - No Alpha)");
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                bool settingsChanged = false;

                // Массивы для выпадающих списков
                const char* wrapNames[] = { "Repeat", "Clamp To Edge", "Mirrored Repeat" };
                uint32_t wrapValues[] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT };

                const char* minNames[] = { "Linear Mipmap Linear", "Nearest Mipmap Nearest", "Linear", "Nearest" };
                uint32_t minValues[] = { GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR, GL_NEAREST };

                const char* magNames[] = { "Linear", "Nearest" };
                uint32_t magValues[] = { GL_LINEAR, GL_NEAREST };

                // Хелпер, чтобы найти нужный индекс по значению из файла
                auto getIndex = [](uint32_t val, uint32_t* arr, int size) {
                    for (int i = 0; i < size; i++) if (arr[i] == val) return i;
                    return 0;
                    };

                // --- ОТРИСОВКА ИНТЕРФЕЙСА ---
                ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.9f, 1.0f), "Advanced Settings");

                int currentWrapS = getIndex(header.wrapS, wrapValues, 3);
                if (ImGui::Combo("Wrap S (U)", &currentWrapS, wrapNames, 3)) { header.wrapS = wrapValues[currentWrapS]; settingsChanged = true; }

                int currentWrapT = getIndex(header.wrapT, wrapValues, 3);
                if (ImGui::Combo("Wrap T (V)", &currentWrapT, wrapNames, 3)) { header.wrapT = wrapValues[currentWrapT]; settingsChanged = true; }

                ImGui::Spacing();

                int currentMin = getIndex(header.minFilter, minValues, 4);
                if (ImGui::Combo("Min Filter", &currentMin, minNames, 4)) { header.minFilter = minValues[currentMin]; settingsChanged = true; }

                int currentMag = getIndex(header.magFilter, magValues, 2);
                if (ImGui::Combo("Mag Filter", &currentMag, magNames, 2)) { header.magFilter = magValues[currentMag]; settingsChanged = true; }


                // 2. Если пользователь дернул ползунок - мгновенно перезаписываем файл!
                if (settingsChanged) {
                    TextureImporter::UpdateHeader(activeAsset, header);
                    // В будущем мы добавим сюда вызов обновления текстуры в видеопамяти, 
                    // если она прямо сейчас используется на какой-то 3D-модели!
                }
            }
        }
        else if (ext == ".bhmat") {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Material Settings: %s", filename.c_str());
            ImGui::Separator(); ImGui::Spacing();

            DrawTextureProperty("Albedo Map", editAlbedo, sizeof(editAlbedo)); ImGui::Spacing();
            DrawTextureProperty("Normal Map", editNormal, sizeof(editNormal)); ImGui::Spacing();
            DrawTextureProperty("Height Map", editHeight, sizeof(editHeight)); ImGui::Spacing();
            DrawTextureProperty("Metallic Map", editMetallic, sizeof(editMetallic)); ImGui::Spacing();
            DrawTextureProperty("Roughness Map", editRoughness, sizeof(editRoughness)); ImGui::Spacing();
            DrawTextureProperty("AO Map", editAO, sizeof(editAO));

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (ImGui::Button("💾 SAVE MATERIAL", ImVec2(-1, 40))) {
                std::ifstream fileIn(activeAsset);
                json j;
                if (fileIn.is_open()) {
                    try { fileIn >> j; }
                    catch (...) {}
                    fileIn.close();
                }

                // Безопасный помощник для сохранения
                auto saveTex = [&](const std::string& key, char* buffer) {
                    if (!j.contains("textures") || !j["textures"].is_object()) {
                        j["textures"] = json::object(); // Если объекта нет, создаем его
                    }

                    if (strlen(buffer) > 0) {
                        j["textures"][key] = buffer;
                    }
                    else {
                        if (j["textures"].contains(key)) {
                            j["textures"].erase(key);
                        }
                    }
                    };

                saveTex("albedo", editAlbedo);
                saveTex("normal", editNormal);
                saveTex("height", editHeight);
                saveTex("ao", editAO);
                saveTex("metallic", editMetallic);
                saveTex("roughness", editRoughness);

                std::ofstream fileOut(activeAsset);
                fileOut << j.dump(4);
                fileOut.close();

                // Обновляем материал в памяти (безопасно)
                Material* activeMat = nullptr;
                for (auto& pair : Serializer::loadedMaterials) {
                    if (fs::path(pair.first) == fs::path(activeAsset)) {
                        activeMat = pair.second; break;
                    }
                }

                if (activeMat != nullptr) {
                    activeMat->setAlbedo(strlen(editAlbedo) > 0 ? (projectDirectory.string() + "/" + editAlbedo) : "");
                    activeMat->setNormal(strlen(editNormal) > 0 ? (projectDirectory.string() + "/" + editNormal) : "");
                    activeMat->setHeight(strlen(editHeight) > 0 ? (projectDirectory.string() + "/" + editHeight) : "");
                    activeMat->setAO(strlen(editAO) > 0 ? (projectDirectory.string() + "/" + editAO) : "");
                    activeMat->setMetallic(strlen(editMetallic) > 0 ? (projectDirectory.string() + "/" + editMetallic) : "");
                    activeMat->setRoughness(strlen(editRoughness) > 0 ? (projectDirectory.string() + "/" + editRoughness) : "");
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

    void DrawMainMenuBar(entt::registry& registry) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) { registry.clear(); selectedEntity = entt::null; undoStack.clear(); redoStack.clear(); }
                if (ImGui::MenuItem("Save Scene")) { std::cout << "Update Serializer!\n"; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) { Undo(registry); }
                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !redoStack.empty())) { Redo(registry); }
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

    void Draw(Window& window, Camera& camera, entt::registry& registry, Render& render) {
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        DrawMainMenuBar(registry);
        ImGuiIO& io = ImGui::GetIO();

        if (!ImGui::IsAnyItemActive()) {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo(registry);
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) Redo(registry);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedEntity != entt::null) {
                SaveState(registry);
                entt::entity parent = entt::null;
                if (registry.all_of<HierarchyComponent>(selectedEntity)) parent = registry.get<HierarchyComponent>(selectedEntity).parent;
                entt::entity newEnt = CloneHierarchy(registry, selectedEntity, parent);
                if (parent != entt::null) registry.get<HierarchyComponent>(parent).children.push_back(newEnt);
                selectedEntity = newEnt;
            }
            // Копирование (Clipboard) для ECS сделать сложнее, пока оставляем пустое место
        }

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
        }
        ImGui::End();

        ImGuizmo::BeginFrame(); ImGuizmo::SetOrthographic(false); ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList()); ImGuizmo::SetRect(0, 0, (float)window.width, (float)window.height);
        static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
        static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) currentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::SCALE;
        glm::mat4 view = camera.GetViewMatrix(); glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f);

        // --- МЫШКА (ЛУЧ) ---
        if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && !ImGuizmo::IsOver()) {
            glm::vec3 rayOrigin = camera.Position; glm::vec3 rayDir = GetMouseRay(window, camera);
            float closestT = 1000.0f; entt::entity hitIndex = entt::null;

            auto pickView = registry.view<TransformComponent>();
            pickView.each([&](entt::entity entity, TransformComponent& tComp) {
                float t;
                if (TestRayOBB(rayOrigin, rayDir, tComp.transform.matrix, t)) {
                    if (t < closestT) { closestT = t; hitIndex = entity; }
                }
                });
            if (hitIndex != entt::null) {
                selectedEntity = hitIndex;
                render.isSceneDirty = true;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.position), glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.rotation), glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.scale), glm::value_ptr(model));
            }
        }

        if (selectedEntity != entt::null && registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity)) {
            auto& tComp = registry.get<TransformComponent>(selectedEntity);
            if (ImGuizmo::IsUsing() && !wasUsingGizmo) SaveState(registry);
            wasUsingGizmo = ImGuizmo::IsUsing();
            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), currentGizmoOperation, currentGizmoMode, glm::value_ptr(model));

            if (ImGuizmo::IsUsing()) {
                render.isSceneDirty = true;
                glm::mat4 newWorldMatrix = model;
                if (registry.all_of<HierarchyComponent>(selectedEntity)) {
                    entt::entity parent = registry.get<HierarchyComponent>(selectedEntity).parent;
                    if (parent != entt::null && registry.all_of<TransformComponent>(parent)) {
                        glm::mat4 parentWorldMatrix = registry.get<TransformComponent>(parent).transform.matrix;
                        newWorldMatrix = glm::inverse(parentWorldMatrix) * newWorldMatrix;
                    }
                }
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(newWorldMatrix), glm::value_ptr(tComp.transform.position), glm::value_ptr(tComp.transform.rotation), glm::value_ptr(tComp.transform.scale));
                tComp.transform.updatematrix = true;
                if (registry.all_of<PhysicsComponent>(selectedEntity)) registry.get<PhysicsComponent>(selectedEntity).updatePhysicsTransform = true;
            }
        }

        DrawSceneOutliner(registry, io);
        DrawSceneInspector(registry,render);
        DrawContentBrowser();
        DrawPropertiesWindow();
        // Вызов твоих старых методов, которые я не стал сюда переписывать целиком, чтобы не было ошибки лимита символов
        // Вставь сюда свой код DrawContentBrowser() и DrawPropertiesWindow() из старого файла
        // Я оставил в классе прототипы функций, просто скопируй тела этих двух функций из старого UI.h!

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
};
#endif