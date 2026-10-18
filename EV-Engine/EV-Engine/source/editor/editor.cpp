#include "DX12/dx12_includes.h"

#include "editor/editor.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

#include "imgui_internal.h"
#include "ImGuizmo.h"

#include "core/application.h"
#include "core/camera.h"
#include "DX12/command_list.h"
#include "DX12/command_queue.h"
#include "DX12/render_target.h"
#include "DX12/scene.h"
#include "DX12/scene_node.h"
#include "resources/material.h"
#include "resources/mesh.h"
#include "resources/texture.h"
#include "UI/GUI.h"
#include "UI/format_utils.h"
#include "UI/texture_preview.h"
#include "UI/texture_readback.h"

#include "editor_internal.h"

using namespace EV;
using namespace DirectX;
using namespace EV::EditorInternal;

namespace
{
    // Window names, also used to dock them in the default layout.
    constexpr const char* g_sceneWindow = "Scene";
    constexpr const char* g_hierarchyWindow = "Hierarchy";
    constexpr const char* g_inspectorWindow = "Inspector";
    constexpr const char* g_textureBrowserWindow = "Textures";
    constexpr const char* g_statsWindow = "Stats";

    const wchar_t* g_previewTextureName = L"Editor Texture Preview";

    // Preview slots: thumbnails are keyed per texture, the viewer has its own slot.
    constexpr uint64_t g_viewerPreviewKey = 1;

    std::string GetTextureLabel(const Texture& texture)
    {
        const std::wstring& name = texture.GetName();
        if (name.empty())
        {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "<unnamed %p>", static_cast<const void*>(&texture));
            return buffer;
        }
        return ToUTF8(name);
    }

    // Only the file name of a path, used to keep loaded texture names short.
    std::string GetShortTextureLabel(const Texture& texture)
    {
        std::string label = GetTextureLabel(texture);
        size_t slash = label.find_last_of("/\\");
        return slash == std::string::npos ? label : label.substr(slash + 1);
    }

    std::string FormatBytes(uint64_t bytes)
    {
        char buffer[32];
        if (bytes >= 1024ull * 1024ull)
        {
            snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0));
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "%.1f KB", bytes / 1024.0);
        }
        return buffer;
    }

    uint64_t GetTextureMemory(const Texture& texture)
    {
        auto resource = texture.GetD3D12Resource();
        if (!resource)
        {
            return 0;
        }
        const auto desc = resource->GetDesc();
        return Application::Get().GetDevice()->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    }

    std::string DescribeDimensions(const D3D12_RESOURCE_DESC& desc)
    {
        char buffer[96];
        switch (desc.Dimension)
        {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            snprintf(buffer, sizeof(buffer), "1D %llu", static_cast<unsigned long long>(desc.Width));
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            snprintf(buffer, sizeof(buffer), "3D %llux%ux%u", static_cast<unsigned long long>(desc.Width), desc.Height, desc.DepthOrArraySize);
            break;
        default:
            if (desc.DepthOrArraySize > 1)
            {
                snprintf(buffer, sizeof(buffer), "%llux%u [%u]%s", static_cast<unsigned long long>(desc.Width), desc.Height,
                    desc.DepthOrArraySize, desc.DepthOrArraySize == 6 ? " cube?" : "");
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%llux%u", static_cast<unsigned long long>(desc.Width), desc.Height);
            }
            break;
        }
        return buffer;
    }

    std::string DescribeUsage(const D3D12_RESOURCE_DESC& desc)
    {
        std::string usage;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) usage += "RT ";
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) usage += "Depth ";
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) usage += "UAV ";
        if (usage.empty()) usage = "SRV";
        return usage;
    }

    // Sensible display defaults for a texture: grayscale for single channel data, sRGB encoding for color.
    TexturePreviewSettings GetDefaultPreviewSettings(const Texture& texture, float nearZ, float farZ)
    {
        TexturePreviewSettings settings;
        const DXGI_FORMAT format = FormatUtils::GetShaderReadableFormat(texture.GetShaderResourceFormat());
        const auto desc = texture.GetD3D12ResourceDesc();
        const uint32_t channels = FormatUtils::GetChannelCount(format);

        settings.channels[0] = true;
        settings.channels[1] = channels >= 2;
        settings.channels[2] = channels >= 3;
        settings.channels[3] = false;

        if (texture.IsDepthTexture())
        {
            settings.linearizeDepth = true;
            settings.nearZ = nearZ;
            settings.farZ = farZ;
            settings.rangeMin = nearZ;
            settings.rangeMax = std::min(farZ, 200.0f);
        }
        else if (FormatUtils::IsSRGB(format) || ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) && channels >= 3 &&
            FormatUtils::GetClass(format) == FormatClass::Float))
        {
            // sRGB textures are sampled as linear and render targets usually hold linear color.
            settings.srgb = true;
        }
        if (FormatUtils::GetClass(format) == FormatClass::Uint || FormatUtils::GetClass(format) == FormatClass::Sint)
        {
            settings.rangeMax = 255.0f;
        }
        return settings;
    }

    // Converts a rotation quaternion into the pitch/yaw/roll used by XMMatrixRotationRollPitchYaw (radians).
    XMFLOAT3 QuaternionToPitchYawRoll(FXMVECTOR quaternion)
    {
        XMFLOAT4 q;
        XMStoreFloat4(&q, quaternion);

        XMFLOAT3 angles;
        angles.x = std::asin(std::clamp(2.0f * (q.w * q.x - q.y * q.z), -1.0f, 1.0f));
        angles.y = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        angles.z = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
        return angles;
    }

    const char* GetTextureSlotName(Material::TextureType type)
    {
        switch (type)
        {
        case Material::TextureType::Ambient: return "Ambient";
        case Material::TextureType::Emissive: return "Emissive";
        case Material::TextureType::Diffuse: return "Diffuse";
        case Material::TextureType::Specular: return "Specular";
        case Material::TextureType::SpecularPower: return "Specular Power";
        case Material::TextureType::Normal: return "Normal";
        case Material::TextureType::Bump: return "Bump";
        case Material::TextureType::Opacity: return "Opacity";
        case Material::TextureType::MetallicRoughness: return "Metallic Roughness";
        case Material::TextureType::H0: return "H0";
        default: return "Unknown";
        }
    }

}

Editor::Editor(std::shared_ptr<GUI> gui)
    : m_gui(std::move(gui))
    , m_texturePreview(std::make_unique<TexturePreview>())
    , m_modelPreview(std::make_unique<ModelPreview>())
    , m_textureReadback(std::make_unique<TextureReadback>(Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)))
    , m_viewerSettings(std::make_unique<TexturePreviewSettings>())
{
    m_gizmoOperation = ImGuizmo::TRANSLATE;
    ApplyTheme();

    // EV_EDITOR_TEXTURE=<texture name> opens a texture in the viewer at startup (used by the CI screenshots).
    wchar_t textureName[256] = {};
    if (GetEnvironmentVariableW(L"EV_EDITOR_TEXTURE", textureName, _countof(textureName)) > 0)
    {
        m_startupTextureName = textureName;
    }

    // EV_EDITOR_MODEL=<path> previews a model at startup and shows its folder in the Assets panel.
    wchar_t modelPath[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"EV_EDITOR_MODEL", modelPath, _countof(modelPath)) > 0)
    {
        m_assetDirectory = std::filesystem::path(modelPath).parent_path();
        m_selectedAsset = modelPath;
        PreviewModel(modelPath);
    }
}

