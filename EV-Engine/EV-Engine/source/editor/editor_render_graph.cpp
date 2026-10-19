#include "DX12/dx12_includes.h"

#include "editor/editor.h"

#include <algorithm>
#include <cmath>

#include "DX12/render_graph.h"
#include "resources/texture.h"
#include "UI/format_utils.h"

#include "editor_internal.h"

using namespace EV;
using namespace EV::EditorInternal;

namespace
{
    // Timeline cell colors: what a pass does with a texture.
    const ImU32 g_writeColor = IM_COL32(214, 120, 72, 255);
    const ImU32 g_readColor = IM_COL32(76, 150, 212, 255);
    const ImU32 g_lifetimeColor = IM_COL32(255, 255, 255, 18);

    const char* GetPassType(const RenderGraphDebugInfo::Pass& pass)
    {
        if (pass.raster)
        {
            return "Raster";
        }
        const bool uav = std::any_of(pass.accesses.begin(), pass.accesses.end(),
            [](const RenderGraphDebugInfo::Access& access) { return access.access == RGAccess::UnorderedAccess; });
        return uav ? "Compute" : "Other";
    }

    std::string DescribeTexture(const RGTextureDesc& desc)
    {
        char text[96];
        if (desc.depthOrArraySize > 1)
        {
            snprintf(text, sizeof(text), "%ux%ux%u  %s", desc.width, desc.height, desc.depthOrArraySize, FormatUtils::GetName(desc.format));
        }
        else
        {
            snprintf(text, sizeof(text), "%ux%u  %s", desc.width, desc.height, FormatUtils::GetName(desc.format));
        }
        return text;
    }
}

void Editor::CaptureRenderGraphTexture(const std::string& pass, const std::string& resource)
{
    // The copy is made while the next frame executes; the Texture Viewer opens it once it exists.
    m_renderGraph->SetCapture(pass, resource);
    m_inspectRenderGraphCapture = true;
}

