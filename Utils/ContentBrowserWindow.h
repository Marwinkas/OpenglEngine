#pragma once
#include "IUIWindow.h"
#include "../Render/Texture.hpp"
#include "../Render/ModelImporter.h"
#include "UI/MaterialPreview.hpp"
#include "UI/NativeDialogs.hpp"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cctype>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <any>
#include <unordered_map>
#include <unordered_set>

namespace burnhope {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    class ContentBrowserWindow : public IUIWindow {
    public:
        ContentBrowserWindow() : IUIWindow("Content Browser") {}

        void Draw(UIContext& context, ui::UIWidgets& widgets, ui::Rect contentRect) override {
            if (!m_IsOpen) return;
            m_WantContext = false;
            EnsureSidecar(context);

            ui::Panel panel(widgets, m_Name, contentRect, 6.0f, false);
            HandleHotkeys(context, widgets);

            constexpr float kToolbarH = 34.0f;
            constexpr float kCrumbsH = 26.0f;
            constexpr float kFooterH = 26.0f;
            constexpr float kSplitW = 5.0f;
            constexpr float kGap = 4.0f;

            glm::vec2 origin = widgets.GetCursor();
            glm::vec2 avail = widgets.ContentAvail();

            ui::Rect toolbar{origin.x, origin.y, avail.x, kToolbarH};
            float bodyY = origin.y + kToolbarH + kGap;
            float bodyH = std::max(64.0f, avail.y - kToolbarH - kGap - kFooterH - 2.0f);
            ui::Rect footerRect{origin.x, bodyY + bodyH + 2.0f, avail.x, kFooterH};

            float treeW = ui::Clamp(m_TreeWidth, 160.0f, std::max(160.0f, avail.x - 220.0f));
            bool showDetails = avail.x > 720.0f;
            float detailsW = showDetails
                ? ui::Clamp(m_DetailsWidth, 200.0f, std::max(200.0f, avail.x - treeW - 280.0f))
                : 0.0f;

            ui::Rect treeRect{origin.x, bodyY, treeW, bodyH};
            ui::Rect splitL{origin.x + treeW, bodyY, kSplitW, bodyH};
            float midX = origin.x + treeW + kSplitW + 2.0f;
            float midW = std::max(80.0f, avail.x - treeW - kSplitW - 2.0f - (showDetails ? detailsW + kSplitW + 2.0f : 0.0f));
            ui::Rect crumbs{midX, bodyY, midW, kCrumbsH};
            ui::Rect gridRect{midX, bodyY + kCrumbsH + 1.0f, midW, std::max(24.0f, bodyH - kCrumbsH - 1.0f)};
            ui::Rect splitR{midX + midW, bodyY, kSplitW, bodyH};
            ui::Rect detailsRect{midX + midW + kSplitW + 2.0f, bodyY, detailsW, bodyH};

            DrawToolbar(context, widgets, toolbar);
            DragSplitter(widgets, splitL, m_TreeWidth, m_SplitDragging, origin.x, 160.0f,
                         std::max(160.0f, avail.x - 220.0f));
            if (showDetails) {
                DragSplitter(widgets, splitR, m_DetailsWidth, m_DetailsDragging,
                             detailsRect.x + detailsW, 200.0f,
                             std::max(200.0f, avail.x - treeW - 280.0f), true);
            }

            if (gridRect.Contains(widgets.MousePos().x, widgets.MousePos().y) &&
                widgets.Ctrl() && widgets.MouseWheel() != 0.0f) {
                m_ThumbnailSize = ui::Clamp(m_ThumbnailSize + widgets.MouseWheel() * 10.0f, 48.0f, 176.0f);
                widgets.AbsorbMouseWheel();
            }

            widgets.Background(treeRect, C::kSidebar, 6.0f);
            {
                ui::Child tree(widgets, "FolderTree", treeRect, true);
                DrawSidebar(context, widgets);
            }

            DrawBreadcrumbs(context, widgets, crumbs);
            widgets.Background(gridRect, C::kGrid, 6.0f);
            {
                ui::Child grid(widgets, "AssetGrid", gridRect, true);
                if (m_ListView) DrawList(context, widgets, gridRect);
                else DrawGrid(context, widgets, gridRect);
            }

            if (showDetails) {
                widgets.Background(detailsRect, C::kSidebar, 6.0f);
                ui::Child details(widgets, "AssetDetails", detailsRect, true);
                DrawDetails(context, widgets, detailsRect);
            }

            if (m_WantContext) widgets.OpenPopup("CB_Context");
            DrawContextMenu(context, widgets);
            DrawFooter(context, widgets, footerRect, m_LastItemCount);
            context.PinMaterialFromSelection();
            if (m_SidecarDirty && !widgets.HasTextFocus()) SaveSidecar(context);
        }

    private:
        struct C {
            static constexpr ui::Color kSidebar     = ui::Color::RGBA8(28, 31, 34);
            static constexpr ui::Color kGrid        = ui::Color::RGBA8(22, 24, 26);
            static constexpr ui::Color kCard        = ui::Color::RGBA8(38, 42, 46);
            static constexpr ui::Color kCardHover   = ui::Color::RGBA8(48, 54, 60);
            static constexpr ui::Color kAccent      = ui::Color::RGBA8(62, 134, 245);
            static constexpr ui::Color kAccentSoft  = ui::Color::RGBA8(62, 134, 245, 55);
            static constexpr ui::Color kText        = ui::Color::RGBA8(236, 238, 242);
            static constexpr ui::Color kMuted       = ui::Color::RGBA8(155, 163, 175);
            static constexpr ui::Color kFolder      = ui::Color::RGBA8(232, 184, 74);
            static constexpr ui::Color kFolderTab   = ui::Color::RGBA8(196, 148, 48);
            static constexpr ui::Color kAdd         = ui::Color::RGBA8(62, 176, 92);
            static constexpr ui::Color kAddHover    = ui::Color::RGBA8(78, 196, 110);
            static constexpr ui::Color kChip        = ui::Color::RGBA8(42, 46, 52);
            static constexpr ui::Color kField       = ui::Color::RGBA8(20, 22, 24);
            static constexpr ui::Color kBadge       = ui::Color::RGBA8(18, 20, 22, 210);
        };

        enum class Kind : uint8_t { All, Folder, Scene, Mesh, Material, Texture, Sound, Particle, Other };

        struct Collection {
            std::string name;
            std::vector<std::string> paths;
        };

        struct AssetMeta {
            std::string tags;
            std::string description;
            std::string usage;
            std::string notes;
        };

        std::string m_Search;
        float m_ThumbnailSize = 96.0f;
        float m_TreeWidth = 220.0f;
        float m_DetailsWidth = 260.0f;
        int m_LastClickedIndex = -1;
        int m_LastItemCount = 0;
        int m_ActiveCollection = -1;
        Kind m_Filter = Kind::All;
        std::string m_RenameBuffer;
        std::string m_ContextPath;
        bool m_ContextIsDir = false;
        bool m_SplitDragging = false;
        bool m_DetailsDragging = false;
        bool m_OpenMatEditorOnRelease = false;
        bool m_WantContext = false;
        bool m_ListView = false;
        bool m_SourcesOpen = true;
        bool m_FavoritesOpen = true;
        bool m_CollectionsOpen = true;
        bool m_SidecarDirty = false;
        std::string m_PendingSingleSelect;
        fs::path m_SidecarProject;
        std::unordered_map<std::string, std::shared_ptr<BurnhopeTexture>> m_ImageThumbs;
        std::unordered_set<std::string> m_OpenFolders;
        std::vector<std::string> m_Favorites;
        std::vector<Collection> m_Collections;
        std::unordered_map<std::string, AssetMeta> m_Meta;

        static std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        static std::string DisplayName(const fs::path& path, bool isDir) {
            return isDir ? path.filename().string() : path.stem().string();
        }

        static Kind Classify(const fs::path& path, bool isDir) {
            if (isDir) return Kind::Folder;
            std::string ext = ToLower(path.extension().string());
            if (ext == ".bhscene" || ext == ".burnscene") return Kind::Scene;
            if (ext == ".bhmat" || ext == ".json") return Kind::Material;
            if (ext == ".bhmesh" || ext == ".bhmodel" || ext == ".obj" || ext == ".fbx" ||
                ext == ".gltf" || ext == ".glb") return Kind::Mesh;
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bhtex")
                return Kind::Texture;
            if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") return Kind::Sound;
            if (ext == ".bhfx" || ext == ".vfx" || ext == ".particle" || ext == ".pfx") return Kind::Particle;
            return Kind::Other;
        }

