#pragma once

/**
 *  @file editor.h
 *
 *  @brief ImGui based scene editor: a docked layout with a scene viewport, hierarchy,
 *  inspector, texture browser/viewer, shaders, render graph and stats. Any Game can use it:
 *
 *      m_editor = std::make_unique<Editor>(m_GUI);
 *      m_editor->AddScene("Helmet", m_helmet);
 *
 *      // Every frame, render the scene into your own texture, then:
 *      m_editor->BeginFrame();
 *      m_editor->SetSceneTexture(sceneTexture);
 *      ... your own ImGui windows ...
 *      m_editor->EndFrame(commandList, swapChainRenderTarget);
 *
 *  Scenes are saved to and loaded from .evscene files (File menu). Play mode (F5) hides the editor and
 *  shows only the scene; "--play <file.evscene>" on the command line starts the application that way.
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
    class RenderGraph;
    class RenderTarget;
    class Scene;
    class SceneNode;
    class Shader;
    class Texture;
    class TexturePreview;
    class TextureReadback;
    struct TexturePreviewSettings;
    struct RenderGraphDebugInfo;

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

        // The camera used to render the viewport. Needed for picking and the transform gizmo, and saved in scene files.
        void SetCamera(Camera* camera) { m_camera = camera; }

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

        // HDR panoramas dropped into the Scene (or double clicked in the Assets panel) are loaded in the
        // background and passed to this callback, e.g. to rebuild the skybox and image based lighting.
        // Without a callback, environment files cannot be dropped.
        void SetEnvironmentCallback(std::function<void(const std::shared_ptr<Texture>& panorama)> callback)
        {
            m_environmentCallback = std::move(callback);
        }

        // Folder shown in the Assets panel (relative to the working directory). Default: "assets".
        void SetAssetRoot(const std::filesystem::path& root);

        // A dockable window drawn by the application. The callback is called between ImGui::Begin and ImGui::End.
        void AddPanel(const std::string& name, std::function<void()> draw, DockSlot slot = DockSlot::Right, bool open = true);

        // Extra items for the main menu bar, called inside BeginMainMenuBar/EndMainMenuBar.
        void AddMenuCallback(std::function<void()> callback);

        // The render graph shown in the Render Graph panel (passes, timings, texture captures). Not owned.
        void SetRenderGraph(RenderGraph* renderGraph) { m_renderGraph = renderGraph; }

        // Open a texture in the Texture Viewer panel.
        void InspectTexture(const std::shared_ptr<Texture>& texture);

        // Rebuild the default panel layout.
        void ResetLayout() { m_resetLayout = true; }

        std::shared_ptr<GUI> GetGUI() const { return m_gui; }

        // Show a small preview of a texture inline (any format). Clicking it opens the Texture Viewer.
        void TextureThumbnail(const std::shared_ptr<Texture>& texture, float size);

        // ----------------------------------------------------------------------------------------
        // Scene files (.evscene, File menu)
        //
        // A scene file holds the models placed in the editor (model file, node hierarchy, transforms and
        // material changes), the transforms of the application's own scenes (matched by the name given to
        // AddScene), the camera and the environment. Models and textures are loaded again from their files,
        // so they are stored relative to the working directory and have to stay where they are.

        // Remove every model placed in the editor. Scenes added by the application stay.
        void NewScene();

        // Write the scene to a file. Returns false (and says why in the status bar) when it could not be written.
        bool SaveScene(const std::filesystem::path& path);

        // Load a scene file in the background. It replaces the current scene once all its models are loaded.
        void LoadScene(const std::filesystem::path& path);

        bool IsLoadingScene() const { return m_sceneLoad.valid(); }

        // The file the scene was last saved to or loaded from, empty for a new scene.
        const std::filesystem::path& GetScenePath() const { return m_scenePath; }

        // Called after the editor moved the camera (a scene was loaded or play mode stopped), so the application
        // can update its camera controller, e.g. the pitch and yaw it rebuilds the camera rotation from.
        void SetCameraChangedCallback(std::function<void()> callback) { m_cameraChangedCallback = std::move(callback); }

        // ----------------------------------------------------------------------------------------
        // Play mode
        //
        // While playing, the editor draws no menus, panels or gizmos: the scene texture fills the window and the
        // viewport size is the window size. F5 (or the Play button) starts playing, F5 or Esc stops. Transforms
        // and the camera go back to where they were when play mode stops.
        // Started with "--play <file.evscene>", the application only plays that scene and Esc quits; that is how
        // a finished game runs. "--scene <file.evscene>" opens a scene in the editor.

        void Play();
        void StopPlaying();
        bool IsPlaying() const { return m_playing; }

        // True when the application was started with --play: there is no editor to go back to.
        bool IsStandalonePlay() const { return m_standalonePlay; }

        // Called when play mode starts (true) and stops (false). Run game logic only while playing.
        void SetPlayModeCallback(std::function<void(bool playing)> callback) { m_playModeCallback = std::move(callback); }

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

        // An HDR panorama loading in the background, passed to the environment callback once loaded.
        struct EnvironmentLoad
        {
            std::filesystem::path                 path;
            std::future<std::shared_ptr<Texture>> loading;
        };

        // A scene file read in the background (editor_scene_file.cpp). Everything in it is built off the main
        // thread; the main thread only swaps it in.
        struct SceneFileContents
        {
            // Node transforms of a scene added by the application, in ForEachNode order.
            struct ApplicationScene
            {
                std::string                      name;
                std::vector<DirectX::XMFLOAT4X4> transforms;
            };

            std::filesystem::path         path;
            std::string                   error; // Set when the file could not be read.
            std::vector<std::string>      warnings;
            std::vector<SceneEntry>       scenes; // Models placed in the editor.
            std::vector<ApplicationScene> applicationScenes;
            bool                          hasCamera = false;
            DirectX::XMFLOAT3             cameraPosition = {};
            DirectX::XMFLOAT4             cameraRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            float                         cameraFov = 45.0f;
            std::filesystem::path         environment;
        };

        // Open and save dialogs run on their own thread, so the window keeps rendering while one is open.
        struct FileDialog
        {
            bool                               save = false;
            std::future<std::filesystem::path> result; // Empty path when cancelled.
        };

        // Transforms and camera when play mode started, restored when it stops.
        struct PlaySnapshot
        {
            std::vector<std::pair<std::weak_ptr<SceneNode>, DirectX::XMFLOAT4X4>> transforms;
            DirectX::XMFLOAT3 cameraPosition = {};
            DirectX::XMFLOAT4 cameraRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            float             cameraFov = 45.0f;
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
        void DrawEditorPanels();
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

        // Shader hot reloading (editor_shaders.cpp)
        void DrawShaders();
        void UpdateShaderReloads();

        // Render graph debugging (editor_render_graph.cpp)
        void DrawRenderGraph();
        void DrawRenderGraphPasses(const RenderGraphDebugInfo& info);
        void DrawRenderGraphTimeline(const RenderGraphDebugInfo& info);
        void DrawRenderGraphResources(const RenderGraphDebugInfo& info);
        void DrawRenderGraphBufferCapture();
        void CaptureRenderGraphResource(const std::string& pass, const std::string& resource, bool isBuffer);

        // Assets and drag & drop (editor_assets.cpp)
        void DrawAssetBrowser();
        void DrawAssetIcon(const std::filesystem::path& path, bool isDirectory, float size);
        bool DrawModelThumbnail(const std::filesystem::path& path, float size);
        void DrawModelPreview();
        void PreviewModel(const std::filesystem::path& path);
        void LoadEnvironment(const std::filesystem::path& path);
        void UpdateEnvironmentLoad();
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

        // Scene files (editor_scene_file.cpp)
        static std::unique_ptr<SceneFileContents> ReadSceneFile(const std::filesystem::path& path);
        void ApplySceneFile(SceneFileContents& contents);
        void UpdateSceneLoad();
        void DrawFileMenu();
        void SaveCurrentScene();
        void ShowFileDialog(bool save);
        void UpdateFileDialog();
        void CancelModelDrag();

        // Play mode (editor_play.cpp)
        void UpdatePlayMode();
        void DrawPlayView();
        void DrawPlayButton();

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
        bool m_showShaders = true;
        bool m_showRenderGraph = true;
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
        Camera*  m_camera = nullptr;
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
        std::function<void(const std::shared_ptr<Texture>&)> m_environmentCallback;
        std::unique_ptr<EnvironmentLoad> m_environmentLoad;
        std::unique_ptr<ModelDrag>   m_modelDrag;
        std::vector<std::future<std::shared_ptr<Scene>>> m_discardedLoads; // Cancelled loads, dropped once finished.
        std::set<std::weak_ptr<Texture>, std::owner_less<std::weak_ptr<Texture>>> m_settledTextures;
        std::vector<TextureDrop>     m_textureDrops;
        float                        m_dropDistance = 10.0f;
        std::string                  m_statusMessage;
        float                        m_statusMessageTime = 0.0f;

        // Shaders
        std::weak_ptr<Shader> m_selectedShader;
        uint64_t              m_shaderReloadCount = 0; // ShaderLibrary reload count already reported.

        // Render graph
        RenderGraph* m_renderGraph = nullptr;
        int          m_selectedGraphPass = -1;
        bool         m_inspectRenderGraphCapture = false; // Open a texture capture in the Texture Viewer once it exists.
        bool         m_selectBufferCaptureTab = false;
        int          m_bufferViewFormat = -1; // Float, int, uint, hex (32 bit), uint16, hex16; -1 = from the stride.
        int          m_bufferViewColumns = 0; // Values per row, 0 = from the buffer's stride.

        // Scene files
        std::filesystem::path                           m_scenePath;
        std::future<std::unique_ptr<SceneFileContents>> m_sceneLoad;
        std::filesystem::path                           m_sceneLoadPath;
        std::unique_ptr<FileDialog>                     m_fileDialog;
        std::filesystem::path                           m_environmentPath; // Last environment loaded by the editor.
        std::function<void()>                           m_cameraChangedCallback;

        // Play mode
        bool                       m_playing = false;
        bool                       m_standalonePlay = false;
        bool                       m_startupPlay = false; // --play: starts playing on the first frame.
        float                      m_playHintTime = 0.0f; // How long "Esc: stop playing" is still shown.
        PlaySnapshot               m_playSnapshot;
        std::function<void(bool)>  m_playModeCallback;

        // Stats
        float  m_frameTimes[120] = {};
        int    m_frameTimeIndex = 0;
    };
}
