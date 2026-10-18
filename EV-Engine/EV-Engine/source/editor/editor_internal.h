#pragma once

// Helpers shared by the editor's source files.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"

#include "DX12/scene_node.h"

namespace EV::EditorInternal
{
    // Drag and drop payload types.
    constexpr const char* g_assetPayload = "EV_ASSET"; // UTF-8 path of a file in the asset folder.
    constexpr const char* g_nodePayload = "EV_NODE";   // SceneNode* being dragged in the Hierarchy.

    // Window names used by more than one source file.
    constexpr const char* g_textureViewerWindow = "Texture Viewer";
    constexpr const char* g_modelPreviewWindow = "Model Preview";

    // Editor palette (dark, similar to the Kudzu editor).
    inline const ImVec4 g_colorBackground0 = ImVec4(0.078f, 0.078f, 0.078f, 1.0f);
    inline const ImVec4 g_colorBackground1 = ImVec4(0.110f, 0.110f, 0.110f, 1.0f);
    inline const ImVec4 g_colorBackground2 = ImVec4(0.141f, 0.141f, 0.141f, 1.0f);
    inline const ImVec4 g_colorBackground3 = ImVec4(0.173f, 0.173f, 0.173f, 1.0f);
    inline const ImVec4 g_colorBackground4 = ImVec4(0.204f, 0.204f, 0.204f, 1.0f);
    inline const ImVec4 g_colorText = ImVec4(0.878f, 0.878f, 0.878f, 1.0f);
    inline const ImVec4 g_colorTextDim = ImVec4(0.578f, 0.578f, 0.578f, 1.0f);
    inline const ImVec4 g_colorAccent = ImVec4(0.298f, 0.588f, 0.831f, 1.0f);

    inline std::string ToUTF8(const std::wstring& wide)
    {
        if (wide.empty())
        {
            return {};
        }
        int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    // Paths travel through drag & drop payloads as UTF-8.
    inline std::filesystem::path PathFromUTF8(const char* utf8)
    {
        const int size = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        std::wstring wide(size > 0 ? size - 1 : 0, L'\0');
        if (size > 1)
        {
            MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), size);
        }
        return std::filesystem::path(wide);
    }

    // Bring a docked window's tab to the front without focusing it. Focusing would take the keyboard from
    // the Scene viewport and cancel a drag in progress (focusing releases the dragged item).
    // Returns false while the window does not exist or is not docked yet.
    inline bool ShowDockedTab(const char* windowName)
    {
        ImGuiWindow* window = ImGui::FindWindowByName(windowName);
        if (!window || !window->DockNode || !window->DockNode->TabBar)
        {
            return false;
        }
        window->DockNode->TabBar->NextSelectedTabId = window->TabId;
        return true;
    }

    inline bool ContainsCaseInsensitive(const std::string& text, const char* filter)
    {
        if (!filter || !*filter)
        {
            return true;
        }
        auto it = std::search(text.begin(), text.end(), filter, filter + strlen(filter),
            [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        return it != text.end();
    }

    // Visit every node of a scene graph.
    template<typename Func>
    void ForEachNode(const std::shared_ptr<SceneNode>& node, Func&& func)
    {
        if (!node)
        {
            return;
        }
        func(node);
        for (const auto& child : node->GetChildren())
        {
            ForEachNode(child, func);
        }
    }
}
