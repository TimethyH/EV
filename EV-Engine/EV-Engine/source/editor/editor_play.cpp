#include "DX12/dx12_includes.h"

#include "editor/editor.h"

#include <algorithm>

#include "core/application.h"
#include "core/camera.h"
#include "DX12/scene.h"
#include "DX12/scene_node.h"
#include "UI/GUI.h"

#include "editor_internal.h"

using namespace EV;
using namespace EV::EditorInternal;
using namespace DirectX;

namespace
{
    // How long the "Esc: stop playing" hint stays on screen after play mode starts.
    constexpr float g_playHintDuration = 3.0f;
}

// ------------------------------------------------------------------------------------------------
// Public

void Editor::Play()
{
    if (m_playing)
    {
        return;
    }

    // Remember where everything is, so play mode can move things around.
    m_playSnapshot = {};
    for (const auto& entry : m_scenes)
    {
        ForEachNode(entry.scene->GetRootNode(), [this](const std::shared_ptr<SceneNode>& node)
        {
            XMFLOAT4X4 transform;
            XMStoreFloat4x4(&transform, node->GetLocalTransform());
            m_playSnapshot.transforms.emplace_back(node, transform);
        });
    }
    if (m_camera)
    {
        XMStoreFloat3(&m_playSnapshot.cameraPosition, m_camera->GetTranslation());
        XMStoreFloat4(&m_playSnapshot.cameraRotation, m_camera->GetRotation());
        m_playSnapshot.cameraFov = m_camera->GetFov();
    }

    // An asset dragged into the scene is dropped where it is.
    CancelModelDrag();

    m_playing = true;
    m_playHintTime = g_playHintDuration;
    m_viewportDragging = false;
    m_hierarchyFocused = false;
    if (m_playModeCallback)
    {
        m_playModeCallback(true);
    }
}

void Editor::StopPlaying()
{
    if (!m_playing)
    {
        return;
    }

    // Without the editor there is nothing to go back to.
    if (m_standalonePlay)
    {
        Application::Get().Stop();
        return;
    }

    m_playing = false;
    m_viewportDragging = false;

    for (const auto& [weakNode, transform] : m_playSnapshot.transforms)
    {
        if (auto node = weakNode.lock())
        {
            node->SetLocalTransform(XMLoadFloat4x4(&transform));
        }
    }
    if (m_camera)
    {
        m_camera->SetTranslation(XMLoadFloat3(&m_playSnapshot.cameraPosition));
        m_camera->SetRotation(XMLoadFloat4(&m_playSnapshot.cameraRotation));
        m_camera->SetFov(m_playSnapshot.cameraFov);
        if (m_cameraChangedCallback)
        {
            m_cameraChangedCallback();
        }
    }
    m_playSnapshot = {};

    if (m_playModeCallback)
    {
        m_playModeCallback(false);
    }
}

// ------------------------------------------------------------------------------------------------
// Frame

void Editor::UpdatePlayMode()
{
    // --play: the application was set up by now (callbacks, camera), so it can start.
    if (m_startupPlay)
    {
        m_startupPlay = false;
        m_standalonePlay = true;
        Play();
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput)
    {
        if (!m_standalonePlay && ImGui::IsKeyPressed(ImGuiKey_F5, false))
        {
            m_playing ? StopPlaying() : Play();
        }
        else if (m_playing && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            if (m_standalonePlay && m_standaloneEscapeCallback)
            {
                m_standaloneEscapeCallback();
            }
            else
            {
                StopPlaying();
            }
        }
    }

    if (!m_playing)
    {
        return;
    }

    m_playHintTime = std::max(0.0f, m_playHintTime - io.DeltaTime);

    // The scene fills the window.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const uint32_t width = static_cast<uint32_t>(std::max(1.0f, viewport->Size.x));
    const uint32_t height = static_cast<uint32_t>(std::max(1.0f, viewport->Size.y));
    if (width != m_viewportWidth || height != m_viewportHeight)
    {
        m_viewportWidth = width;
        m_viewportHeight = height;
        m_viewportResized = true;
    }

    // The game gets the mouse and keyboard, unless one of its own ImGui windows wants them.
    m_viewportMousePosition = ImVec2(io.MousePos.x - viewport->Pos.x, io.MousePos.y - viewport->Pos.y);
    m_viewportHovered = !io.WantCaptureMouse;
    m_viewportFocused = true;
    if (m_viewportHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
    {
        m_viewportDragging = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        m_viewportDragging = false;
    }
}

void Editor::DrawPlayView()
{
    // Drawn behind every ImGui window, so the game's own windows stay on top.
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 min = viewport->Pos;
    const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    const ImVec2 center(min.x + viewport->Size.x * 0.5f, min.y + viewport->Size.y * 0.5f);

    if (m_sceneTexture)
    {
        drawList->AddImage(m_gui->GetTextureID(m_sceneTexture), min, max);
    }

    auto drawCenteredText = [drawList](const ImVec2& position, const char* text, float alpha)
    {
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 padding(12.0f, 6.0f);
        const ImVec2 topLeft(position.x - size.x * 0.5f, position.y - size.y * 0.5f);
        drawList->AddRectFilled(ImVec2(topLeft.x - padding.x, topLeft.y - padding.y),
            ImVec2(topLeft.x + size.x + padding.x, topLeft.y + size.y + padding.y), IM_COL32(0, 0, 0, static_cast<int>(160 * alpha)), 4.0f);
        drawList->AddText(topLeft, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), text);
    };

    if (IsLoadingScene())
    {
        drawCenteredText(center, "Loading...", 1.0f);
    }
    else if (!m_standalonePlay && m_playHintTime > 0.0f)
    {
        const float alpha = std::min(1.0f, m_playHintTime);
        drawCenteredText(ImVec2(center.x, min.y + 30.0f), "Playing   Esc or F5: back to the editor", alpha);
    }
}

void Editor::DrawPlayButton()
{
    const char* label = "  Play  ";
    const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(g_colorAccent.x, g_colorAccent.y, g_colorAccent.z, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_colorAccent);
    if (ImGui::Button(label))
    {
        Play();
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Play the scene without the editor (F5)\nEsc or F5 to come back");
    }
}