        static const char* KindLabel(Kind k) {
            switch (k) {
                case Kind::Folder: return "Folder";
                case Kind::Scene: return "Scene";
                case Kind::Mesh: return "Mesh";
                case Kind::Material: return "Material";
                case Kind::Texture: return "Texture";
                case Kind::Sound: return "Sound";
                case Kind::Particle: return "Particle";
                default: return "Asset";
            }
        }

        static ui::Color KindColor(Kind k) {
            switch (k) {
                case Kind::Folder: return C::kFolder;
                case Kind::Scene: return ui::Color::RGBA8(120, 200, 110);
                case Kind::Mesh: return ui::Color::RGBA8(72, 196, 214);
                case Kind::Material: return ui::Color::RGBA8(232, 140, 72);
                case Kind::Texture: return ui::Color::RGBA8(168, 122, 220);
                case Kind::Sound: return ui::Color::RGBA8(92, 196, 130);
                case Kind::Particle: return ui::Color::RGBA8(110, 180, 230);
                default: return ui::kTheme.button;
            }
        }

        static std::string FormatBytes(uintmax_t n) {
            char buf[32];
            if (n < 1024) {
                std::snprintf(buf, sizeof(buf), "%ju B", static_cast<uintmax_t>(n));
            } else if (n < 1024ull * 1024ull) {
                std::snprintf(buf, sizeof(buf), "%.1f KB", n / 1024.0);
            } else {
                std::snprintf(buf, sizeof(buf), "%.1f MB", n / (1024.0 * 1024.0));
            }
            return buf;
        }

        fs::path SidecarPath(UIContext& context) const {
            return context.projectDirectory / ".burnhope" / "content_browser.json";
        }

        void EnsureSidecar(UIContext& context) {
            if (m_SidecarProject == context.projectDirectory) return;
            m_SidecarProject = context.projectDirectory;
            LoadSidecar(context);
            if (m_OpenFolders.empty()) m_OpenFolders.insert(context.projectDirectory.string());
        }

        void LoadSidecar(UIContext& context) {
            m_Favorites.clear();
            m_Collections.clear();
            m_Meta.clear();
            std::ifstream in(SidecarPath(context));
            if (!in) return;
            json j;
            try { in >> j; } catch (...) { return; }
            if (j.contains("favorites")) {
                for (const auto& v : j["favorites"]) m_Favorites.push_back(v.get<std::string>());
            }
            if (j.contains("collections")) {
                for (const auto& c : j["collections"]) {
                    Collection col;
                    col.name = c.value("name", "Collection");
                    if (c.contains("paths")) {
                        for (const auto& p : c["paths"]) col.paths.push_back(p.get<std::string>());
                    }
                    m_Collections.push_back(std::move(col));
                }
            }
            if (j.contains("meta")) {
                for (auto it = j["meta"].begin(); it != j["meta"].end(); ++it) {
                    AssetMeta m;
                    m.tags = it.value().value("tags", "");
                    m.description = it.value().value("description", "");
                    m.usage = it.value().value("usage", "");
                    m.notes = it.value().value("notes", "");
                    m_Meta[it.key()] = std::move(m);
                }
            }
        }

        void SaveSidecar(UIContext& context) {
            fs::path path = SidecarPath(context);
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            json j;
            j["favorites"] = m_Favorites;
            j["collections"] = json::array();
            for (const auto& c : m_Collections) {
                j["collections"].push_back({{"name", c.name}, {"paths", c.paths}});
            }
            j["meta"] = json::object();
            for (const auto& [k, m] : m_Meta) {
                if (m.tags.empty() && m.description.empty() && m.usage.empty() && m.notes.empty()) continue;
                j["meta"][k] = {{"tags", m.tags}, {"description", m.description},
                                {"usage", m.usage}, {"notes", m.notes}};
            }
            std::ofstream out(path);
            if (out) out << j.dump(2);
            m_SidecarDirty = false;
        }

        AssetMeta& MetaFor(const std::string& path) { return m_Meta[path]; }

        void DrawFolderGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            widgets.Background({r.x + 3.0f, r.y + r.h * 0.20f, r.w * 0.42f, r.h * 0.22f}, C::kFolderTab, 2.0f);
            widgets.Background({r.x + 3.0f, r.y + r.h * 0.34f, r.w - 6.0f, r.h * 0.50f}, C::kFolder, 3.0f);
        }

        void DrawDocGlyph(ui::UIWidgets& widgets, ui::Rect r, ui::Color c) {
            widgets.Background({r.x + 6.0f, r.y + 4.0f, r.w - 12.0f, r.h - 8.0f}, c, 4.0f);
            widgets.Background({r.x + r.w - 16.0f, r.y + 4.0f, 10.0f, 10.0f}, ui::Color::RGBA8(20, 21, 26, 180), 2.0f);
        }