Editor::~Editor() = default;

void Editor::ApplyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = g_colorText;
    colors[ImGuiCol_TextDisabled] = g_colorTextDim;
    colors[ImGuiCol_WindowBg] = g_colorBackground1;
    colors[ImGuiCol_ChildBg] = g_colorBackground1;
    colors[ImGuiCol_PopupBg] = g_colorBackground2;
    colors[ImGuiCol_Border] = g_colorBackground0;
    colors[ImGuiCol_FrameBg] = g_colorBackground2;
    colors[ImGuiCol_FrameBgHovered] = g_colorBackground4;
    colors[ImGuiCol_FrameBgActive] = g_colorBackground3;
    colors[ImGuiCol_TitleBg] = g_colorBackground0;
    colors[ImGuiCol_TitleBgActive] = g_colorBackground0;
    colors[ImGuiCol_TitleBgCollapsed] = g_colorBackground0;
    colors[ImGuiCol_MenuBarBg] = g_colorBackground0;
    colors[ImGuiCol_ScrollbarBg] = g_colorBackground1;
    colors[ImGuiCol_ScrollbarGrab] = g_colorBackground3;
    colors[ImGuiCol_ScrollbarGrabHovered] = g_colorBackground4;
    colors[ImGuiCol_ScrollbarGrabActive] = g_colorAccent;
    colors[ImGuiCol_CheckMark] = g_colorAccent;
    colors[ImGuiCol_SliderGrab] = g_colorAccent;
    colors[ImGuiCol_SliderGrabActive] = g_colorAccent;
    colors[ImGuiCol_Button] = g_colorBackground3;
    colors[ImGuiCol_ButtonHovered] = g_colorBackground4;
    colors[ImGuiCol_ButtonActive] = ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.6f);
    colors[ImGuiCol_Header] = g_colorBackground3;
    colors[ImGuiCol_HeaderHovered] = g_colorBackground4;
    colors[ImGuiCol_HeaderActive] = ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.5f);
    colors[ImGuiCol_Separator] = g_colorBackground0;
    colors[ImGuiCol_SeparatorHovered] = g_colorAccent;
    colors[ImGuiCol_SeparatorActive] = g_colorAccent;
    colors[ImGuiCol_ResizeGrip] = g_colorBackground3;
    colors[ImGuiCol_ResizeGripHovered] = g_colorAccent;
    colors[ImGuiCol_ResizeGripActive] = g_colorAccent;
    colors[ImGuiCol_Tab] = g_colorBackground0;
    colors[ImGuiCol_TabHovered] = g_colorBackground3;
    colors[ImGuiCol_TabSelected] = g_colorBackground1;
    colors[ImGuiCol_TabSelectedOverline] = g_colorAccent;
    colors[ImGuiCol_TabDimmed] = g_colorBackground0;
    colors[ImGuiCol_TabDimmedSelected] = g_colorBackground1;
    colors[ImGuiCol_DockingPreview] = ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.6f);
    colors[ImGuiCol_DockingEmptyBg] = g_colorBackground0;
    colors[ImGuiCol_TableHeaderBg] = g_colorBackground2;
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.35f);
    colors[ImGuiCol_NavCursor] = g_colorAccent;
}

// ------------------------------------------------------------------------------------------------
// Frame

void Editor::BeginFrame()
{
    m_gui->NewFrame();
    ImGuizmo::BeginFrame();
    m_textureReadback->Update();

    UpdateModelDrag();
    UpdateTextureDrops();
    UpdateSettledTextures();
    m_statusMessageTime = std::max(0.0f, m_statusMessageTime - ImGui::GetIO().DeltaTime);

    m_frameTimes[m_frameTimeIndex] = ImGui::GetIO().DeltaTime * 1000.0f;
    m_frameTimeIndex = (m_frameTimeIndex + 1) % IM_ARRAYSIZE(m_frameTimes);

    DrawMenuBar();
    DrawStatusBar();

    const ImGuiID dockspaceID = ImGui::GetID("EditorDockSpace");
    if (m_resetLayout || ImGui::DockBuilderGetNode(dockspaceID) == nullptr)
    {
        BuildDefaultLayout(dockspaceID);
        m_resetLayout = false;
    }
    ImGui::DockSpaceOverViewport(dockspaceID, ImGui::GetMainViewport());
}

void Editor::EndFrame(const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget)
{
    DrawSceneViewport();
    DrawHierarchy();
    DrawInspector();
    DrawAssetBrowser();
    DrawModelPreview();
    DrawTextureBrowser();
    DrawTextureViewer();
    DrawStats();

    // Delete removes the selected object while the scene or hierarchy has focus.
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput &&
        (m_viewportFocused || m_hierarchyFocused))
    {
        DeleteSelectedNode();
    }

    for (auto& panel : m_panels)
    {
        if (!panel.open)
        {
            continue;
        }
        if (ImGui::Begin(panel.name.c_str(), &panel.open))
        {
            panel.draw();
        }
        ImGui::End();
    }

    if (m_showImGuiDemo)
    {
        ImGui::ShowDemoWindow(&m_showImGuiDemo);
    }

    // Previews and readbacks have to be recorded before the GUI samples them.
    m_texturePreview->Render(*commandList);
    m_modelPreview->Render(*commandList);
    m_textureReadback->Record(*commandList);

    m_gui->Render(commandList, renderTarget);
}

void Editor::BuildDefaultLayout(ImGuiID dockspaceID)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(dockspaceID);
    ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->WorkSize);

    ImGuiID center = dockspaceID;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.36f, nullptr, &center);
    ImGuiID bottomRight = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.55f, nullptr, &bottom);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.3f, nullptr, &left);

    ImGui::DockBuilderDockWindow(g_sceneWindow, center);
    ImGui::DockBuilderDockWindow(g_hierarchyWindow, left);
    ImGui::DockBuilderDockWindow(g_statsWindow, leftBottom);
    ImGui::DockBuilderDockWindow(g_inspectorWindow, right);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow(g_textureBrowserWindow, bottom);
    ImGui::DockBuilderDockWindow(g_textureViewerWindow, bottomRight);
    ImGui::DockBuilderDockWindow(g_modelPreviewWindow, bottomRight);

    for (const auto& panel : m_panels)
    {
        ImGuiID node = right;
        switch (panel.slot)
        {
        case DockSlot::Left: node = left; break;
        case DockSlot::Bottom: node = bottom; break;
        case DockSlot::Center: node = center; break;
        default: break;
        }
        ImGui::DockBuilderDockWindow(panel.name.c_str(), node);
    }

    ImGui::DockBuilderFinish(dockspaceID);
}

