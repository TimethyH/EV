#pragma once

/**
 *  @file editor.h
 *
 *  @brief ImGui based scene editor: a docked layout with a scene viewport, hierarchy,
 *  inspector, texture browser/viewer and stats. Any Game can use it:
 *
 *      m_editor = std::make_unique<Editor>(m_GUI);
 *      m_editor->AddScene("Helmet", m_helmet);
 *
 *      // Every frame, render the scene into your own texture, then:
 *      m_editor->BeginFrame();
 *      m_editor->SetSceneTexture(sceneTexture);
 *      ... your own ImGui windows ...
 *      m_editor->EndFrame(commandList, swapChainRenderTarget);
 */

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>

#include "imgui.h"

#include "UI/model_preview.h"

namespace EV
{
    class Camera;
    class CommandList;
    class GUI;
    class Material;
    class RenderTarget;
    class Scene;
    class SceneNode;
    class Texture;
    class TexturePreview;
    class TextureReadback;
    struct TexturePreviewSettings;

    class Editor
    {
    public:
        // Where an application panel is docked in the default layout.
        enum class DockSlot
        {
            Left,
            Right,
            Bottom,
            Center,
        };

        explicit Editor(std::shared_ptr<GUI> gui);
        ~Editor();

        Editor(const Editor&) = delete;
        Editor& operator=(const Editor&) = delete;

        /**
         * Start a new GUI frame and draw the dockspace and menu bar.
         * Application ImGui windows can be submitted between BeginFrame and EndFrame.
         */
        void BeginFrame();

        /**
         * Draw the editor panels, record texture previews/readbacks and render the GUI.
         */
        void EndFrame(const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget);

        // ----------------------------------------------------------------------------------------
        // Scene viewport

        // The image shown in the Scene panel (normally the tonemapped scene color).
        void SetSceneTexture(const std::shared_ptr<Texture>& texture);

        // Size of the Scene panel's image area in pixels. Render the scene at this size.
        uint32_t GetViewportWidth() const { return m_viewportWidth; }
        uint32_t GetViewportHeight() const { return m_viewportHeight; }

        // True once each time the viewport size changes. Resize render targets and the camera then.
        bool ConsumeViewportResize();

        bool IsViewportHovered() const { return m_viewportHovered; }
        bool IsViewportFocused() const { return m_viewportFocused; }

        // True while the mouse is over the viewport or a drag that started there is still held.
        // Use this to decide whether camera controls should react to the mouse.
        bool IsViewportMouseActive() const { return m_viewportHovered || m_viewportDragging; }

        // True when keyboard camera controls should react (viewport focused and no text field active).
        bool IsViewportKeyboardActive() const;

        // Mouse position relative to the top left of the viewport image, in pixels.
        ImVec2 GetViewportMousePosition() const { return m_viewportMousePosition; }

        // The camera used to render the viewport. Needed for picking and the transform gizmo.
        void SetCamera(const Camera* camera) { m_camera = camera; }

        // Near/far planes of the scene camera, used by the texture viewer to linearize depth.
        void SetCameraClipPlanes(float nearZ, float farZ);

        // Selected object, nullptr if nothing is selected.
        std::shared_ptr<SceneNode> GetSelectedNode() const { return m_selectedNode.lock(); }
        void SelectNode(const std::shared_ptr<SceneNode>& node) { m_selectedNode = node; }

        // ----------------------------------------------------------------------------------------
        // Scenes, panels and menus

        // Scenes shown in the Hierarchy panel (and used for picking and drop placement).
        void AddScene(const std::string& name, const std::shared_ptr<Scene>& scene);
        void RemoveScene(const std::shared_ptr<Scene>& scene);

        // Models dropped into the scene from the Assets panel. The application renders these.
        std::vector<std::shared_ptr<Scene>> GetSpawnedScenes() const;

        // Folder shown in the Assets panel (relative to the working directory). Default: "assets".
        void SetAssetRoot(const std::filesystem::path& root);

        // A dockable window drawn by the application. The callback is called between ImGui::Begin and ImGui::End.
        void AddPanel(const std::string& name, std::function<void()> draw, DockSlot slot = DockSlot::Right, bool open = true);

        // Extra items for the main menu bar, called inside BeginMainMenuBar/EndMainMenuBar.
        void AddMenuCallback(std::function<void()> callback);

        // Open a texture in the Texture Viewer panel.
        void InspectTexture(const std::shared_ptr<Texture>& texture);

