#include "DX12/dx12_includes.h"

#include "editor/editor.h"

#include <algorithm>
#include <chrono>

#include "core/application.h"
#include "DX12/shader.h"
#include "DX12/shader_library.h"

#include "editor_internal.h"

using namespace EV;
using namespace EV::EditorInternal;

namespace
{
    std::string FormatTimeSince(std::chrono::steady_clock::time_point time)
    {
        const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - time).count();
        char text[32];
        if (seconds < 60.0f)
        {
            snprintf(text, sizeof(text), "%.0f s ago", seconds);
        }
        else
        {
            snprintf(text, sizeof(text), "%.0f min ago", seconds / 60.0f);
        }
        return text;
    }

    void DrawShaderStatus(const Shader& shader)
    {
        if (shader.GetSourcePath().empty())
        {
            ImGui::TextColored(g_colorTextDim, "No source");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s.hlsl was not found in a shaders folder next to or above the executable.\n"
                    "Add its folder with ShaderLibrary::AddSourceDirectory.", shader.GetName().c_str());
            }
        }
        else if (shader.GetTarget().empty())
        {
            ImGui::TextColored(g_colorTextDim, "Unknown profile");
        }
        else if (!shader.GetError().empty())
        {
            ImGui::TextColored(g_colorError, "Error");
        }
        else if (shader.GetReloadSerial() > 0)
        {
            ImGui::TextColored(g_colorAccent, "Reloaded");
        }
        else
        {
            ImGui::TextColored(g_colorTextDim, "Built");
        }
    }
}

void Editor::UpdateShaderReloads()
{
    const auto& library = Application::Get().GetShaderLibrary();
    if (library.GetReloadCount() == m_shaderReloadCount)
    {
        return;
    }

    // Report the shaders recompiled since the last frame in the status bar.
    std::shared_ptr<Shader> failed;
    std::string reloaded;
    size_t reloadedCount = 0;
    for (const auto& shader : library.GetShaders())
    {
        if (shader->GetReloadSerial() <= m_shaderReloadCount)
        {
            continue;
        }
        if (!shader->GetError().empty())
        {
            failed = failed ? failed : shader;
        }
        else
        {
            reloaded = shader->GetName();
            ++reloadedCount;
        }
    }
    m_shaderReloadCount = library.GetReloadCount();

    if (failed)
    {
        // Show the error in the Shaders panel.
        m_statusMessage = failed->GetName() + ".hlsl has errors, see the Shaders panel";
        m_statusMessageTime = 5.0f;
        m_selectedShader = failed;
        m_showShaders = true;
        ShowDockedTab(g_shadersWindow);
    }
    else if (reloadedCount > 0)
    {
        m_statusMessage = reloadedCount == 1 ? "Reloaded " + reloaded + ".hlsl" : "Reloaded " + std::to_string(reloadedCount) + " shaders";
        m_statusMessageTime = 3.0f;
    }
}

void Editor::DrawShaders()
{
    if (!m_showShaders)
    {
        return;
    }
    if (!ImGui::Begin(g_shadersWindow, &m_showShaders))
    {
        ImGui::End();
        return;
    }

    auto& library = Application::Get().GetShaderLibrary();
    const auto& shaders = library.GetShaders();

    bool autoReload = library.IsAutoReloadEnabled();
    if (ImGui::Checkbox("Reload on save", &autoReload))
    {
        library.SetAutoReload(autoReload);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Recompile a shader when its .hlsl file, or a file it includes, is saved");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload All"))
    {
        library.ReloadAll();
    }
    ImGui::SameLine();
    const auto errorCount = std::count_if(shaders.begin(), shaders.end(), [](const auto& shader) { return !shader->GetError().empty(); });
    if (errorCount > 0)
    {
        ImGui::TextColored(g_colorError, "%zu of %zu shaders have errors", static_cast<size_t>(errorCount), shaders.size());
    }
    else
    {
        ImGui::TextColored(g_colorTextDim, "%zu shaders", shaders.size());
    }

    ImGui::PushStyleColor(ImGuiCol_Text, g_colorTextDim);
    ImGui::TextWrapped("Compiler: %s", library.GetCompilerStatus().c_str());
    ImGui::PopStyleColor();

    // The table, and below it the details of the selected shader.
    const auto selected = m_selectedShader.lock();
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float tableHeight = selected ? std::max(availableHeight * 0.5f, ImGui::GetFrameHeightWithSpacing() * 3.0f) : availableHeight;

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##shaders", 5, tableFlags, ImVec2(0.0f, tableHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Last reload", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Reload").x + ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::TableHeadersRow();

        for (const auto& shader : shaders)
        {
            ImGui::PushID(shader.get());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(shader->GetName().c_str(), selected == shader,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
            {
                m_selectedShader = selected == shader ? std::weak_ptr<Shader>() : shader;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(shader->GetTarget().empty() ? "?" : ToUTF8(shader->GetTarget()).c_str());

            ImGui::TableSetColumnIndex(2);
            DrawShaderStatus(*shader);

            ImGui::TableSetColumnIndex(3);
            if (shader->GetReloadSerial() > 0)
            {
                ImGui::TextUnformatted(FormatTimeSince(shader->GetReloadTime()).c_str());
            }
            else
            {
                ImGui::TextColored(g_colorTextDim, "-");
            }

            ImGui::TableSetColumnIndex(4);
            if (shader->CanReload() && ImGui::SmallButton("Reload"))
            {
                library.Reload(*shader);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (selected)
    {
        ImGui::Separator();
        ImGui::Text("%s", selected->GetName().c_str());
        ImGui::SameLine();
        DrawShaderStatus(*selected);

        ImGui::TextColored(g_colorTextDim, "Compiled: %s", ToUTF8(selected->GetCompiledPath().wstring()).c_str());
        if (!selected->GetSourcePath().empty())
        {
            ImGui::TextColored(g_colorTextDim, "Source: %s", ToUTF8(selected->GetSourcePath().wstring()).c_str());
            for (const auto& file : selected->GetWatchedFiles())
            {
                if (file != selected->GetSourcePath())
                {
                    ImGui::TextColored(g_colorTextDim, "Includes: %s", ToUTF8(file.wstring()).c_str());
                }
            }
        }

        if (!selected->GetError().empty())
        {
            // Read only, but the text can be selected and copied.
            const std::string& error = selected->GetError();
            ImGui::PushStyleColor(ImGuiCol_Text, g_colorError);
            ImGui::InputTextMultiline("##error", const_cast<char*>(error.c_str()), error.size() + 1, ImVec2(-1.0f, -1.0f),
                ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
        }
    }

    ImGui::End();
}