void Editor::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    for (auto& callback : m_menuCallbacks)
    {
        callback();
    }

    if (ImGui::BeginMenu("Window"))
    {
        ImGui::MenuItem(g_sceneWindow, nullptr, &m_showScene);
        ImGui::MenuItem(g_hierarchyWindow, nullptr, &m_showHierarchy);
        ImGui::MenuItem(g_inspectorWindow, nullptr, &m_showInspector);
        ImGui::MenuItem("Assets", nullptr, &m_showAssetBrowser);
        ImGui::MenuItem(g_modelPreviewWindow, nullptr, &m_showModelPreview);
        ImGui::MenuItem(g_textureBrowserWindow, nullptr, &m_showTextureBrowser);
        ImGui::MenuItem(g_textureViewerWindow, nullptr, &m_showTextureViewer);
        ImGui::MenuItem(g_statsWindow, nullptr, &m_showStats);
        if (!m_panels.empty())
        {
            ImGui::Separator();
            for (auto& panel : m_panels)
            {
                ImGui::MenuItem(panel.name.c_str(), nullptr, &panel.open);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
        {
            ResetLayout();
        }
        ImGui::MenuItem("ImGui Demo", nullptr, &m_showImGuiDemo);
        ImGui::EndMenu();
    }

    // Frame rate on the right side of the menu bar.
    float averageMs = 0.0f;
    for (float ms : m_frameTimes) averageMs += ms;
    averageMs /= IM_ARRAYSIZE(m_frameTimes);

    char fps[64];
    snprintf(fps, sizeof(fps), "%.0f FPS  %.2f ms", averageMs > 0.0f ? 1000.0f / averageMs : 0.0f, averageMs);
    const float width = ImGui::CalcTextSize(fps).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - width);
    ImGui::TextColored(g_colorTextDim, "%s", fps);

    ImGui::EndMainMenuBar();
}