        // Rebuild the default panel layout.
        void ResetLayout() { m_resetLayout = true; }

        std::shared_ptr<GUI> GetGUI() const { return m_gui; }

        // Show a small preview of a texture inline (any format). Clicking it opens the Texture Viewer.
        void TextureThumbnail(const std::shared_ptr<Texture>& texture, float size);

    private:
        struct Panel
        {
            std::string           name;
            std::function<void()> draw;
            DockSlot              slot;
            bool                  open;
        };

        struct SceneEntry
        {
            std::string            name;
            std::shared_ptr<Scene> scene;
            bool                   spawned = false; // Created by dropping an asset, rendered by the application.
        };

        // A model being dragged from the Assets panel into the viewport.
        struct ModelDrag
        {
            std::filesystem::path               path;
            std::future<std::shared_ptr<Scene>> loading;
            std::shared_ptr<Scene>              scene;          // Set once loaded.
            DirectX::XMFLOAT4X4                 rootTransform;  // Root transform as loaded.
            DirectX::XMFLOAT3                   bottomOffset;   // Bottom center of the model relative to its root position.
            DirectX::XMFLOAT3                   position = {};  // Where the model's bottom center goes.
            bool                                hasPosition = false;
            bool                                delivered = false; // Mouse released over the viewport.
        };

        // An image dropped onto a material slot or object, loading in the background.
        struct TextureDrop
        {
            std::future<std::shared_ptr<Texture>> loading;
            std::weak_ptr<Material>               material;
            int                                   slot;
        };

        struct AssetThumbnail
        {
            std::future<std::shared_ptr<Texture>> loading;
            std::shared_ptr<Texture>              texture;
            bool                                  failed = false;
        };

        struct AssetEntry
        {
            std::filesystem::path path;
            std::string           name;
            bool                  isDirectory = false;
        };

        // Contents of the folder shown in the Assets panel.
        struct AssetListing
        {
            std::filesystem::path   directory;
            std::string             filter;
            double                  time = 0.0;
            std::vector<AssetEntry> entries;
        };

        // A model file loaded for its thumbnail in the Assets panel. The model is rendered once and then released.
        struct ModelThumbnail
        {
            std::future<std::shared_ptr<Scene>> loading;
            std::shared_ptr<Texture>            image;
            bool                                failed = false;
        };

        // The model shown in the Model Preview panel.
        struct PreviewedModel
        {
            std::filesystem::path               path;
            std::future<std::shared_ptr<Scene>> loading;
            std::shared_ptr<Scene>              scene;
            std::shared_ptr<Texture>            image;
            ModelPreviewView                    view;
            bool                                failed = false;
            bool                                dirty = true; // The image has to be rendered again.

            // Shown below the preview.
            size_t            meshCount = 0;
            size_t            vertexCount = 0;
            size_t            triangleCount = 0;
            size_t            materialCount = 0;
            size_t            textureCount = 0;
            DirectX::XMFLOAT3 size = {};
        };

        void ApplyTheme();
        void DrawMenuBar();
        void DrawStatusBar();
        void BuildDefaultLayout(ImGuiID dockspaceID);

        void DrawSceneViewport();
        void DrawViewportToolbar(const ImVec2& imagePosition);
        bool DrawGizmo(const ImVec2& imagePosition, const ImVec2& imageSize);
        void PickNode(const ImVec2& mousePosition);
        void DrawHierarchy();
        void DrawHierarchyNode(const std::shared_ptr<SceneNode>& node, const char* label);
        void ApplyPendingReparent();
        void DrawInspector();
        void DrawMaterial(const std::shared_ptr<Material>& material, int index);
        void DrawTextureBrowser();
        void DrawTextureViewer();
        void DrawStats();

        // Assets and drag & drop (editor_assets.cpp)
        void DrawAssetBrowser();
        void DrawAssetIcon(const std::filesystem::path& path, bool isDirectory, float size);
        bool DrawModelThumbnail(const std::filesystem::path& path, float size);
        void DrawModelPreview();
        void PreviewModel(const std::filesystem::path& path);
        void AcceptViewportDrop();
        void UpdateModelDrag();
        void PlaceDraggedModel();
        void UpdateTextureDrops();
        void DropTextureOnMaterial(const std::filesystem::path& path, const std::shared_ptr<Material>& material, int slot);
        bool RayCastScenes(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, const Scene* ignore, float& distance) const;
        bool GetMouseRay(DirectX::XMVECTOR& origin, DirectX::XMVECTOR& direction) const;
        void DeleteSelectedNode();
        void SpawnModelInFrontOfCamera(const std::filesystem::path& path);
        std::shared_ptr<SceneNode> FindNodeUnderMouse() const;