void Editor::DrawRenderGraph()
{
    if (!m_showRenderGraph || !m_renderGraph)
    {
        return;
    }
    if (!ImGui::Begin(g_renderGraphWindow, &m_showRenderGraph))
    {
        ImGui::End();
        return;
    }

    const RenderGraphDebugInfo& info = m_renderGraph->GetDebugInfo();

    if (m_inspectRenderGraphCapture && info.captureFound && m_renderGraph->GetCapturedTexture())
    {
        InspectTexture(m_renderGraph->GetCapturedTexture());
        m_inspectRenderGraphCapture = false;
    }

    // Summary
    const auto culledCount = std::count_if(info.passes.begin(), info.passes.end(), [](const auto& pass) { return pass.culled; });
    bool profiling = m_renderGraph->IsProfilingEnabled();
    if (ImGui::Checkbox("GPU timings", &profiling))
    {
        m_renderGraph->SetProfilingEnabled(profiling);
    }
    ImGui::SameLine();
    ImGui::TextColored(g_colorTextDim, "%zu passes (%zu skipped), %zu textures, %s transient",
        info.passes.size(), static_cast<size_t>(culledCount), info.resources.size(), FormatBytes(info.transientMemory).c_str());
    if (profiling)
    {
        ImGui::SameLine();
        ImGui::TextColored(g_colorTextDim, "  GPU %.2f ms", info.gpuMilliseconds);
    }

    // Capture: a texture as it was right after a pass.
    if (!m_renderGraph->GetCapturePass().empty())
    {
        ImGui::TextColored(info.captureFound ? g_colorAccent : g_colorError, "Capturing '%s' after '%s'%s",
            m_renderGraph->GetCaptureResource().c_str(), m_renderGraph->GetCapturePass().c_str(),
            info.captureFound ? "" : " (that pass did not use it this frame)");
        ImGui::SameLine();
        if (ImGui::SmallButton("View") && m_renderGraph->GetCapturedTexture())
        {
            InspectTexture(m_renderGraph->GetCapturedTexture());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop"))
        {
            m_renderGraph->ClearCapture();
            m_inspectRenderGraphCapture = false;
        }
    }
    else
    {
        ImGui::TextColored(g_colorTextDim, "Click a cell in the Timeline (or Capture in a pass) to see a texture as it was after that pass");
    }

    if (ImGui::BeginTabBar("##rendergraphtabs"))
    {
        if (ImGui::BeginTabItem("Passes"))
        {
            DrawRenderGraphPasses(info);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Timeline"))
        {
            DrawRenderGraphTimeline(info);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Resources"))
        {
            DrawRenderGraphResources(info);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void Editor::DrawRenderGraphPasses(const RenderGraphDebugInfo& info)
{
    if (m_selectedGraphPass >= static_cast<int>(info.passes.size()))
    {
        m_selectedGraphPass = -1;
    }

    float maxMilliseconds = 0.0f;
    for (const auto& pass : info.passes)
    {
        maxMilliseconds = std::max(maxMilliseconds, pass.gpuMilliseconds);
    }

    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float tableHeight = m_selectedGraphPass >= 0 ? std::max(availableHeight * 0.55f, ImGui::GetFrameHeightWithSpacing() * 4.0f) : availableHeight;

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##passes", 4, tableFlags, ImVec2(0.0f, tableHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000").x);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(info.passes.size()); ++i)
        {
            const auto& pass = info.passes[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(g_colorTextDim, "%d", i);

            ImGui::TableSetColumnIndex(1);
            if (pass.culled)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, g_colorTextDim);
            }
            if (ImGui::Selectable(pass.name.c_str(), m_selectedGraphPass == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
            {
                m_selectedGraphPass = m_selectedGraphPass == i ? -1 : i;
            }
            if (pass.culled)
            {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Skipped: nothing uses what this pass writes");
                }
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(g_colorTextDim, "%s", pass.culled ? "Skipped" : GetPassType(pass));

            ImGui::TableSetColumnIndex(3);
            if (pass.gpuMilliseconds >= 0.0f)
            {
                // A bar relative to the slowest pass, with the time on it.
                const float fraction = maxMilliseconds > 0.0f ? pass.gpuMilliseconds / maxMilliseconds : 0.0f;
                char label[32];
                snprintf(label, sizeof(label), "%.3f ms", pass.gpuMilliseconds);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, g_colorAccent);
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, ImGui::GetTextLineHeight()), label);
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextColored(g_colorTextDim, "-");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (m_selectedGraphPass < 0)
    {
        return;
    }

    // The textures the selected pass uses.
    const auto& pass = info.passes[m_selectedGraphPass];
    ImGui::Separator();
    ImGui::Text("%s", pass.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(g_colorTextDim, "(%s)", pass.culled ? "skipped" : GetPassType(pass));

    for (size_t i = 0; i < pass.accesses.size(); ++i)
    {
        const auto& access = pass.accesses[i];
        const auto& resource = info.resources[access.resource];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextColored(IsWriteAccess(access.access) ? ImGui::ColorConvertU32ToFloat4(g_writeColor) : ImGui::ColorConvertU32ToFloat4(g_readColor),
            "%s", IsWriteAccess(access.access) ? "Write" : "Read ");
        ImGui::SameLine();
        ImGui::Text("%s", resource.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(g_colorTextDim, "%s, %s%s", GetAccessName(access.access), DescribeTexture(resource.desc).c_str(),
            resource.imported ? ", imported" : "");
        if (!pass.culled)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Capture"))
            {
                CaptureRenderGraphTexture(pass.name, resource.name);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Show %s as it is right after this pass", resource.name.c_str());
            }
        }
        ImGui::PopID();
    }
}

void Editor::DrawRenderGraphTimeline(const RenderGraphDebugInfo& info)
{
    // Rows are textures, columns are passes in execution order (like the Frostbite FrameGraph visualization).
    const float cell = std::floor(ImGui::GetTextLineHeight() * 1.2f);
    float nameWidth = 0.0f;
    for (const auto& resource : info.resources)
    {
        nameWidth = std::max(nameWidth, ImGui::CalcTextSize(resource.name.c_str()).x);
    }
    nameWidth += ImGui::GetStyle().ItemSpacing.x * 2.0f;

    const float width = nameWidth + cell * info.passes.size();
    const float height = cell * (info.resources.size() + 1);

    ImGui::BeginChild("##timeline", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##timelinecanvas", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::ColorConvertFloat4ToU32(g_colorTextDim);

    // Header: pass numbers (hover for the name).
    for (size_t p = 0; p < info.passes.size(); ++p)
    {
        char number[8];
        snprintf(number, sizeof(number), "%zu", p);
        const ImVec2 position(origin.x + nameWidth + cell * p + 2.0f, origin.y);
        drawList->AddText(position, info.passes[p].culled ? dimColor : textColor, number);
    }

    int hoveredPass = -1;
    int hoveredResource = -1;
    if (hovered && mouse.x >= origin.x + nameWidth)
    {
        hoveredPass = static_cast<int>((mouse.x - origin.x - nameWidth) / cell);
        hoveredResource = static_cast<int>((mouse.y - origin.y) / cell) - 1;
        if (hoveredPass >= static_cast<int>(info.passes.size()))
        {
            hoveredPass = -1;
        }
        if (hoveredResource >= static_cast<int>(info.resources.size()))
        {
            hoveredResource = -1;
        }
    }

    for (size_t r = 0; r < info.resources.size(); ++r)
    {
        const auto& resource = info.resources[r];
        const float y = origin.y + cell * (r + 1);
        drawList->AddText(ImVec2(origin.x, y), resource.imported ? textColor : ImGui::ColorConvertFloat4ToU32(g_colorAccent), resource.name.c_str());

        // How long the texture is in use.
        if (resource.firstPass >= 0)
        {
            drawList->AddRectFilled(ImVec2(origin.x + nameWidth + cell * resource.firstPass, y + 1.0f),
                ImVec2(origin.x + nameWidth + cell * (resource.lastPass + 1), y + cell - 1.0f), g_lifetimeColor);
        }
    }

    for (size_t p = 0; p < info.passes.size(); ++p)
    {
        const auto& pass = info.passes[p];
        for (const auto& access : pass.accesses)
        {
            const float x = origin.x + nameWidth + cell * p;
            const float y = origin.y + cell * (access.resource + 1);
            ImU32 color = IsWriteAccess(access.access) ? g_writeColor : g_readColor;
            if (pass.culled)
            {
                color = (color & 0x00FFFFFF) | 0x50000000;
            }
            drawList->AddRectFilled(ImVec2(x + 2.0f, y + 2.0f), ImVec2(x + cell - 2.0f, y + cell - 2.0f), color, 2.0f);
        }
    }

    // Highlight the capture.
    for (size_t p = 0; p < info.passes.size(); ++p)
    {
        if (info.passes[p].name != m_renderGraph->GetCapturePass())
        {
            continue;
        }
        for (size_t r = 0; r < info.resources.size(); ++r)
        {
            if (info.resources[r].name == m_renderGraph->GetCaptureResource())
            {
                const ImVec2 min(origin.x + nameWidth + cell * p, origin.y + cell * (r + 1));
                drawList->AddRect(min, ImVec2(min.x + cell, min.y + cell), IM_COL32(255, 255, 0, 255), 2.0f, 0, 2.0f);
            }
        }
    }

    // Hover: what the cell is; click: capture it.
    if (hoveredPass >= 0)
    {
        const auto& pass = info.passes[hoveredPass];
        const RenderGraphDebugInfo::Access* access = nullptr;
        if (hoveredResource >= 0)
        {
            for (const auto& candidate : pass.accesses)
            {
                if (candidate.resource == static_cast<uint32_t>(hoveredResource))
                {
                    access = &candidate;
                }
            }
        }

        ImGui::BeginTooltip();
        ImGui::Text("%d: %s%s", hoveredPass, pass.name.c_str(), pass.culled ? " (skipped)" : "");
        if (pass.gpuMilliseconds >= 0.0f)
        {
            ImGui::TextColored(g_colorTextDim, "%.3f ms", pass.gpuMilliseconds);
        }
        if (access)
        {
            ImGui::Text("%s: %s", info.resources[hoveredResource].name.c_str(), GetAccessName(access->access));
            if (!pass.culled)
            {
                ImGui::TextColored(g_colorTextDim, "Click to see it as it is after this pass");
            }
        }
        ImGui::EndTooltip();

        if (clicked && access && !pass.culled)
        {
            CaptureRenderGraphTexture(pass.name, info.resources[hoveredResource].name);
        }
    }

    ImGui::EndChild();
}

void Editor::DrawRenderGraphResources(const RenderGraphDebugInfo& info)
{
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##resources", 5, tableFlags))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch, 2.5f);
    ImGui::TableSetupColumn("Size / format", ImGuiTableColumnFlags_WidthStretch, 2.5f);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Used by passes", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("View").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < info.resources.size(); ++i)
    {
        const auto& resource = info.resources[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(resource.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(DescribeTexture(resource.desc).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(resource.imported ? g_colorTextDim : g_colorAccent, "%s", resource.imported ? "Imported" : "Transient");
        ImGui::TableSetColumnIndex(3);
        if (resource.firstPass >= 0)
        {
            ImGui::Text("%d - %d", resource.firstPass, resource.lastPass);
        }
        else
        {
            ImGui::TextColored(g_colorTextDim, "unused");
        }
        ImGui::TableSetColumnIndex(4);
        const auto texture = resource.texture.lock();
        if (texture && ImGui::SmallButton("View"))
        {
            // The texture as it is at the end of the frame.
            InspectTexture(texture);
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}