void Editor::DrawStatusBar()
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    const float height = ImGui::GetFrameHeight();

    if (ImGui::BeginViewportSideBar("##EditorStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            if (auto node = m_selectedNode.lock())
            {
                ImGui::Text("Selected: %s", node->GetName().empty() ? "<unnamed>" : node->GetName().c_str());
            }
            else
            {
                ImGui::TextColored(g_colorTextDim, "Nothing selected");
            }
            ImGui::SameLine(0.0f, 30.0f);
            ImGui::TextColored(g_colorTextDim, "Viewport %ux%u", m_viewportWidth, m_viewportHeight);
            ImGui::SameLine(0.0f, 30.0f);
            if (m_statusMessageTime > 0.0f)
            {
                ImGui::TextColored(g_colorAccent, "%s", m_statusMessage.c_str());
            }
            else if (m_modelDrag)
            {
                ImGui::TextColored(g_colorAccent, "Loading %s...", m_modelDrag->path.filename().string().c_str());
            }
            else
            {
                ImGui::TextColored(g_colorTextDim, "Right mouse + WASD/QE: fly   Left click: select   Del: delete   Drag assets into the scene");
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------
// Public helpers

void Editor::SetSceneTexture(const std::shared_ptr<Texture>& texture)
{
    m_sceneTexture = texture;
}

bool Editor::ConsumeViewportResize()
{
    bool resized = m_viewportResized;
    m_viewportResized = false;
    return resized;
}

bool Editor::IsViewportKeyboardActive() const
{
    return (m_viewportFocused || m_viewportDragging) && !ImGui::GetIO().WantTextInput;
}

void Editor::SetCameraClipPlanes(float nearZ, float farZ)
{
    m_nearZ = nearZ;
    m_farZ = farZ;
}

void Editor::AddScene(const std::string& name, const std::shared_ptr<Scene>& scene)
{
    if (scene)
    {
        m_scenes.push_back({ name, scene });
    }
}

void Editor::RemoveScene(const std::shared_ptr<Scene>& scene)
{
    std::erase_if(m_scenes, [&scene](const SceneEntry& entry) { return entry.scene == scene; });
}

void Editor::AddPanel(const std::string& name, std::function<void()> draw, DockSlot slot, bool open)
{
    m_panels.push_back({ name, std::move(draw), slot, open });
}

void Editor::AddMenuCallback(std::function<void()> callback)
{
    m_menuCallbacks.push_back(std::move(callback));
}

void Editor::InspectTexture(const std::shared_ptr<Texture>& texture)
{
    if (!texture)
    {
        return;
    }

    if (m_viewedTexture.lock() != texture)
    {
        m_viewedTexture = texture;
        m_viewedTextureName = texture->GetName();
        *m_viewerSettings = GetDefaultPreviewSettings(*texture, m_nearZ, m_farZ);
        m_viewerFit = true;
    }
    m_showTextureViewer = true;
    m_focusTextureViewer = true;
}

std::shared_ptr<Texture> Editor::FindLiveTexture(const std::wstring& name) const
{
    if (name.empty())
    {
        return nullptr;
    }
    for (auto& texture : Application::Get().GetLiveTextures())
    {
        if (texture->GetName() == name)
        {
            return texture;
        }
    }
    return nullptr;
}

void Editor::TextureThumbnail(const std::shared_ptr<Texture>& texture, float size)
{
    if (!texture)
    {
        ImGui::Dummy(ImVec2(size, size));
        return;
    }

    const auto desc = texture->GetD3D12ResourceDesc();
    std::shared_ptr<Texture> image;

    if (TexturePreview::CanPreview(*texture) && IsTextureSettled(texture))
    {
        TexturePreviewSettings settings = GetDefaultPreviewSettings(*texture, m_nearZ, m_farZ);
        settings.linearFilter = true;

        // Pick the mip closest to the thumbnail size to avoid aliasing.
        const float largest = static_cast<float>(std::max<uint64_t>(desc.Width, desc.Height));
        const int mip = static_cast<int>(std::floor(std::log2(std::max(1.0f, largest / size))));
        settings.mip = std::clamp(mip, 0, desc.MipLevels - 1);

        // Thumbnails are keyed per texture and size so each keeps its own image (no reallocation every frame).
        const uint64_t key = (reinterpret_cast<uint64_t>(texture.get()) * 31 + static_cast<uint64_t>(size)) | (1ull << 63);
        image = m_texturePreview->Request(key, texture, settings, static_cast<uint32_t>(size), static_cast<uint32_t>(size));
    }

    // Keep the aspect ratio inside a size x size box.
    const float aspect = desc.Height > 0 ? static_cast<float>(desc.Width) / desc.Height : 1.0f;
    ImVec2 imageSize = aspect >= 1.0f ? ImVec2(size, size / aspect) : ImVec2(size * aspect, size);

    if (image)
    {
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::Image(m_gui->GetTextureID(image), imageSize);
        // Reserve the full square so rows line up.
        ImGui::SetCursorPos(cursor);
        ImGui::Dummy(ImVec2(size, size));
    }
    else
    {
        ImGui::Button("n/a", ImVec2(size, size));
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s\n%s  %s\nClick to open in the Texture Viewer", GetTextureLabel(*texture).c_str(),
            DescribeDimensions(desc).c_str(), FormatUtils::GetName(desc.Format));
    }
    if (ImGui::IsItemClicked())
    {
        InspectTexture(texture);
    }
}

// ------------------------------------------------------------------------------------------------
// Scene viewport

void Editor::DrawSceneViewport()
{
    m_viewportHovered = false;
    m_viewportFocused = false;

    if (!m_showScene)
    {
        m_viewportDragging = false;
        return;
    }

    // ImGuizmo only starts a drag when no ImGui item is hovered or active, so while the gizmo is hovered
    // (previous frame) or in use the viewport's input button steps aside, and the window must not move.
    // Not while dragging an asset: the button is also the drop target (and a dragged model, which is
    // selected once loaded, has its gizmo right under the mouse).
    const bool assetDragging = ImGui::GetDragDropPayload() != nullptr;
    const bool gizmoOwnsMouse = !assetDragging && !m_selectedNode.expired() && ImGuizmo::IsOver();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(g_sceneWindow, &m_showScene, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        (gizmoOwnsMouse ? ImGuiWindowFlags_NoMove : 0));
    ImGui::PopStyleVar();

    if (visible)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const uint32_t width = static_cast<uint32_t>(std::max(1.0f, available.x));
        const uint32_t height = static_cast<uint32_t>(std::max(1.0f, available.y));
        if (width != m_viewportWidth || height != m_viewportHeight)
        {
            m_viewportWidth = width;
            m_viewportHeight = height;
            m_viewportResized = true;
        }

        const ImVec2 imagePosition = ImGui::GetCursorScreenPos();
        const ImVec2 imageSize(static_cast<float>(width), static_cast<float>(height));

        // The invisible button owns the viewport's mouse input, the image is drawn behind it.
        // Overlap is allowed so the toolbar drawn on top of it can still be clicked.
        if (gizmoOwnsMouse)
        {
            ImGui::Dummy(imageSize);
        }
        else
        {
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##SceneViewport", imageSize,
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        }
        // While dragging something the mouse belongs to drag & drop, not to the camera.
        const bool itemHovered = ImGui::IsItemHovered() && ImGui::GetDragDropPayload() == nullptr;
        const ImGuiIO& io = ImGui::GetIO();
        m_viewportMousePosition = ImVec2(io.MousePos.x - imagePosition.x, io.MousePos.y - imagePosition.y);
        AcceptViewportDrop();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (m_sceneTexture)
        {
            drawList->AddImage(m_gui->GetTextureID(m_sceneTexture), imagePosition,
                ImVec2(imagePosition.x + imageSize.x, imagePosition.y + imageSize.y));
        }
        else
        {
            drawList->AddText(ImVec2(imagePosition.x + 10.0f, imagePosition.y + 10.0f), ImGui::GetColorU32(g_colorTextDim),
                "No scene texture. Call Editor::SetSceneTexture().");
        }

        // While a dragged model is still loading, mark where it will land.
        if (m_modelDrag && !m_modelDrag->scene && m_modelDrag->hasPosition && m_camera)
        {
            const XMVECTOR clip = XMVector3TransformCoord(XMLoadFloat3(&m_modelDrag->position),
                m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix());
            const ImVec2 marker(imagePosition.x + (XMVectorGetX(clip) * 0.5f + 0.5f) * imageSize.x,
                imagePosition.y + (0.5f - XMVectorGetY(clip) * 0.5f) * imageSize.y);
            drawList->AddCircle(marker, 10.0f, ImGui::GetColorU32(g_colorAccent), 0, 2.0f);
            drawList->AddLine(ImVec2(marker.x - 16.0f, marker.y), ImVec2(marker.x + 16.0f, marker.y), ImGui::GetColorU32(g_colorAccent), 2.0f);
            drawList->AddLine(ImVec2(marker.x, marker.y - 16.0f), ImVec2(marker.x, marker.y + 16.0f), ImGui::GetColorU32(g_colorAccent), 2.0f);
        }

        const bool gizmoActive = DrawGizmo(imagePosition, imageSize);
        DrawViewportToolbar(imagePosition);
        const bool toolbarHovered = ImGui::IsMouseHoveringRect(imagePosition, ImGui::GetItemRectMax());

        // The toolbar and gizmo sit on top of the image; they take the mouse when hovered.
        m_viewportHovered = itemHovered && !gizmoActive && !toolbarHovered;
        m_viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (m_viewportHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
        {
            m_viewportDragging = true;
            ImGui::SetWindowFocus();
        }
        if (m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            ImGui::SetWindowFocus();
            PickNode(m_viewportMousePosition);
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        m_viewportDragging = false;
    }

    ImGui::End();
}

void Editor::DrawViewportToolbar(const ImVec2& imagePosition)
{
    ImGui::SetCursorScreenPos(ImVec2(imagePosition.x + 8.0f, imagePosition.y + 8.0f));

    auto toolButton = [this](const char* label, int operation, const char* tooltip)
    {
        const bool selected = m_gizmoOperation == operation;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.8f));
        if (ImGui::Button(label)) m_gizmoOperation = operation;
        if (selected) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        ImGui::SameLine();
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    toolButton("Move", ImGuizmo::TRANSLATE, "Translate the selected object");
    toolButton("Rotate", ImGuizmo::ROTATE, "Rotate the selected object");
    toolButton("Scale", ImGuizmo::SCALE, "Scale the selected object");
    if (ImGui::Button(m_gizmoLocal ? "Local" : "World"))
    {
        m_gizmoLocal = !m_gizmoLocal;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle gizmo space");
    ImGui::PopStyleVar();
}

bool Editor::DrawGizmo(const ImVec2& imagePosition, const ImVec2& imageSize)
{
    auto node = m_selectedNode.lock();
    if (!node || !m_camera || ImGui::GetDragDropPayload() != nullptr)
    {
        return false;
    }

    XMFLOAT4X4 view, projection, world;
    XMStoreFloat4x4(&view, m_camera->GetViewMatrix());
    XMStoreFloat4x4(&projection, m_camera->GetProjectionMatrix());
    XMStoreFloat4x4(&world, node->GetWorldTransform());

    // DirectX row-major matrices with row vectors have the memory layout ImGuizmo expects.
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(imagePosition.x, imagePosition.y, imageSize.x, imageSize.y);

    const auto mode = (m_gizmoLocal || m_gizmoOperation == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    if (ImGuizmo::Manipulate(&view._11, &projection._11, static_cast<ImGuizmo::OPERATION>(m_gizmoOperation), mode, &world._11))
    {
        // Convert the manipulated world transform back into the node's local space.
        XMMATRIX newWorld = XMLoadFloat4x4(&world);
        XMMATRIX parentWorld = XMMatrixIdentity();
        if (auto parent = node->GetParent())
        {
            parentWorld = parent->GetWorldTransform();
        }
        node->SetLocalTransform(newWorld * XMMatrixInverse(nullptr, parentWorld));
    }

    return ImGuizmo::IsUsing() || ImGuizmo::IsOver();
}

void Editor::PickNode(const ImVec2& mousePosition)
{
    if (!m_camera || m_viewportWidth == 0 || m_viewportHeight == 0)
    {
        return;
    }

    // Build a world space ray through the mouse position.
    const float ndcX = 2.0f * mousePosition.x / m_viewportWidth - 1.0f;
    const float ndcY = 1.0f - 2.0f * mousePosition.y / m_viewportHeight;
    const XMMATRIX inverseViewProjection = XMMatrixInverse(nullptr, m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix());
    const XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), inverseViewProjection);
    const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), inverseViewProjection);
    const XMVECTOR direction = XMVector3Normalize(farPoint - nearPoint);

    std::shared_ptr<SceneNode> closestNode;
    float closestDistance = FLT_MAX;

    for (const auto& entry : m_scenes)
    {
        ForEachNode(entry.scene->GetRootNode(), [&](const std::shared_ptr<SceneNode>& node)
        {
            const XMMATRIX world = node->GetWorldTransform();
            for (const auto& mesh : node->GetMeshes())
            {
                if (!mesh)
                {
                    continue;
                }
                BoundingBox worldBox;
                mesh->GetAABB().Transform(worldBox, world);

                float distance = 0.0f;
                if (worldBox.Intersects(nearPoint, direction, distance) && distance < closestDistance)
                {
                    closestDistance = distance;
                    closestNode = node;
                }
            }
        });
    }

    m_selectedNode = closestNode;
}