        std::shared_ptr<Texture> FindLiveTexture(const std::wstring& name) const;

        // Textures created by background loads may still be uploading on the copy queue; they are not
        // sampled until all editor loads have finished.
        bool IsLoadingInBackground() const;
        void UpdateSettledTextures();
        bool IsTextureSettled(const std::shared_ptr<Texture>& texture) const;

        std::shared_ptr<GUI>             m_gui;
        std::unique_ptr<TexturePreview>  m_texturePreview;
        std::unique_ptr<ModelPreview>    m_modelPreview;
        std::unique_ptr<TextureReadback> m_textureReadback;

        std::vector<Panel>                 m_panels;
        std::vector<std::function<void()>> m_menuCallbacks;
        std::vector<SceneEntry>            m_scenes;

        // Built-in panel visibility.
        bool m_showScene = true;
        bool m_showHierarchy = true;
        bool m_showInspector = true;
        bool m_showTextureBrowser = true;
        bool m_showTextureViewer = true;
        bool m_showStats = true;
        bool m_showImGuiDemo = false;
        bool m_resetLayout = false;
        bool m_focusTextureViewer = false;

        // Scene viewport
        std::shared_ptr<Texture> m_sceneTexture;
        uint32_t m_viewportWidth = 1;
        uint32_t m_viewportHeight = 1;
        bool     m_viewportResized = false;
        bool     m_viewportHovered = false;
        bool     m_viewportFocused = false;
        bool     m_viewportDragging = false;
        bool     m_hierarchyFocused = false;
        ImVec2   m_viewportMousePosition = {};
        const Camera* m_camera = nullptr;
        int      m_gizmoOperation = 0; // ImGuizmo::OPERATION
        bool     m_gizmoLocal = false;
        float    m_nearZ = 0.1f;
        float    m_farZ = 1000.0f;

        // Selection
        std::weak_ptr<SceneNode> m_selectedNode;
        const SceneNode*         m_inspectedNode = nullptr; // Selection shown last frame, to notice changes (never dereferenced).

        // Reparenting requested by a hierarchy drop; applied after the tree is drawn.
        struct Reparent
        {
            SceneNode*                 node = nullptr;
            std::shared_ptr<SceneNode> newParent;
        } m_pendingReparent;

        // Texture browser / viewer
        std::weak_ptr<Texture>                  m_viewedTexture;
        std::wstring                            m_viewedTextureName; // Used to find the texture again when it is recreated.
        std::wstring                            m_startupTextureName;
        std::unique_ptr<TexturePreviewSettings> m_viewerSettings;
        float  m_viewerZoom = 1.0f;
        ImVec2 m_viewerPan = {};
        bool   m_viewerFit = true;
        bool   m_viewerAutoRangePending = false;
        char   m_textureFilter[128] = {};
        bool   m_showEditorTextures = false;

        // Assets
        std::filesystem::path m_assetRoot = "assets";
        std::filesystem::path m_assetDirectory = "assets";
        bool  m_showAssetBrowser = true;
        float m_assetIconSize = 72.0f;
        char  m_assetFilter[128] = {};
        AssetListing m_assetListing;
        std::unordered_map<std::wstring, AssetThumbnail> m_assetThumbnails;
        std::unordered_map<std::wstring, ModelThumbnail> m_modelThumbnails;
        std::filesystem::path        m_selectedAsset;
        PreviewedModel               m_previewedModel;
        bool                         m_showModelPreview = true;
        bool                         m_focusModelPreview = false;
        std::unique_ptr<ModelDrag>   m_modelDrag;
        std::vector<std::future<std::shared_ptr<Scene>>> m_discardedLoads; // Cancelled loads, dropped once finished.
        std::set<std::weak_ptr<Texture>, std::owner_less<std::weak_ptr<Texture>>> m_settledTextures;
        std::vector<TextureDrop>     m_textureDrops;
        float                        m_dropDistance = 10.0f;
        std::string                  m_statusMessage;
        float                        m_statusMessageTime = 0.0f;

        // Stats
        float  m_frameTimes[120] = {};
        int    m_frameTimeIndex = 0;
    };
}