        void DrawMatGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            float s = std::min(r.w, r.h) * 0.72f;
            ui::Rect ball{r.x + (r.w - s) * 0.5f, r.y + (r.h - s) * 0.5f, s, s};
            widgets.Background(ball, ui::Color::RGBA8(210, 120, 70), s * 0.5f);
            widgets.Background({ball.x + s * 0.18f, ball.y + s * 0.16f, s * 0.34f, s * 0.22f},
                               ui::Color::RGBA8(255, 210, 160, 80), s * 0.2f);
        }

        void DrawMeshGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            ui::Color side = ui::Color::RGBA8(50, 150, 165);
            ui::Color top = ui::Color::RGBA8(110, 214, 226);
            ui::Color front = ui::Color::RGBA8(72, 196, 214);
            float s = std::min(r.w, r.h);
            float cx = r.x + r.w * 0.5f;
            float cy = r.y + r.h * 0.52f;
            widgets.Background({cx - s * 0.28f, cy - s * 0.18f, s * 0.40f, s * 0.36f}, front, 3.0f);
            widgets.Background({cx - s * 0.08f, cy - s * 0.30f, s * 0.40f, s * 0.22f}, top, 3.0f);
            widgets.Background({cx + s * 0.12f, cy - s * 0.18f, s * 0.18f, s * 0.36f}, side, 3.0f);
        }

        void DrawSoundGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            float s = std::min(r.w, r.h);
            ui::Color c = KindColor(Kind::Sound);
            widgets.Background({r.x + s * 0.22f, r.y + s * 0.38f, s * 0.22f, s * 0.24f}, c, 2.0f);
            widgets.Background({r.x + s * 0.36f, r.y + s * 0.28f, s * 0.16f, s * 0.44f}, c, 2.0f);
            widgets.Background({r.x + s * 0.56f, r.y + s * 0.34f, s * 0.08f, s * 0.32f}, c, 2.0f);
            widgets.Background({r.x + s * 0.68f, r.y + s * 0.28f, s * 0.08f, s * 0.44f}, c, 2.0f);
        }

        void DrawParticleGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            ui::Color c = KindColor(Kind::Particle);
            float s = std::min(r.w, r.h);
            widgets.Background({r.x + s * 0.28f, r.y + s * 0.30f, s * 0.34f, s * 0.28f}, c, s * 0.14f);
            widgets.Background({r.x + s * 0.46f, r.y + s * 0.26f, s * 0.28f, s * 0.24f}, c, s * 0.12f);
            widgets.Background({r.x + s * 0.22f, r.y + s * 0.46f, s * 0.46f, s * 0.28f}, c, s * 0.14f);
        }

        void DrawTypeBadge(ui::UIWidgets& widgets, ui::Rect thumb, Kind k) {
            float s = 16.0f;
            ui::Rect b{thumb.x + thumb.w - s - 5.0f, thumb.y + thumb.h - s - 5.0f, s, s};
            widgets.Background(b, C::kBadge, 4.0f);
            ui::Rect inner{b.x + 3.0f, b.y + 3.0f, b.w - 6.0f, b.h - 6.0f};
            if (k == Kind::Folder) DrawFolderGlyph(widgets, inner);
            else if (k == Kind::Material) widgets.Background(inner, KindColor(k), inner.w * 0.5f);
            else widgets.Background(inner, KindColor(k), 2.0f);
        }

        std::shared_ptr<BurnhopeTexture> LoadImageThumb(UIContext& context, const std::string& pathStr) {
            auto it = m_ImageThumbs.find(pathStr);
            if (it != m_ImageThumbs.end()) return it->second;
            if (!context.device) return nullptr;
            try { m_ImageThumbs[pathStr] = BurnhopeTexture::createTextureFromFile(*context.device, pathStr); }
            catch (...) { m_ImageThumbs[pathStr] = nullptr; }
            return m_ImageThumbs[pathStr];
        }

        void DrawFileIcon(UIContext& context, ui::UIWidgets& widgets, ui::Rect icon, const fs::path& path, bool isDir) {
            widgets.Background(icon, C::kField, 6.0f);
            Kind k = Classify(path, isDir);
            if (isDir) { DrawFolderGlyph(widgets, icon); DrawTypeBadge(widgets, icon, k); return; }
            std::string ext = ToLower(path.extension().string());
            std::string pathStr = path.string();
            if (k == Kind::Texture) {
                if (auto tex = LoadImageThumb(context, pathStr)) {
                    widgets.ImageAt(icon, tex->getImageView(), tex->getSampler(), 6.0f);
                    DrawTypeBadge(widgets, icon, k);
                    return;
                }
                if (ext == ".bhtex") {
                    for (const char* srcExt : {".png", ".jpg", ".jpeg", ".tga"}) {
                        fs::path src = path;
                        src.replace_extension(srcExt);
                        if (!fs::exists(src)) continue;
                        if (auto tex = LoadImageThumb(context, src.string())) {
                            widgets.ImageAt(icon, tex->getImageView(), tex->getSampler(), 6.0f);
                            DrawTypeBadge(widgets, icon, k);
                            return;
                        }
                    }
                }
            }
            if (k == Kind::Material && context.materialPreview) {
                if (BurnhopeTexture* thumb = context.materialPreview->Thumb(pathStr)) {
                    widgets.ImageAt(icon, thumb->getImageView(), thumb->getSampler(), 6.0f,
                                    {1, 1, 1, 1}, context.materialPreview->PreviewLayout());
                    DrawTypeBadge(widgets, icon, k);
                    return;
                }
                DrawMatGlyph(widgets, icon);
                DrawTypeBadge(widgets, icon, k);
                return;
            }
            if (k == Kind::Material) DrawMatGlyph(widgets, icon);
            else if (k == Kind::Mesh) DrawMeshGlyph(widgets, icon);
            else if (k == Kind::Sound) DrawSoundGlyph(widgets, icon);
            else if (k == Kind::Particle) DrawParticleGlyph(widgets, icon);
            else DrawDocGlyph(widgets, icon, KindColor(k));
            DrawTypeBadge(widgets, icon, k);
        }

        void HandleHotkeys(UIContext& context, ui::UIWidgets& widgets) {
            if (context.renamingPath.empty() && widgets.KeyPressed(SDL_SCANCODE_DELETE)) {
                DeleteSelected(context);
            }
            if (context.renamingPath.empty() && widgets.KeyPressed(SDL_SCANCODE_F2) &&
                context.selectedAssets.size() == 1) {
                StartRename(context, context.selectedAssets.front());
            }
            if (!context.selectedAssets.empty() && widgets.Ctrl() &&
                widgets.KeyPressed(SDL_SCANCODE_C)) {
                context.clipboardPaths = context.selectedAssets;
                context.isCut = false;
            }
            if (!context.selectedAssets.empty() && widgets.Ctrl() &&
                widgets.KeyPressed(SDL_SCANCODE_X)) {
                context.clipboardPaths = context.selectedAssets;
                context.isCut = true;
            }
            if (widgets.Ctrl() && widgets.KeyPressed(SDL_SCANCODE_V) &&
                !context.clipboardPaths.empty()) {
                PasteCopiedItems(context, context.currentDirectory);
            }
            if (widgets.Ctrl() && widgets.KeyPressed(SDL_SCANCODE_D) &&
                context.selectedAssets.size() == 1) {
                DuplicatePath(context, context.selectedAssets.front());
            }
        }

        void DeleteSelected(UIContext& context) {
            for (const auto& p : context.selectedAssets) {
                std::error_code ec;
                fs::remove_all(p, ec);
            }
            context.selectedAssets.clear();
            context.renamingPath.clear();
            m_LastClickedIndex = -1;
        }

        fs::path UniquePath(const fs::path& dir, const std::string& base, const std::string& ext) {
            fs::path result = dir / (base + ext);
            uint32_t suffix = 1;
            while (fs::exists(result)) {
                result = dir / (base + " " + std::to_string(suffix++) + ext);
            }
            return result;
        }

        void StartRename(UIContext& context, const std::string& path) {
            context.renamingPath = path;
            m_RenameBuffer = fs::path(path).stem().string();
        }

        void ApplyRename(UIContext& context) {
            if (context.renamingPath.empty() || m_RenameBuffer.empty()) {
                context.renamingPath.clear();
                return;
            }
            fs::path oldPath(context.renamingPath);
            fs::path newPath = oldPath.parent_path() / (m_RenameBuffer + oldPath.extension().string());
            std::error_code ec;
            if (oldPath != newPath && !fs::exists(newPath)) fs::rename(oldPath, newPath, ec);
            if (!ec) {
                for (auto& selected : context.selectedAssets) {
                    if (selected == context.renamingPath) selected = newPath.string();
                }
            }
            context.renamingPath.clear();
        }

        void DuplicatePath(UIContext& context, const std::string& source) {
            fs::path src(source);
            if (!fs::exists(src)) return;
            fs::path dst = UniquePath(src.parent_path(), src.stem().string(), src.extension().string());
            std::error_code ec;
            fs::copy(src, dst, fs::copy_options::recursive, ec);
            if (!ec) {
                context.selectedAssets = {dst.string()};
                StartRename(context, dst.string());
            }
        }

        void PasteCopiedItems(UIContext& context, const fs::path& targetDir) {
            for (const auto& source : context.clipboardPaths) {
                fs::path src(source);
                if (!fs::exists(src)) continue;
                fs::path dst = UniquePath(targetDir, src.stem().string(), src.extension().string());
                std::error_code ec;
                if (context.isCut) fs::rename(src, dst, ec);
                else fs::copy(src, dst, fs::copy_options::recursive, ec);
            }
            if (context.isCut) {
                context.clipboardPaths.clear();
                context.isCut = false;
            }
        }

        bool IsDescendantOf(const fs::path& child, const fs::path& ancestor) {
            std::error_code ec;
            auto rel = fs::relative(child, ancestor, ec);
            if (ec) return false;
            std::string s = rel.generic_string();
            return !s.empty() && s != "." && !s.starts_with("..");
        }

        void MoveInto(UIContext& context, const fs::path& destDir, const std::string& draggedPath) {
            if (!fs::is_directory(destDir)) return;
            std::vector<std::string> toMove = context.selectedAssets;
            if (std::find(toMove.begin(), toMove.end(), draggedPath) == toMove.end()) {
                toMove = {draggedPath};
            }
            for (const auto& srcStr : toMove) {
                fs::path src(srcStr);
                if (!fs::exists(src)) continue;
                if (src == destDir) continue;
                if (fs::is_directory(src) && IsDescendantOf(destDir, src)) continue;
                fs::path dst = UniquePath(destDir, src.stem().string(), src.extension().string());
                if (src.filename() == dst.filename() && src.parent_path() == destDir) continue;
                std::error_code ec;
                fs::rename(src, dst, ec);
            }
            context.selectedAssets.clear();
        }

        void NavigateTo(UIContext& context, const fs::path& target) {
            m_ActiveCollection = -1;
            if (context.currentDirectory == target) return;
            if (context.dirHistoryIndex < static_cast<int>(context.dirHistory.size()) - 1) {
                context.dirHistory.erase(context.dirHistory.begin() + context.dirHistoryIndex + 1, context.dirHistory.end());
            }
            context.dirHistory.push_back(target);
            context.dirHistoryIndex++;
            context.currentDirectory = target;
            context.selectedAssets.clear();
            m_LastClickedIndex = -1;
        }

        void CreateFolder(UIContext& context) {
            fs::path newPath = UniquePath(context.currentDirectory, "New Folder", "");
            std::error_code ec;
            fs::create_directory(newPath, ec);
            if (ec) return;
            context.selectedAssets = {newPath.string()};
            StartRename(context, newPath.string());
        }

        void CreateMaterial(UIContext& context) {
            fs::path newPath = UniquePath(context.currentDirectory, "New Material", ".bhmat");
            json j;
            j["name"] = "New Material";
            std::ofstream file(newPath);
            if (!file) return;
            file << j.dump(4);
            context.selectedAssets = {newPath.string()};
            StartRename(context, newPath.string());
        }

        void CreateScene(UIContext& context) {
            fs::path newPath = UniquePath(context.currentDirectory, "New Scene", ".bhscene");
            std::ofstream file(newPath);
            if (!file) return;
            file << "{\"version\":1,\"entities\":[]}";
            context.selectedAssets = {newPath.string()};
            StartRename(context, newPath.string());
        }

        void DrawCreateItems(UIContext& context, ui::UIWidgets& widgets) {
            if (widgets.MenuItem("New Folder")) CreateFolder(context);
            if (widgets.MenuItem("Material (.bhmat)")) CreateMaterial(context);
            if (widgets.MenuItem("Scene (.bhscene)")) CreateScene(context);
        }

        void ImportAsset(UIContext& context) {
            auto picked = ui::NativeDialogs::OpenFile({
                {"3D Models", "fbx,obj,gltf,glb,bhmesh,bhmodel"},
                {"Textures", "png,jpg,jpeg,tga,bhtex"},
                {"Materials", "bhmat,json"},
                {"Audio", "wav,ogg,mp3,flac"},
            }, context.currentDirectory.string());
            if (!picked) return;
            fs::path src(*picked);
            std::string ext = ToLower(src.extension().string());
            fs::path dst = UniquePath(context.currentDirectory, src.stem().string(), src.extension().string());
            std::error_code ec;
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") {
                fs::path cooked = UniquePath(context.currentDirectory, src.stem().string(), ".bhmesh");
                ModelImporter::ImportModel(src.string(), cooked.string());
                if (fs::exists(cooked)) dst = cooked;
            }
            if (fs::exists(dst)) context.selectedAssets = {dst.string()};
        }

        void AddFavorite(const std::string& path) {
            if (std::find(m_Favorites.begin(), m_Favorites.end(), path) == m_Favorites.end()) {
                m_Favorites.push_back(path);
                m_SidecarDirty = true;
            }
        }

        void NewCollectionFromSelection(UIContext& context) {
            Collection col;
            col.name = "Collection " + std::to_string(m_Collections.size() + 1);
            col.paths = context.selectedAssets;
            if (col.paths.empty() && !m_ContextPath.empty()) col.paths = {m_ContextPath};
            m_Collections.push_back(std::move(col));
            m_SidecarDirty = true;
        }

        void DragSplitter(ui::UIWidgets& widgets, ui::Rect handle, float& width, bool& dragging,
                          float origin, float minW, float maxW, bool fromRight = false) {
            widgets.InvisibleHit(fromRight ? "##cb_split_r" : "##cb_split", handle);
            bool hovered = widgets.IsMouseOverItem() || dragging;
            if (hovered) widgets.RequestCursor(ui::UIWidgets::MouseCursor::EwResize);
            widgets.Background({handle.x + handle.w * 0.5f - 1.0f, handle.y, 2.0f, handle.h},
                               hovered ? ui::kTheme.splitterHover : ui::kTheme.splitter);
            if (widgets.IsMouseOverItem() && widgets.MouseDown(0) && !widgets.IsDragDropActive()) {
                dragging = true;
            }
            if (dragging && widgets.MouseDown(0)) {
                widgets.RequestCursor(ui::UIWidgets::MouseCursor::EwResize);
                if (fromRight) width = ui::Clamp(origin - widgets.MousePos().x, minW, maxW);
                else width = ui::Clamp(widgets.MousePos().x - origin, minW, maxW);
            }
            if (!widgets.MouseDown(0)) dragging = false;
        }

        void AcceptFolderDrop(UIContext& context, ui::UIWidgets& widgets, const fs::path& destDir) {
            if (widgets.BeginDragDropTarget()) {
                if (const auto* payload = widgets.AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    if (const std::string* path = std::any_cast<std::string>(payload)) {
                        MoveInto(context, destDir, *path);
                    }
                }
                widgets.EndDragDropTarget();
            }
        }

        bool FilterChip(ui::UIWidgets& widgets, const char* label, Kind kind) {
            bool on = m_Filter == kind;
            ui::Color bg = on ? C::kAccent : C::kChip;
            ui::Color hover = on ? C::kAccent : ui::kTheme.buttonHover;
            ui::Color active = C::kAccent;
            ui::Color text = on ? C::kText : C::kMuted;
            if (widgets.ButtonColored(label, {0, 24}, bg, hover, active, text)) {
                m_Filter = kind;
                return true;
            }
            return false;
        }

        bool TinyIcon(ui::UIWidgets& widgets, std::string_view id, ui::Rect r, ui::Color glyph, const char* tip) {
            bool clicked = widgets.InvisibleHit(id, r);
            bool hot = widgets.IsMouseOverItem();
            widgets.Background(r, hot ? ui::kTheme.buttonHover : ui::Color::RGBA8(16, 18, 20, 220), 4.0f);
            widgets.Background({r.x + 4.0f, r.y + 4.0f, r.w - 8.0f, r.h - 8.0f}, glyph, 2.0f);
            if (hot) widgets.SetTooltip(tip);
            return clicked;
        }

        void DrawToolbar(UIContext& context, ui::UIWidgets& widgets, ui::Rect toolbar) {
            widgets.Background(toolbar, C::kSidebar, 6.0f);
            widgets.SetCursor({toolbar.x + 8.0f, toolbar.y + 4.0f});

            if (widgets.Button("Import", {70, 26})) ImportAsset(context);
            widgets.SameLine(6.0f);
            if (widgets.Button("Save All", {78, 26})) context.requestSaveScene = true;
            widgets.SameLine(8.0f);
            if (widgets.ButtonColored("+", {24, 26}, C::kAdd, C::kAddHover, ui::Color::RGBA8(40, 140, 70), C::kText)) {
                widgets.OpenPopup("CreateMenuPopup");
            }
            widgets.SameLine(4.0f);
            if (widgets.ButtonColored("Add", {44, 26}, C::kChip, ui::kTheme.buttonHover, ui::kTheme.buttonActive, C::kText)) {
                widgets.OpenPopup("CreateMenuPopup");
            }

            widgets.SameLine(12.0f);
            if (widgets.Button("Filters", {64, 26})) widgets.OpenPopup("CB_Filters");

            widgets.SameLine(8.0f);
            FilterChip(widgets, "All", Kind::All);
            widgets.SameLine(4.0f);
            FilterChip(widgets, "Scene", Kind::Scene);
            widgets.SameLine(4.0f);
            FilterChip(widgets, "Mesh", Kind::Mesh);
            widgets.SameLine(4.0f);
            FilterChip(widgets, "Material", Kind::Material);
            widgets.SameLine(4.0f);
            FilterChip(widgets, "Sound", Kind::Sound);
            widgets.SameLine(4.0f);
            FilterChip(widgets, "Particle", Kind::Particle);
            widgets.SameLine(4.0f);
            FilterChip(widgets, "Texture", Kind::Texture);

            const float searchW = ui::Clamp(toolbar.w * 0.22f, 160.0f, 260.0f);
            const float sliderW = 88.0f;
            float right = toolbar.x + toolbar.w - 8.0f;
            float viewX = right - 70.0f;
            float sliderX = viewX - sliderW - 78.0f;
            float searchX = sliderX - searchW - 10.0f;

            std::string folder = context.currentDirectory.filename().string();
            if (folder.empty() || context.currentDirectory == context.projectDirectory) folder = "Project";
            std::string placeholder = "Search " + folder + "...";

            widgets.SetCursor({searchX, toolbar.y + 4.0f});
            widgets.InputText(placeholder, m_Search, 128, {searchW, 26});

            widgets.SetCursor({sliderX, toolbar.y + 6.0f});
            widgets.Text("Grid Size", C::kMuted);
            widgets.SameLine(6.0f);
            widgets.SetCursor({sliderX + 70.0f, toolbar.y + 10.0f});
            widgets.SliderFloat("##zoom", &m_ThumbnailSize, 48.0f, 176.0f, {sliderW, 14.0f});

            widgets.SetCursor({viewX, toolbar.y + 4.0f});
            ui::Color gridBg = !m_ListView ? C::kAccent : C::kChip;
            ui::Color listBg = m_ListView ? C::kAccent : C::kChip;
            if (widgets.ButtonColored("Grid", {36, 26}, gridBg, ui::kTheme.buttonHover, C::kAccent, C::kText))
                m_ListView = false;
            widgets.SameLine(4.0f);
            if (widgets.ButtonColored("List", {32, 26}, listBg, ui::kTheme.buttonHover, C::kAccent, C::kText))
                m_ListView = true;

            if (ui::Popup create(widgets, "CreateMenuPopup"); create) {
                DrawCreateItems(context, widgets);
            }
            if (ui::Popup filters(widgets, "CB_Filters"); filters) {
                if (widgets.MenuItem("All", "", true, m_Filter == Kind::All)) m_Filter = Kind::All;
                if (widgets.MenuItem("Scene", "", true, m_Filter == Kind::Scene)) m_Filter = Kind::Scene;
                if (widgets.MenuItem("Mesh", "", true, m_Filter == Kind::Mesh)) m_Filter = Kind::Mesh;
                if (widgets.MenuItem("Material", "", true, m_Filter == Kind::Material)) m_Filter = Kind::Material;
                if (widgets.MenuItem("Texture", "", true, m_Filter == Kind::Texture)) m_Filter = Kind::Texture;
                if (widgets.MenuItem("Sound", "", true, m_Filter == Kind::Sound)) m_Filter = Kind::Sound;
                if (widgets.MenuItem("Particle", "", true, m_Filter == Kind::Particle)) m_Filter = Kind::Particle;
            }
        }

        void DrawBreadcrumbs(UIContext& context, ui::UIWidgets& widgets, ui::Rect bar) {
            widgets.Background(bar, C::kGrid, 4.0f);
            widgets.PushID("PathBar");
            widgets.SetCursor({bar.x + 8.0f, bar.y + 1.0f});

            const bool canBack = context.dirHistoryIndex > 0;
            const bool canFwd = context.dirHistoryIndex < static_cast<int>(context.dirHistory.size()) - 1;
            ui::Color backBg = canBack ? C::kChip : C::kField;
            if (widgets.ButtonColored("<", {24, 22}, backBg, ui::kTheme.buttonHover, C::kAccent, C::kText) && canBack) {
                context.dirHistoryIndex--;
                context.currentDirectory = context.dirHistory[context.dirHistoryIndex];
                context.selectedAssets.clear();
                m_LastClickedIndex = -1;
                m_ActiveCollection = -1;
            }
            widgets.SameLine(4.0f);
            ui::Color fwdBg = canFwd ? C::kChip : C::kField;
            if (widgets.ButtonColored(">", {24, 22}, fwdBg, ui::kTheme.buttonHover, C::kAccent, C::kText) && canFwd) {
                context.dirHistoryIndex++;
                context.currentDirectory = context.dirHistory[context.dirHistoryIndex];
                context.selectedAssets.clear();
                m_LastClickedIndex = -1;
                m_ActiveCollection = -1;
            }

            widgets.SameLine(10.0f);
            auto crumb = [&](const char* name, const fs::path& target, bool last) {
                ui::Color col = last ? C::kText : C::kMuted;
                if (widgets.ButtonColored(name, {0, 22}, {0, 0, 0, 0}, C::kCardHover, C::kAccentSoft, col)) {
                    NavigateTo(context, target);
                }
                AcceptFolderDrop(context, widgets, target);
            };

            if (m_ActiveCollection >= 0 && m_ActiveCollection < static_cast<int>(m_Collections.size())) {
                crumb("Collections", context.projectDirectory, false);
                widgets.SameLine(4.0f);
                widgets.Text(">", C::kMuted);
                widgets.SameLine(4.0f);
                crumb(m_Collections[m_ActiveCollection].name.c_str(), context.currentDirectory, true);
                widgets.PopID();
                return;
            }

            crumb("All", context.projectDirectory, context.currentDirectory == context.projectDirectory);
            AcceptFolderDrop(context, widgets, context.projectDirectory);

            std::error_code ec;
            fs::path rel = fs::relative(context.currentDirectory, context.projectDirectory, ec);
            const bool inside = !ec && (rel.empty() || IsDescendantOf(context.currentDirectory, context.projectDirectory)
                                        || rel.generic_string() == ".");
            if (inside && !rel.empty() && rel.generic_string() != ".") {
                fs::path accum = context.projectDirectory;
                for (const auto& part : rel) {
                    std::string name = part.generic_string();
                    if (name.empty() || name == "." || name == ".." || name == "...") continue;
                    accum /= part;
                    widgets.SameLine(4.0f);
                    widgets.Text(">", C::kMuted);
                    widgets.SameLine(4.0f);
                    bool last = accum == context.currentDirectory;
                    crumb(name.c_str(), accum, last);
                }
            }
            widgets.PopID();
        }

        bool SectionHeader(ui::UIWidgets& widgets, const char* label, bool& open) {
            glm::vec2 origin = widgets.GetCursor();
            float w = widgets.ContentAvail().x;
            ui::Rect row{origin.x, origin.y, w, 22.0f};
            bool clicked = widgets.InvisibleHit(label, row);
            if (clicked) open = !open;
            widgets.SetCursor({origin.x + 4.0f, origin.y});
            widgets.Text(open ? "v" : ">", C::kMuted);
            widgets.SameLine(6.0f);
            widgets.Text(label, C::kMuted);
            return open;
        }

        void DrawSidebarRow(UIContext& context, ui::UIWidgets& widgets, const std::string& id,
                            const std::string& label, int depth, bool selected, bool hasChildren,
                            bool& open, bool isFolder, const fs::path& path) {
            glm::vec2 origin = widgets.GetCursor();
            float w = widgets.ContentAvail().x;
            ui::Rect row{origin.x, origin.y, w, 22.0f};
            bool clicked = widgets.InvisibleHit(id, row);
            bool hovered = widgets.IsMouseOverItem();
            if (selected) widgets.Background(row, C::kAccentSoft, 4.0f);
            else if (hovered) widgets.Background(row, ui::kTheme.rowHover, 4.0f);

            float x = origin.x + 6.0f + depth * 14.0f;
            if (hasChildren) {
                ui::Rect chev{x, origin.y, 14.0f, 22.0f};
                bool onChev = chev.Contains(widgets.MousePos().x, widgets.MousePos().y);
                if (clicked && onChev) {
                    open = !open;
                    clicked = false;
                }
                widgets.SetCursor({x, origin.y});
                widgets.TextClipped(open ? "v" : ">", 14.0f, C::kMuted);
            }
            x += 14.0f;
            ui::Rect icon{x, origin.y + 3.0f, 16.0f, 16.0f};
            if (isFolder) DrawFolderGlyph(widgets, icon);
            else widgets.Background(icon, KindColor(Classify(path, false)), 3.0f);
            widgets.SetCursor({x + 20.0f, origin.y});
            widgets.TextClipped(label, std::max(20.0f, w - (x + 20.0f - origin.x) - 4.0f),
                                selected ? C::kText : C::kText);

            if (clicked) {
                if (fs::is_directory(path)) NavigateTo(context, path);
                else {
                    context.selectedAssets = {path.string()};
                    if (fs::exists(path.parent_path())) NavigateTo(context, path.parent_path());
                    context.selectedAssets = {path.string()};
                }
            }
            if (widgets.WasItemRightClicked()) {
                m_ContextPath = path.string();
                m_ContextIsDir = fs::is_directory(path);
                m_WantContext = true;
                context.selectedAssets = {path.string()};
            }
            if (fs::is_directory(path)) {
                if (const auto* payload = widgets.AcceptDragDropOnRect("CONTENT_BROWSER_ITEM", row)) {
                    if (const std::string* p = std::any_cast<std::string>(payload)) {
                        MoveInto(context, path, *p);
                    }
                }
            }
        }

        void DrawFolderTree(UIContext& context, ui::UIWidgets& widgets, const fs::path& dir, int depth) {
            std::error_code ec;
            std::vector<fs::directory_entry> dirs;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_directory()) dirs.push_back(entry);
            }
            std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
                return a.path().filename().string() < b.path().filename().string();
            });

            std::string key = dir.string();
            bool hasChildDir = !dirs.empty();
            bool selected = context.currentDirectory == dir && m_ActiveCollection < 0;
            bool open = m_OpenFolders.contains(key);
            if (depth == 0) open = true;
            std::string name = (dir == context.projectDirectory) ? context.projectDirectory.filename().string()
                                                                 : dir.filename().string();
            if (name.empty()) name = "Project";

            DrawSidebarRow(context, widgets, key, name, depth, selected, hasChildDir, open, true, dir);
            if (open || depth == 0) m_OpenFolders.insert(key);
            else m_OpenFolders.erase(key);

            if (open && hasChildDir) {
                for (const auto& entry : dirs) DrawFolderTree(context, widgets, entry.path(), depth + 1);
            }
        }

        void DrawSidebar(UIContext& context, ui::UIWidgets& widgets) {
            if (SectionHeader(widgets, "SOURCES", m_SourcesOpen)) {
                DrawFolderTree(context, widgets, context.projectDirectory, 0);
            }
            widgets.Dummy({1.0f, 6.0f});
            if (SectionHeader(widgets, "FAVORITES", m_FavoritesOpen)) {
                if (m_Favorites.empty()) {
                    widgets.Text("  Right-click to pin", C::kMuted);
                }
                for (int i = 0; i < static_cast<int>(m_Favorites.size()); ++i) {
                    fs::path p(m_Favorites[i]);
                    bool exists = fs::exists(p);
                    std::string name = p.filename().string();
                    if (name.empty()) name = p.string();
                    bool dummyOpen = false;
                    ui::ID id(widgets, "fav" + std::to_string(i));
                    DrawSidebarRow(context, widgets, m_Favorites[i], name, 0, false, false,
                                   dummyOpen, exists && fs::is_directory(p), p);
                }
            }
            widgets.Dummy({1.0f, 6.0f});
            if (SectionHeader(widgets, "COLLECTIONS", m_CollectionsOpen)) {
                if (m_Collections.empty()) {
                    widgets.Text("  Empty", C::kMuted);
                }
                for (int i = 0; i < static_cast<int>(m_Collections.size()); ++i) {
                    glm::vec2 origin = widgets.GetCursor();
                    float w = widgets.ContentAvail().x;
                    ui::Rect row{origin.x, origin.y, w, 22.0f};
                    bool selected = m_ActiveCollection == i;
                    bool clicked = widgets.InvisibleHit("col" + std::to_string(i), row);
                    if (selected) widgets.Background(row, C::kAccentSoft, 4.0f);
                    else if (widgets.IsMouseOverItem()) widgets.Background(row, ui::kTheme.rowHover, 4.0f);
                    widgets.Background({origin.x + 8.0f, origin.y + 6.0f, 10.0f, 10.0f}, C::kAccent, 2.0f);
                    widgets.SetCursor({origin.x + 24.0f, origin.y});
                    widgets.TextClipped(m_Collections[i].name, w - 28.0f, C::kText);
                    if (clicked) {
                        m_ActiveCollection = i;
                        context.selectedAssets.clear();
                        m_LastClickedIndex = -1;
                    }
                }
            }
        }

        void CollectSearchItems(const fs::path& root, const std::string& query,
                                std::vector<fs::directory_entry>& out) {
            std::error_code ec;
            fs::recursive_directory_iterator it(
                root, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            for (; it != end; it.increment(ec)) {
                if (ec) { ec.clear(); continue; }
                const auto& entry = *it;
                if (entry.path() == root) continue;
                std::string name = ToLower(entry.path().filename().string());
                std::string stem = ToLower(entry.path().stem().string());
                if (name.find(query) != std::string::npos || stem.find(query) != std::string::npos) {
                    out.push_back(entry);
                }
            }
        }

        std::vector<fs::directory_entry> CollectItems(UIContext& context) {
            std::vector<fs::directory_entry> items;
            std::error_code ec;
            if (m_ActiveCollection >= 0 && m_ActiveCollection < static_cast<int>(m_Collections.size())) {
                for (const auto& p : m_Collections[m_ActiveCollection].paths) {
                    if (fs::exists(p)) items.emplace_back(p);
                }
            } else {
                std::string query = ToLower(m_Search);
                if (!query.empty()) CollectSearchItems(context.currentDirectory, query, items);
                else {
                    for (const auto& entry : fs::directory_iterator(context.currentDirectory, ec)) {
                        items.push_back(entry);
                    }
                }
            }
            if (m_Filter != Kind::All) {
                items.erase(std::remove_if(items.begin(), items.end(), [&](const fs::directory_entry& e) {
                    return Classify(e.path(), e.is_directory()) != m_Filter;
                }), items.end());
            }
            std::sort(items.begin(), items.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                if (a.is_directory() != b.is_directory()) return a.is_directory();
                return a.path().filename().string() < b.path().filename().string();
            });
            return items;
        }

        void ApplyClickSelection(UIContext& context, ui::UIWidgets& widgets,
                                 const std::vector<fs::directory_entry>& items,
                                 int index, const std::string& pathStr) {
            if (widgets.Shift() && m_LastClickedIndex >= 0 && !items.empty()) {
                int a = std::min(m_LastClickedIndex, index);
                int b = std::max(m_LastClickedIndex, index);
                a = std::max(0, a);
                b = std::min(static_cast<int>(items.size()) - 1, b);
                context.selectedAssets.clear();
                for (int i = a; i <= b; ++i) {
                    context.selectedAssets.push_back(items[i].path().string());
                }
            } else if (widgets.Ctrl()) {
                auto it = std::find(context.selectedAssets.begin(), context.selectedAssets.end(), pathStr);
                if (it != context.selectedAssets.end()) context.selectedAssets.erase(it);
                else context.selectedAssets.push_back(pathStr);
                m_LastClickedIndex = index;
            } else {
                context.selectedAssets = {pathStr};
                m_LastClickedIndex = index;
            }
        }

        void OpenAsset(UIContext& context, const fs::directory_entry& item) {
            std::string pathStr = item.path().string();
            std::string ext = ToLower(item.path().extension().string());
            if (item.is_directory()) NavigateTo(context, item.path());
            else if (ext == ".bhscene" || ext == ".json") context.pendingSceneLoadPath = pathStr;
            else if (ext == ".bhmat") context.requestActivateWindow = "Material Editor";
        }

        void HandleCellInput(UIContext& context, ui::UIWidgets& widgets,
                             const std::vector<fs::directory_entry>& items, int i,
                             const std::string& pathStr, bool isDir, bool selected,
                             bool& hitCell) {
            if (widgets.WasItemClicked()) {
                hitCell = true;
                if (widgets.Shift() || widgets.Ctrl() || !selected) {
                    ApplyClickSelection(context, widgets, items, i, pathStr);
                    m_PendingSingleSelect.clear();
                } else {
                    m_LastClickedIndex = i;
                    m_PendingSingleSelect = pathStr;
                }
                std::string ext = ToLower(items[i].path().extension().string());
                m_OpenMatEditorOnRelease = (ext == ".bhmat" || ext == ".json");
            }
            if (widgets.IsItemDoubleClicked()) {
                hitCell = true;
                m_PendingSingleSelect.clear();
                OpenAsset(context, items[i]);
            }
            if (widgets.WasItemRightClicked()) {
                hitCell = true;
                m_PendingSingleSelect.clear();
                if (!selected) {
                    context.selectedAssets = {pathStr};
                    m_LastClickedIndex = i;
                }
                m_ContextPath = pathStr;
                m_ContextIsDir = isDir;
                m_WantContext = true;
            }
            if (widgets.BeginDragDropSource()) {
                m_OpenMatEditorOnRelease = false;
                m_PendingSingleSelect.clear();
                widgets.SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr);
                widgets.EndDragDropSource();
            }
            if (isDir && widgets.BeginDragDropTarget()) {
                if (const auto* payload = widgets.AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    if (const std::string* path = std::any_cast<std::string>(payload)) {
                        MoveInto(context, items[i].path(), *path);
                    }
                }
                widgets.EndDragDropTarget();
            }
        }

        void FinishGridInput(UIContext& context, ui::UIWidgets& widgets, bool hitCell, bool bgHovered,
                             bool renameFieldHovered) {
            if (m_OpenMatEditorOnRelease && widgets.MouseReleased(0) && !widgets.IsDragDropActive()) {
                context.requestActivateWindow = "Material Editor";
                m_OpenMatEditorOnRelease = false;
            }
            if (!m_PendingSingleSelect.empty() && widgets.MouseReleased(0) && !widgets.IsDragDropActive()) {
                context.selectedAssets = {m_PendingSingleSelect};
                m_PendingSingleSelect.clear();
            }
            if (!widgets.MouseDown(0)) {
                m_OpenMatEditorOnRelease = false;
                m_PendingSingleSelect.clear();
            }
            if (!hitCell && bgHovered && widgets.MouseClicked(0) && !widgets.IsDragDropActive()) {
                context.selectedAssets.clear();
                m_LastClickedIndex = -1;
            }
            if (!context.renamingPath.empty() && widgets.MouseClicked(0) && !renameFieldHovered) {
                ApplyRename(context);
            }
        }

        void DrawGrid(UIContext& context, ui::UIWidgets& widgets, ui::Rect gridRect) {
            auto items = CollectItems(context);
            m_LastItemCount = static_cast<int>(items.size());

            widgets.InvisibleHit("##grid_bg", gridRect);
            const bool bgHovered = widgets.IsMouseOverItem();
            bool hitCell = false;
            bool renameFieldHovered = false;

            if (widgets.WasItemRightClicked() && bgHovered) {
                m_ContextPath.clear();
                m_ContextIsDir = false;
                m_WantContext = true;
            }

            m_ThumbnailSize = ui::Clamp(m_ThumbnailSize, 48.0f, 176.0f);
            glm::vec2 avail = widgets.ContentAvail();
            const float cellW = m_ThumbnailSize + 20.0f;
            const float cellH = m_ThumbnailSize + 44.0f;
            const float gap = 10.0f;
            const int cols = std::max(1, static_cast<int>((avail.x + gap) / (cellW + gap)));
            glm::vec2 origin = widgets.GetCursor();
            ui::Rect clip = widgets.ClipRect();
            int col = 0;
            int row = 0;

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                const auto& item = items[i];
                std::string pathStr = item.path().string();
                bool isDir = item.is_directory();
                bool selected = std::find(context.selectedAssets.begin(), context.selectedAssets.end(), pathStr)
                    != context.selectedAssets.end();
                Kind kind = Classify(item.path(), isDir);

                float x = origin.x + col * (cellW + gap);
                float y = origin.y + row * (cellH + gap);
                ui::Rect cell{x, y, cellW, cellH};

                if (cell.Intersect(clip).h > 1.0f && cell.Intersect(clip).w > 1.0f) {
                    widgets.PushID(pathStr);
                    bool renaming = context.renamingPath == pathStr;
                    ui::Rect icon{x + 8.0f, y + 6.0f, cellW - 16.0f, m_ThumbnailSize};
                    ui::Rect nameRect{x + 6.0f, y + 8.0f + m_ThumbnailSize, cellW - 12.0f, 18.0f};
                    ui::Rect typeRect{x + 6.0f, y + 24.0f + m_ThumbnailSize, cellW - 12.0f, 16.0f};

                    bool hovered = false;
                    if (renaming) {
                        widgets.SetCursor({nameRect.x, nameRect.y});
                        widgets.InputText("##rename", m_RenameBuffer, 128, {nameRect.w, 18});
                        renameFieldHovered = widgets.IsMouseOverItem();
                        if (widgets.KeyPressed(SDL_SCANCODE_RETURN)) ApplyRename(context);
                        if (widgets.KeyPressed(SDL_SCANCODE_ESCAPE)) context.renamingPath.clear();
                    } else {
                        widgets.InvisibleHit("##cell", cell);
                        hovered = widgets.IsMouseOverItem();
                        if (hovered) hitCell = true;
                        HandleCellInput(context, widgets, items, i, pathStr, isDir, selected, hitCell);
                    }

                    const glm::vec2 mouse = widgets.MousePos();
                    bool cellHot = cell.Contains(mouse.x, mouse.y) && clip.Contains(mouse.x, mouse.y);
                    if (cellHot) {
                        hovered = true;
                        hitCell = true;
                        widgets.SetTooltip(item.path().filename().string());
                    }

                    if (selected) {
                        widgets.Background(cell, C::kAccent, 8.0f);
                        widgets.Background({cell.x + 2.0f, cell.y + 2.0f, cell.w - 4.0f, cell.h - 4.0f}, C::kCard, 6.0f);
                        widgets.Background(icon, C::kAccentSoft, 6.0f);
                    } else {
                        widgets.Background(cell, hovered ? C::kCardHover : C::kCard, 8.0f);
                    }
                    DrawFileIcon(context, widgets, icon, item.path(), isDir);

                    if ((hovered || selected) && !renaming) {
                        float ax = cell.x + cell.w - 24.0f;
                        float ay = cell.y + 8.0f;
                        ui::Rect e{ax, ay, 18.0f, 18.0f};
                        ui::Rect f{ax, ay + 20.0f, 18.0f, 18.0f};
                        ui::Rect u{ax, ay + 40.0f, 18.0f, 18.0f};
                        ui::Rect n{ax, ay + 60.0f, 18.0f, 18.0f};
                        if (TinyIcon(widgets, "##e", e, C::kAccent, "Edit")) OpenAsset(context, item);
                        if (TinyIcon(widgets, "##f", f, C::kFolder, "Browse to folder")) {
                            if (isDir) NavigateTo(context, item.path());
                            else NavigateTo(context, item.path().parent_path());
                        }
                        if (TinyIcon(widgets, "##u", u, C::kAdd, "Add to Favorites")) AddFavorite(pathStr);
                        if (TinyIcon(widgets, "##n", n, C::kMuted, "Details")) {
                            context.selectedAssets = {pathStr};
                        }
                    }

                    if (!renaming) {
                        widgets.SetCursor({nameRect.x, nameRect.y});
                        widgets.TextClippedCentered(DisplayName(item.path(), isDir), nameRect.w, C::kText);
                        widgets.SetCursor({typeRect.x, typeRect.y});
                        std::string type = std::string("(") + KindLabel(kind) + ")";
                        widgets.TextClippedCentered(type, typeRect.w, C::kMuted);
                    }

                    widgets.PopID();
                }

                ++col;
                if (col >= cols) { col = 0; ++row; }
            }
            int usedRows = row + (col > 0 ? 1 : 0);
            widgets.SetCursor({origin.x, origin.y + usedRows * (cellH + gap)});
            widgets.Dummy({1.0f, 4.0f});
            FinishGridInput(context, widgets, hitCell, bgHovered, renameFieldHovered);
        }

        void DrawList(UIContext& context, ui::UIWidgets& widgets, ui::Rect gridRect) {
            auto items = CollectItems(context);
            m_LastItemCount = static_cast<int>(items.size());
            widgets.InvisibleHit("##list_bg", gridRect);
            const bool bgHovered = widgets.IsMouseOverItem();
            bool hitCell = false;
            bool renameFieldHovered = false;
            if (widgets.WasItemRightClicked() && bgHovered) {
                m_ContextPath.clear();
                m_ContextIsDir = false;
                m_WantContext = true;
            }

            glm::vec2 origin = widgets.GetCursor();
            float w = widgets.ContentAvail().x;
            widgets.SetCursor({origin.x + 44.0f, origin.y});
            widgets.Text("Name", C::kMuted);
            widgets.SameLine(8.0f);
            widgets.SetCursor({origin.x + w * 0.55f, origin.y});
            widgets.Text("Type", C::kMuted);
            widgets.SameLine(8.0f);
            widgets.SetCursor({origin.x + w * 0.78f, origin.y});
            widgets.Text("Size", C::kMuted);

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                const auto& item = items[i];
                std::string pathStr = item.path().string();
                bool isDir = item.is_directory();
                bool selected = std::find(context.selectedAssets.begin(), context.selectedAssets.end(), pathStr)
                    != context.selectedAssets.end();
                Kind kind = Classify(item.path(), isDir);
                glm::vec2 rowOrigin = widgets.GetCursor();
                ui::Rect row{rowOrigin.x, rowOrigin.y, w, 36.0f};
                widgets.PushID(pathStr);
                bool renaming = context.renamingPath == pathStr;
                widgets.InvisibleHit("##row", row);
                bool hovered = widgets.IsMouseOverItem();
                if (hovered) hitCell = true;
                if (!renaming) HandleCellInput(context, widgets, items, i, pathStr, isDir, selected, hitCell);

                if (selected) widgets.Background(row, C::kAccentSoft, 4.0f);
                else if (hovered) widgets.Background(row, ui::kTheme.rowHover, 4.0f);

                ui::Rect icon{row.x + 6.0f, row.y + 4.0f, 28.0f, 28.0f};
                DrawFileIcon(context, widgets, icon, item.path(), isDir);

                if (renaming) {
                    widgets.SetCursor({row.x + 40.0f, row.y + 6.0f});
                    widgets.InputText("##rename", m_RenameBuffer, 128, {w * 0.4f, 22});
                    renameFieldHovered = widgets.IsMouseOverItem();
                    if (widgets.KeyPressed(SDL_SCANCODE_RETURN)) ApplyRename(context);
                    if (widgets.KeyPressed(SDL_SCANCODE_ESCAPE)) context.renamingPath.clear();
                } else {
                    widgets.SetCursor({row.x + 40.0f, row.y + 6.0f});
                    widgets.TextClipped(DisplayName(item.path(), isDir), w * 0.45f, C::kText);
                    widgets.SetCursor({row.x + w * 0.55f, row.y + 6.0f});
                    widgets.TextClipped(KindLabel(kind), w * 0.20f, C::kMuted);
                    std::error_code ec;
                    uintmax_t sz = isDir ? 0 : fs::file_size(item.path(), ec);
                    widgets.SetCursor({row.x + w * 0.78f, row.y + 6.0f});
                    widgets.TextClipped(isDir ? "—" : FormatBytes(sz), w * 0.20f, C::kMuted);
                }
                widgets.SetCursor({row.x, row.y});
                widgets.Dummy({w, 36.0f});
                widgets.PopID();
            }
            FinishGridInput(context, widgets, hitCell, bgHovered, renameFieldHovered);
        }

        int CountJsonDeps(const fs::path& path) {
            std::ifstream in(path);
            if (!in) return 0;
            json j;
            try { in >> j; } catch (...) { return 0; }
            int n = 0;
            auto walk = [&](auto& self, const json& node) -> void {
                if (node.is_string()) {
                    fs::path p(node.get<std::string>());
                    if (p.has_extension() && fs::exists(p)) ++n;
                } else if (node.is_object()) {
                    for (auto it = node.begin(); it != node.end(); ++it) self(self, it.value());
                } else if (node.is_array()) {
                    for (const auto& v : node) self(self, v);
                }
            };
            walk(walk, j);
            return n;
        }

        void DrawDetails(UIContext& context, ui::UIWidgets& widgets, ui::Rect) {
            if (context.selectedAssets.size() != 1) {
                widgets.Dummy({1.0f, 12.0f});
                widgets.Text(context.selectedAssets.empty() ? "No asset selected" : "Multiple assets", C::kMuted);
                return;
            }
            fs::path p(context.selectedAssets.front());
            bool isDir = fs::is_directory(p);
            Kind kind = Classify(p, isDir);
            glm::vec2 origin = widgets.GetCursor();
            float w = widgets.ContentAvail().x;

            ui::Rect thumb{origin.x + (w - 96.0f) * 0.5f, origin.y + 8.0f, 96.0f, 96.0f};
            DrawFileIcon(context, widgets, thumb, p, isDir);
            widgets.Dummy({1.0f, 110.0f});

            widgets.SetCursor({origin.x, origin.y + 112.0f});
            widgets.TextClippedCentered(DisplayName(p, isDir), w, C::kText);
            widgets.TextClippedCentered(KindLabel(kind), w, C::kMuted);
            widgets.Dummy({1.0f, 6.0f});

            std::error_code ec;
            std::string rel = fs::relative(p, context.projectDirectory, ec).generic_string();
            if (ec) rel = p.generic_string();
            widgets.Text("Path", C::kMuted);
            widgets.TextClipped(rel, w, C::kText);
            widgets.Dummy({1.0f, 4.0f});

            AssetMeta& meta = MetaFor(p.string());
            widgets.Text("Tags", C::kMuted);
            {
                std::string prev = meta.tags;
                widgets.InputText("##tags", meta.tags, 128, {w, 22});
                if (meta.tags != prev) m_SidecarDirty = true;
            }

            uintmax_t sz = isDir ? 0 : fs::file_size(p, ec);
            widgets.Dummy({1.0f, 6.0f});
            widgets.Text("Size", C::kMuted);
            widgets.SameLine(8.0f);
            widgets.Text(isDir ? "—" : FormatBytes(sz), C::kAccent);

            int deps = (kind == Kind::Material) ? CountJsonDeps(p) : 0;
            widgets.Text("References", C::kMuted);
            widgets.SameLine(8.0f);
            widgets.Text("—", C::kAccent);
            widgets.Text("Dependencies", C::kMuted);
            widgets.SameLine(8.0f);
            widgets.Text(std::to_string(deps), C::kAccent);

            widgets.Dummy({1.0f, 8.0f});
            widgets.Text("Description", C::kMuted);
            {
                std::string prev = meta.description;
                widgets.InputText("##desc", meta.description, 256, {w, 44});
                if (meta.description != prev) m_SidecarDirty = true;
            }
            widgets.Text("Usage", C::kMuted);
            {
                std::string prev = meta.usage;
                widgets.InputText("##usage", meta.usage, 256, {w, 44});
                if (meta.usage != prev) m_SidecarDirty = true;
            }
            widgets.Text("Notes", C::kMuted);
            {
                std::string prev = meta.notes;
                widgets.InputText("##notes", meta.notes, 256, {w, 44});
                if (meta.notes != prev) m_SidecarDirty = true;
            }
        }

        void DrawFooter(UIContext& context, ui::UIWidgets& widgets, ui::Rect footer, int assetCount) {
            widgets.Background(footer, C::kSidebar, 4.0f);
            widgets.Background({footer.x, footer.y, footer.w, 1.0f}, ui::kTheme.border);

            std::string left = std::to_string(assetCount) + (assetCount == 1 ? " Asset" : " Assets");
            if (context.selectedAssets.size() == 1) {
                fs::path p(context.selectedAssets.front());
                bool isDir = fs::is_directory(p);
                Kind k = Classify(p, isDir);
                std::error_code ec;
                uintmax_t sz = isDir ? 0 : fs::file_size(p, ec);
                left += "  |  Selected: " + DisplayName(p, isDir) + " (" + KindLabel(k);
                if (!isDir) left += ", " + FormatBytes(sz);
                left += ")";
            } else if (context.selectedAssets.size() > 1) {
                left += "  |  Selected: " + std::to_string(context.selectedAssets.size()) + " items";
            }

            widgets.SetCursor({footer.x + 10.0f, footer.y + 1.0f});
            widgets.TextClipped(left, std::max(40.0f, footer.w - 100.0f), C::kMuted);

            widgets.SetCursor({footer.x + footer.w - 84.0f, footer.y + 1.0f});
            if (widgets.Button("Refresh", {76, 22})) m_ImageThumbs.clear();
        }

        void DrawContextMenu(UIContext& context, ui::UIWidgets& widgets) {
            if (ui::Popup ctx(widgets, "CB_Context"); ctx) {
                DrawCreateItems(context, widgets);
                if (!m_ContextPath.empty()) {
                    widgets.Separator();
                    if (widgets.MenuItem("Rename", "F2")) StartRename(context, m_ContextPath);
                    if (widgets.MenuItem("Duplicate", "Ctrl+D")) DuplicatePath(context, m_ContextPath);
                    if (widgets.MenuItem("Copy", "Ctrl+C")) {
                        context.clipboardPaths = context.selectedAssets.empty()
                            ? std::vector<std::string>{m_ContextPath} : context.selectedAssets;
                        context.isCut = false;
                    }
                    if (widgets.MenuItem("Cut", "Ctrl+X")) {
                        context.clipboardPaths = context.selectedAssets.empty()
                            ? std::vector<std::string>{m_ContextPath} : context.selectedAssets;
                        context.isCut = true;
                    }
                    if (widgets.MenuItem("Add to Favorites")) AddFavorite(m_ContextPath);
                    if (widgets.MenuItem("New Collection from Selection")) NewCollectionFromSelection(context);
                    if (widgets.MenuItem("Delete", "Del")) {
                        if (context.selectedAssets.empty()) context.selectedAssets = {m_ContextPath};
                        DeleteSelected(context);
                    }
                }
                if (!context.clipboardPaths.empty()) {
                    widgets.Separator();
                    if (widgets.MenuItem("Paste", "Ctrl+V")) {
                        PasteCopiedItems(context, context.currentDirectory);
                    }
                }
            }
        }
    };
}