// ------------------------------------------------------------------------------------------------
// Hierarchy and inspector

void Editor::DrawHierarchy()
{
    if (!m_showHierarchy)
    {
        return;
    }
    m_hierarchyFocused = false;
    if (ImGui::Begin(g_hierarchyWindow, &m_showHierarchy))
    {
        m_hierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (m_scenes.empty())
        {
            ImGui::TextColored(g_colorTextDim, "No scenes. Use Editor::AddScene().");
        }
        for (const auto& entry : m_scenes)
        {
            DrawHierarchyNode(entry.scene->GetRootNode(), entry.name.c_str());
        }

        if (m_pendingReparent.node)
        {
            ApplyPendingReparent();
        }

        // Clicking empty space clears the selection.
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            m_selectedNode.reset();
        }

        // Models dropped on the empty part of the hierarchy are placed in front of the camera.
        const ImVec2 remaining = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(std::max(1.0f, remaining.x), std::max(40.0f, remaining.y)));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(g_assetPayload))
            {
                SpawnModelInFrontOfCamera(PathFromUTF8(static_cast<const char*>(payload->Data)));
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::End();
}

void Editor::ApplyPendingReparent()
{
    Reparent request = m_pendingReparent;
    m_pendingReparent = {};

    // Find the shared pointer of the dragged node.
    std::shared_ptr<SceneNode> node;
    for (const auto& entry : m_scenes)
    {
        ForEachNode(entry.scene->GetRootNode(), [&](const std::shared_ptr<SceneNode>& candidate)
        {
            if (candidate.get() == request.node) node = candidate;
        });
    }
    auto oldParent = node ? node->GetParent() : nullptr;
    if (!node || !oldParent || !request.newParent || oldParent == request.newParent)
    {
        return;
    }

    // A node cannot become a child of itself or of one of its children.
    for (auto ancestor = request.newParent; ancestor; ancestor = ancestor->GetParent())
    {
        if (ancestor == node)
        {
            return;
        }
    }

    // Keep the node where it is in the world.
    const XMMATRIX world = node->GetWorldTransform();
    oldParent->RemoveChild(node);
    request.newParent->AddChild(node);
    node->SetLocalTransform(world * XMMatrixInverse(nullptr, request.newParent->GetWorldTransform()));
}

void Editor::DrawHierarchyNode(const std::shared_ptr<SceneNode>& node, const char* label)
{
    if (!node)
    {
        return;
    }

    const auto& children = node->GetChildren();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (m_selectedNode.lock() == node)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string& name = node->GetName();
    const char* text = label ? label : (name.empty() ? "<unnamed>" : name.c_str());

    ImGui::PushID(node.get());
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", text);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        m_selectedNode = node;
    }

    // Drag a node onto another to make it a child (scene roots stay where they are).
    if (node->GetParent() && ImGui::BeginDragDropSource())
    {
        SceneNode* dragged = node.get();
        ImGui::SetDragDropPayload(g_nodePayload, &dragged, sizeof(dragged));
        ImGui::Text("Move %s", text);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(g_nodePayload))
        {
            m_pendingReparent = { *static_cast<SceneNode* const*>(payload->Data), node };
        }
        ImGui::EndDragDropTarget();
    }
    if (!node->GetMeshes().empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(g_colorTextDim, "(%zu mesh%s)", node->GetMeshes().size(), node->GetMeshes().size() == 1 ? "" : "es");
    }

    if (open && !children.empty())
    {
        for (const auto& child : children)
        {
            DrawHierarchyNode(child, nullptr);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Editor::DrawInspector()
{
    if (!m_showInspector)
    {
        return;
    }

    // Selecting an object (in the Scene or Hierarchy) brings the Inspector to the front of its dock.
    const SceneNode* selected = m_selectedNode.lock().get();
    if (selected != m_inspectedNode)
    {
        m_inspectedNode = selected;
        if (selected)
        {
            ShowDockedTab(g_inspectorWindow);
        }
    }

    if (!ImGui::Begin(g_inspectorWindow, &m_showInspector))
    {
        ImGui::End();
        return;
    }

    auto node = m_selectedNode.lock();
    if (!node)
    {
        ImGui::TextColored(g_colorTextDim, "Select an object in the Hierarchy or the Scene.");
        ImGui::End();
        return;
    }

    char name[256];
    strncpy_s(name, node->GetName().c_str(), _TRUNCATE);
    if (ImGui::InputText("Name", name, sizeof(name)))
    {
        node->SetName(name);
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        XMVECTOR scale, rotation, translation;
        if (XMMatrixDecompose(&scale, &rotation, &translation, node->GetLocalTransform()))
        {
            XMFLOAT3 position, scaling;
            XMStoreFloat3(&position, translation);
            XMStoreFloat3(&scaling, scale);
            XMFLOAT3 angles = QuaternionToPitchYawRoll(rotation);
            XMFLOAT3 degrees(XMConvertToDegrees(angles.x), XMConvertToDegrees(angles.y), XMConvertToDegrees(angles.z));

            bool changed = false;
            changed |= ImGui::DragFloat3("Position", &position.x, 0.05f);
            changed |= ImGui::DragFloat3("Rotation", &degrees.x, 0.5f);
            changed |= ImGui::DragFloat3("Scale", &scaling.x, 0.01f);

            if (changed)
            {
                XMMATRIX local = XMMatrixScaling(scaling.x, scaling.y, scaling.z) *
                    XMMatrixRotationRollPitchYaw(XMConvertToRadians(degrees.x), XMConvertToRadians(degrees.y), XMConvertToRadians(degrees.z)) *
                    XMMatrixTranslation(position.x, position.y, position.z);
                node->SetLocalTransform(local);
            }
        }
        else
        {
            ImGui::TextColored(g_colorTextDim, "Transform cannot be decomposed.");
        }

        XMFLOAT3 worldPosition;
        XMStoreFloat3(&worldPosition, node->GetWorldTransform().r[3]);
        ImGui::TextColored(g_colorTextDim, "World position: %.2f, %.2f, %.2f", worldPosition.x, worldPosition.y, worldPosition.z);
    }

    const auto& meshes = node->GetMeshes();
    if (!meshes.empty() && ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            const auto& mesh = meshes[i];
            if (!mesh)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::TreeNodeEx("##mesh", ImGuiTreeNodeFlags_DefaultOpen, "Mesh %zu", i))
            {
                const auto& aabb = mesh->GetAABB();
                ImGui::Text("Vertices: %zu   Indices: %zu", mesh->GetVertexCount(), mesh->GetIndexCount());
                ImGui::TextColored(g_colorTextDim, "AABB center (%.2f, %.2f, %.2f) extents (%.2f, %.2f, %.2f)",
                    aabb.Center.x, aabb.Center.y, aabb.Center.z, aabb.Extents.x, aabb.Extents.y, aabb.Extents.z);

                if (auto material = mesh->GetMaterial())
                {
                    DrawMaterial(material, static_cast<int>(i));
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    ImGui::End();
}

void Editor::DrawMaterial(const std::shared_ptr<Material>& materialPtr, int index)
{
    Material& material = *materialPtr;
    ImGui::PushID(index);
    if (ImGui::TreeNodeEx("##material", ImGuiTreeNodeFlags_DefaultOpen, "Material"))
    {
        MaterialProperties properties = material.GetMaterialProperties();
        bool changed = false;
        changed |= ImGui::ColorEdit4("Diffuse", &properties.diffuse.x, ImGuiColorEditFlags_Float);
        changed |= ImGui::ColorEdit4("Emissive", &properties.emissive.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        changed |= ImGui::ColorEdit4("Specular", &properties.specular.x, ImGuiColorEditFlags_Float);
        changed |= ImGui::ColorEdit4("Ambient", &properties.ambient.x, ImGuiColorEditFlags_Float);
        changed |= ImGui::SliderFloat("Metallic", &properties.metallic, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Roughness", &properties.roughness, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Opacity", &properties.opacity, 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Specular Power", &properties.specularPower, 1.0f, 0.0f, 1024.0f);
        if (changed)
        {
            material.SetMaterialProperties(properties);
        }

        ImGui::Spacing();
        ImGui::TextColored(g_colorTextDim, "Textures (drop images from Assets onto a slot)");

        const float thumbnailSize = 56.0f;
        for (int type = 0; type < static_cast<int>(Material::TextureType::NumTypes); ++type)
        {
            const auto slot = static_cast<Material::TextureType>(type);
            auto texture = material.GetTexture(slot);

            // Show the common slots even when empty so textures can be dropped onto them.
            const bool commonSlot = slot == Material::TextureType::Diffuse || slot == Material::TextureType::Normal ||
                slot == Material::TextureType::MetallicRoughness || slot == Material::TextureType::Emissive;
            if (!texture && !commonSlot)
            {
                continue;
            }

            ImGui::PushID(type);
            if (texture)
            {
                TextureThumbnail(texture, thumbnailSize);
            }
            else
            {
                const ImVec2 position = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(thumbnailSize, thumbnailSize));
                ImGui::GetWindowDrawList()->AddRect(position, ImVec2(position.x + thumbnailSize, position.y + thumbnailSize),
                    ImGui::GetColorU32(g_colorBackground4), 3.0f);
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(g_assetPayload))
                {
                    DropTextureOnMaterial(PathFromUTF8(static_cast<const char*>(payload->Data)), materialPtr, type);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(GetTextureSlotName(slot));
            if (texture)
            {
                const auto desc = texture->GetD3D12ResourceDesc();
                ImGui::TextColored(g_colorTextDim, "%s", GetShortTextureLabel(*texture).c_str());
                ImGui::TextColored(g_colorTextDim, "%s  %s  %u mips", DescribeDimensions(desc).c_str(),
                    FormatUtils::GetName(desc.Format), desc.MipLevels);
                if (ImGui::SmallButton("Clear"))
                {
                    material.SetTexture(slot, nullptr);
                }
            }
            else
            {
                ImGui::TextColored(g_colorTextDim, "Empty");
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// ------------------------------------------------------------------------------------------------
// Textures

void Editor::DrawTextureBrowser()
{
    if (!m_showTextureBrowser)
    {
        return;
    }
    if (!ImGui::Begin(g_textureBrowserWindow, &m_showTextureBrowser))
    {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(250.0f);
    ImGui::InputTextWithHint("##filter", "Filter by name...", m_textureFilter, sizeof(m_textureFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Show editor textures", &m_showEditorTextures);

    auto textures = Application::Get().GetLiveTextures();

    // Filter and sort by name.
    struct Row
    {
        std::shared_ptr<Texture> texture;
        std::string              label;
    };
    std::vector<Row> rows;
    rows.reserve(textures.size());
    uint64_t totalMemory = 0;
    for (auto& texture : textures)
    {
        if (!texture->GetD3D12Resource())
        {
            continue;
        }
        if (!m_showEditorTextures && texture->GetName() == g_previewTextureName)
        {
            continue;
        }
        std::string label = GetTextureLabel(*texture);
        if (!ContainsCaseInsensitive(label, m_textureFilter))
        {
            continue;
        }
        totalMemory += GetTextureMemory(*texture);
        rows.push_back({ std::move(texture), std::move(label) });
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.label < b.label; });

    ImGui::SameLine();
    ImGui::TextColored(g_colorTextDim, "%zu textures, %s", rows.size(), FormatBytes(totalMemory).c_str());

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    const float rowHeight = 36.0f;

    if (ImGui::BeginTable("##textures", 6, tableFlags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, rowHeight);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableHeadersRow();

        const auto viewed = m_viewedTexture.lock();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()), rowHeight + ImGui::GetStyle().CellPadding.y * 2.0f);
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const Row& row = rows[i];
                const auto desc = row.texture->GetD3D12ResourceDesc();

                ImGui::PushID(row.texture.get());
                ImGui::TableNextRow(0, rowHeight);

                ImGui::TableSetColumnIndex(0);
                TextureThumbnail(row.texture, rowHeight);

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Selectable(row.label.c_str(), viewed == row.texture,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowHeight)))
                {
                    InspectTexture(row.texture);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s  (%u mips)", DescribeDimensions(desc).c_str(), desc.MipLevels);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(FormatUtils::GetName(desc.Format));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(DescribeUsage(desc).c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(FormatBytes(GetTextureMemory(*row.texture)).c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

void Editor::DrawTextureViewer()
{
    if (!m_showTextureViewer)
    {
        return;
    }

    // Looked up before Begin: when the viewer is a hidden tab, inspecting the texture brings it to the front.
    if (!m_startupTextureName.empty())
    {
        if (auto startupTexture = FindLiveTexture(m_startupTextureName))
        {
            InspectTexture(startupTexture);
            m_startupTextureName.clear();
        }
    }

    if (m_focusTextureViewer)
    {
        ImGui::SetNextWindowFocus();
        m_focusTextureViewer = false;
    }
    if (!ImGui::Begin(g_textureViewerWindow, &m_showTextureViewer, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }

    // Textures that are recreated (e.g. on resize or when parameters change) are found again by name.
    auto texture = m_viewedTexture.lock();
    if (!texture && !m_viewedTextureName.empty())
    {
        texture = FindLiveTexture(m_viewedTextureName);
        m_viewedTexture = texture;
    }
    if (!texture || !texture->GetD3D12Resource())
    {
        ImGui::TextColored(g_colorTextDim, "Select a texture in the Textures panel or click a material texture in the Inspector.");
        ImGui::End();
        return;
    }

    const auto desc = texture->GetD3D12ResourceDesc();
    auto& settings = *m_viewerSettings;
    const uint32_t sliceCount = TexturePreview::GetSliceCount(*texture);
    const bool is3D = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;

    ImGui::TextUnformatted(GetTextureLabel(*texture).c_str());
    ImGui::SameLine();
    ImGui::TextColored(g_colorTextDim, "  %s  %s  %u mips  %s", DescribeDimensions(desc).c_str(), FormatUtils::GetName(desc.Format),
        desc.MipLevels, DescribeUsage(desc).c_str());

    if (!IsTextureSettled(texture))
    {
        ImGui::TextColored(g_colorTextDim, "Waiting for background loading to finish...");
        ImGui::End();
        return;
    }
    if (!TexturePreview::CanPreview(*texture))
    {
        ImGui::TextColored(g_colorTextDim, "This texture cannot be previewed (format %s, %u samples).", FormatUtils::GetName(desc.Format),
            desc.SampleDesc.Count);
        ImGui::End();
        return;
    }

    // ---- Controls
    const DXGI_FORMAT readableFormat = FormatUtils::GetShaderReadableFormat(texture->GetShaderResourceFormat());
    const uint32_t channelCount = FormatUtils::GetChannelCount(readableFormat);

    const ImVec4 channelColors[4] = { ImVec4(0.8f, 0.25f, 0.25f, 1.0f), ImVec4(0.25f, 0.7f, 0.3f, 1.0f),
                                      ImVec4(0.3f, 0.45f, 0.9f, 1.0f), ImVec4(0.6f, 0.6f, 0.6f, 1.0f) };
    const char* channelNames[4] = { "R", "G", "B", "A" };
    for (int c = 0; c < 4; ++c)
    {
        const bool enabled = settings.channels[c];
        ImGui::PushStyleColor(ImGuiCol_Button, enabled ? channelColors[c] : g_colorBackground3);
        ImGui::BeginDisabled(c >= static_cast<int>(channelCount) && c != 3);
        if (ImGui::Button(channelNames[c], ImVec2(ImGui::GetFrameHeight(), 0.0f)))
        {
            // Ctrl+click isolates a channel.
            if (ImGui::GetIO().KeyCtrl)
            {
                for (int other = 0; other < 4; ++other) settings.channels[other] = other == c;
            }
            else
            {
                settings.channels[c] = !settings.channels[c];
            }
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle channel (Ctrl+click to isolate)");
        ImGui::SameLine(0.0f, 2.0f);
    }
    ImGui::SameLine(0.0f, 12.0f);

    if (desc.MipLevels > 1)
    {
        int mip = static_cast<int>(settings.mip);
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderInt("Mip", &mip, 0, desc.MipLevels - 1))
        {
            settings.mip = static_cast<uint32_t>(mip);
        }
        ImGui::SameLine();
    }
    if (sliceCount > 1)
    {
        const uint32_t slices = is3D ? std::max<uint32_t>(1, sliceCount >> settings.mip) : sliceCount;
        int slice = static_cast<int>(std::min(settings.slice, slices - 1));
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderInt(is3D ? "Depth" : (sliceCount == 6 ? "Face" : "Slice"), &slice, 0, static_cast<int>(slices) - 1))
        {
            settings.slice = static_cast<uint32_t>(slice);
        }
        ImGui::SameLine();
    }

    ImGui::Checkbox("sRGB", &settings.srgb);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Encode values as sRGB for display (use for linear color)");
    ImGui::SameLine();
    ImGui::Checkbox("Abs", &settings.absolute);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show absolute values (signed data such as displacement)");
    ImGui::SameLine();
    ImGui::Checkbox("Filter", &settings.linearFilter);

    // Range
    ImGui::SetNextItemWidth(220.0f);
    const float range = std::max(std::abs(settings.rangeMax - settings.rangeMin), 1e-4f);
    ImGui::DragFloatRange2("Range", &settings.rangeMin, &settings.rangeMax, range * 0.005f, 0.0f, 0.0f, "%.4g", "%.4g");
    ImGui::SameLine();

    const bool rangePending = m_textureReadback->IsRangePending();
    ImGui::BeginDisabled(rangePending || !TextureReadback::CanReadback(*texture));
    if (ImGui::Button(rangePending ? "Auto..." : "Auto"))
    {
        m_textureReadback->RequestRange(texture, settings.mip, settings.slice);
        m_viewerAutoRangePending = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Fit the range to the min/max of the visible channels");
    ImGui::SameLine();
    if (ImGui::Button("0..1")) { settings.rangeMin = 0.0f; settings.rangeMax = 1.0f; }
    ImGui::SameLine();
    if (ImGui::Button("-1..1")) { settings.rangeMin = -1.0f; settings.rangeMax = 1.0f; }

    if (texture->IsDepthTexture())
    {
        ImGui::SameLine();
        ImGui::Checkbox("Linear depth", &settings.linearizeDepth);
        if (settings.linearizeDepth)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::DragFloatRange2("Near/Far", &settings.nearZ, &settings.farZ, 0.1f, 0.001f, 100000.0f, "%.3g", "%.4g");
        }
    }

    // Apply an auto range when it arrives.
    const auto& rangeResult = m_textureReadback->GetRange();
    if (m_viewerAutoRangePending && !rangePending && rangeResult.valid)
    {
        float minValue = FLT_MAX, maxValue = -FLT_MAX;
        for (int c = 0; c < 4; ++c)
        {
            if (settings.channels[c] && c < static_cast<int>(channelCount))
            {
                minValue = std::min(minValue, rangeResult.min[c]);
                maxValue = std::max(maxValue, rangeResult.max[c]);
            }
        }
        if (minValue <= maxValue)
        {
            if (settings.linearizeDepth)
            {
                auto linearize = [&settings](float depth) { return settings.nearZ * settings.farZ / (settings.farZ - depth * (settings.farZ - settings.nearZ)); };
                minValue = linearize(minValue);
                maxValue = linearize(maxValue);
            }
            if (settings.absolute)
            {
                const float largest = std::max(std::abs(minValue), std::abs(maxValue));
                minValue = (minValue < 0.0f && maxValue > 0.0f) ? 0.0f : std::min(std::abs(minValue), std::abs(maxValue));
                maxValue = largest;
            }
            settings.rangeMin = minValue;
            settings.rangeMax = maxValue > minValue ? maxValue : minValue + 1e-4f;
        }
        m_viewerAutoRangePending = false;
    }

    // Zoom controls
    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button("Fit")) m_viewerFit = true;
    ImGui::SameLine();
    if (ImGui::Button("1:1")) { m_viewerFit = false; m_viewerZoom = 1.0f; m_viewerPan = ImVec2(0.0f, 0.0f); }

    // ---- Canvas
    const uint32_t mip = std::min<uint32_t>(settings.mip, desc.MipLevels - 1);
    const uint32_t mipWidth = std::max<uint32_t>(1, static_cast<uint32_t>(desc.Width >> mip));
    const uint32_t mipHeight = std::max<uint32_t>(1, desc.Height >> mip);

    // The preview is rendered at the mip's resolution (capped), so each texel maps to one preview pixel.
    const float previewScale = std::min(1.0f, 4096.0f / std::max(mipWidth, mipHeight));
    auto image = m_texturePreview->Request(g_viewerPreviewKey, texture, settings,
        static_cast<uint32_t>(mipWidth * previewScale), static_cast<uint32_t>(mipHeight * previewScale));

    const float statusHeight = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y = std::max(50.0f, canvasSize.y - statusHeight);
    const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    const bool canvasActive = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();

    if (m_viewerFit)
    {
        m_viewerZoom = std::min(canvasSize.x / mipWidth, canvasSize.y / mipHeight);
        m_viewerPan = ImVec2(0.0f, 0.0f);
    }

    const ImVec2 canvasCenter(canvasPosition.x + canvasSize.x * 0.5f, canvasPosition.y + canvasSize.y * 0.5f);

    // Zoom around the mouse cursor.
    if (canvasHovered && io.MouseWheel != 0.0f)
    {
        const float oldZoom = m_viewerZoom;
        m_viewerZoom = std::clamp(m_viewerZoom * std::pow(1.2f, io.MouseWheel), 0.01f, 256.0f);
        const ImVec2 mouseFromCenter(io.MousePos.x - canvasCenter.x - m_viewerPan.x, io.MousePos.y - canvasCenter.y - m_viewerPan.y);
        const float ratio = m_viewerZoom / oldZoom;
        m_viewerPan.x -= mouseFromCenter.x * (ratio - 1.0f);
        m_viewerPan.y -= mouseFromCenter.y * (ratio - 1.0f);
        m_viewerFit = false;
    }
    if (canvasActive && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)))
    {
        m_viewerPan.x += io.MouseDelta.x;
        m_viewerPan.y += io.MouseDelta.y;
        m_viewerFit = false;
    }

    const ImVec2 imageSize(mipWidth * m_viewerZoom, mipHeight * m_viewerZoom);
    const ImVec2 imageMin(canvasCenter.x + m_viewerPan.x - imageSize.x * 0.5f, canvasCenter.y + m_viewerPan.y - imageSize.y * 0.5f);
    const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasPosition, ImVec2(canvasPosition.x + canvasSize.x, canvasPosition.y + canvasSize.y), true);
    drawList->AddRectFilled(canvasPosition, ImVec2(canvasPosition.x + canvasSize.x, canvasPosition.y + canvasSize.y),
        ImGui::GetColorU32(g_colorBackground0));
    if (image)
    {
        drawList->AddImage(m_gui->GetTextureID(image), imageMin, imageMax);
    }
    drawList->AddRect(imageMin, imageMax, ImGui::GetColorU32(g_colorBackground4));

    // Hovered texel: highlight it and read its value back.
    bool texelHovered = false;
    uint32_t texelX = 0, texelY = 0;
    if (canvasHovered && io.MousePos.x >= imageMin.x && io.MousePos.y >= imageMin.y && io.MousePos.x < imageMax.x && io.MousePos.y < imageMax.y)
    {
        texelX = std::min(mipWidth - 1, static_cast<uint32_t>((io.MousePos.x - imageMin.x) / m_viewerZoom));
        texelY = std::min(mipHeight - 1, static_cast<uint32_t>((io.MousePos.y - imageMin.y) / m_viewerZoom));
        texelHovered = true;
        m_textureReadback->RequestTexel(texture, mip, settings.slice, texelX, texelY);

        if (m_viewerZoom >= 6.0f)
        {
            const ImVec2 texelMin(imageMin.x + texelX * m_viewerZoom, imageMin.y + texelY * m_viewerZoom);
            drawList->AddRect(texelMin, ImVec2(texelMin.x + m_viewerZoom, texelMin.y + m_viewerZoom), IM_COL32(255, 255, 0, 200));
        }
    }
    drawList->PopClipRect();

    // ---- Status line
    ImGui::TextColored(g_colorTextDim, "Zoom %.0f%%", m_viewerZoom * 100.0f);
    if (texelHovered)
    {
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::Text("Texel (%u, %u)", texelX, texelY);

        const auto& texel = m_textureReadback->GetTexel();
        if (texel.valid)
        {
            ImGui::SameLine(0.0f, 20.0f);
            const char* format = FormatUtils::GetClass(readableFormat) == FormatClass::Float ? "%s %.5g" : "%s %.0f";
            for (uint32_t c = 0; c < std::min(channelCount, 4u); ++c)
            {
                ImGui::TextColored(channelColors[c], format, channelNames[c], texel.value[c]);
                ImGui::SameLine(0.0f, 12.0f);
            }
            if (settings.linearizeDepth)
            {
                const float linear = settings.nearZ * settings.farZ / (settings.farZ - texel.value[0] * (settings.farZ - settings.nearZ));
                ImGui::TextColored(g_colorTextDim, "(linear %.4g)", linear);
            }
            ImGui::NewLine();
        }
        else if (!TextureReadback::CanReadback(*texture))
        {
            ImGui::SameLine(0.0f, 20.0f);
            ImGui::TextColored(g_colorTextDim, "(values cannot be read back for this format)");
        }
    }

    ImGui::End();
}

void Editor::DrawStats()
{
    if (!m_showStats)
    {
        return;
    }
    if (ImGui::Begin(g_statsWindow, &m_showStats))
    {
        float averageMs = 0.0f, maxMs = 0.0f;
        for (float ms : m_frameTimes)
        {
            averageMs += ms;
            maxMs = std::max(maxMs, ms);
        }
        averageMs /= IM_ARRAYSIZE(m_frameTimes);

        ImGui::Text("%.1f FPS", averageMs > 0.0f ? 1000.0f / averageMs : 0.0f);
        ImGui::Text("Frame: %.2f ms (max %.2f ms)", averageMs, maxMs);

        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f ms", averageMs);
        ImGui::PlotLines("##frametimes", m_frameTimes, IM_ARRAYSIZE(m_frameTimes), m_frameTimeIndex, overlay, 0.0f,
            std::max(33.3f, maxMs), ImVec2(-1.0f, 60.0f));

        ImGui::Separator();
        ImGui::Text("Viewport: %u x %u", m_viewportWidth, m_viewportHeight);
        ImGui::Text("Live textures: %zu", Application::Get().GetLiveTextures().size());

        size_t nodeCount = 0, meshCount = 0;
        for (const auto& entry : m_scenes)
        {
            ForEachNode(entry.scene->GetRootNode(), [&](const std::shared_ptr<SceneNode>& node)
            {
                ++nodeCount;
                meshCount += node->GetMeshes().size();
            });
        }
        ImGui::Text("Scenes: %zu  Nodes: %zu  Meshes: %zu", m_scenes.size(), nodeCount, meshCount);
    }
    ImGui::End();
}
