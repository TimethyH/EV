/**
 *  @file rc_toy.cpp
 *
 *  @brief RC-0, the 2D radiance cascades toy. See rc_toy.h and docs/path-tracing/knowledge.md "### RC-0".
 */

#include "rc_toy.h"

#include <algorithm>
#include <d3dx12.h>

#include "core/EV.h"
#include "compute_pso.h"

using namespace EV;

// Root parameter slots of the paint PSO (must match the order they are created in Initialize).
namespace PaintRoot
{
    enum : uint32_t
    {
        CanvasUAV = 0, // descriptor table, u0
        Brush = 1,     // 8 root constants, b0 = RcToy::PaintConstants
    };
}

// ── Resources ────────────────────────────────────────────────────────────
// Called once from Ocean::LoadContent, after the device exists.
void RcToy::Initialize()
{
    DXGI_FORMAT rcCanvasFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto rcDesc = CD3DX12_RESOURCE_DESC::Tex2D(rcCanvasFormat, CanvasSize, CanvasSize);
    rcDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // The canvas: rgb = emitted light (linear HDR), a = opacity. The UAV flag lets the paint shader write it.
    // Created through Application so the editor's texture browser tracks it; new memory starts zeroed = empty canvas.
    m_canvas = Application::Get().CreateTexture(rcDesc);
    m_canvas->SetName(L"RC-0 Canvas");

    // The paint shader's root signature, same pattern as the ocean's FFT pass (Ocean::LoadContent):
    // a descriptor table with one UAV for the canvas, and the brush as root constants (no constant buffer needed
    // for 8 values: they are stored in the root signature itself and set per dispatch).
    CD3DX12_DESCRIPTOR_RANGE1 canvasRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 UAV starting at u0
    CD3DX12_ROOT_PARAMETER1 paintRootParameters[2];
    paintRootParameters[PaintRoot::CanvasUAV].InitAsDescriptorTable(1, &canvasRange);
    paintRootParameters[PaintRoot::Brush].InitAsConstants(sizeof(PaintConstants) / sizeof(uint32_t), 0); // b0
    m_paintPSO = std::make_shared<OceanCompute>(L"/rc_paint.cso", paintRootParameters,
        static_cast<uint32_t>(_countof(paintRootParameters)));
}

void RcToy::Release()
{
    m_paintPSO.reset();
    m_canvas.reset();
}

// ── Render graph ─────────────────────────────────────────────────────────
// Every frame, from Ocean::RecordRenderGraph, before the graph is executed.
void RcToy::AddPasses(RenderGraph& renderGraph)
{
    if (!m_enabled || !m_canvas)
    {
        return;
    }

    // The canvas lives outside the graph (it must keep what was painted), so it is imported every frame.
    const RGTexture canvas = renderGraph.Import("RC-0 Canvas", m_canvas);

    // Step 1c: paint while the left mouse button is held over the canvas image (m_painting comes from DrawPanel).
    // Write(canvas) tells the graph this pass writes the canvas as a UAV, so it inserts the transition barrier.
    if (m_painting)
    {
        renderGraph.AddPass("RC-0 Paint")
            .Write(canvas)
            .Execute([this, canvas](RGPassContext& context)
                {
                    // Runs later, inside rg.Execute: reads the brush members as they are at that moment.
                    CommandList& commandList = context.GetCommandList();

                    // PSO + root signature first: the slots below belong to this root signature.
                    m_paintPSO->SetPipelineState(commandList);
                    std::shared_ptr<Texture> canvasTexture = context.GetTexture(canvas);

                    // The brush as 8 root constants. They are copied into the command list by the call below,
                    // so a struct on the stack is fine.
                    PaintConstants constants = {};
                    constants.centerX = m_brushPixelX;
                    constants.centerY = m_brushPixelY;
                    constants.intensity = m_brushIntensity;
                    constants.mode = static_cast<uint32_t>(m_brushMode);
                    constants.radius = m_brushRadius;
                    constants.color[0] = m_brushColor[0];
                    constants.color[1] = m_brushColor[1];
                    constants.color[2] = m_brushColor[2];

                    commandList.SetCompute32BitConstants(PaintRoot::Brush, sizeof(PaintConstants) / sizeof(float), &constants);
					commandList.SetUnorderedAccessView(PaintRoot::CanvasUAV, 0, canvasTexture, 0);
					// Group counts, not threads: 16 = numthreads(16, 16, 1) in rc_paint.hlsl -> 64 x 64 groups, one thread per pixel.
					commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);
            });
    }

    // Steps 2-5 add their passes here (reference, cascades, fluence).
}

// ── Panel ────────────────────────────────────────────────────────────────
// The "RC-0" tab next to the Scene view. Shows the canvas as a square that fills the panel.
void RcToy::DrawPanel(Editor& editor)
{
    ImGui::Checkbox("Enable", &m_enabled);

    if (!m_canvas)
    {
        ImGui::TextDisabled("No canvas yet (RcToy::Initialize).");
        return;
    }

    // Brush settings (step 1c). The mode numbers are what rc_paint.hlsl receives.
    int mode = static_cast<int>(m_brushMode);
    ImGui::RadioButton("Light", &mode, static_cast<int>(BrushMode::Light));
    ImGui::SameLine();
    ImGui::RadioButton("Wall", &mode, static_cast<int>(BrushMode::Wall));
    ImGui::SameLine();
    ImGui::RadioButton("Eraser", &mode, static_cast<int>(BrushMode::Eraser));
    m_brushMode = static_cast<BrushMode>(mode);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Radius", &m_brushRadius, 1.0f, 64.0f, "%.0f px");
    if (m_brushMode == BrushMode::Light)
    {
        ImGui::ColorEdit3("Colour", m_brushColor, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Intensity", &m_brushIntensity, 0.1f, 0.0f, 100.0f, "%.1f");
    }

    // Largest square that fits in the space left in the panel.
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float side = std::max(1.0f, std::min(available.x, available.y));

    // ImGui alpha-blends images, and the canvas's alpha is OPACITY (0 = empty space), so a plain Image() of an empty
    // canvas is fully transparent. Drawing it over a dark grey background turns that into a readable view:
    // empty = grey (background shows through), wall = black (rgb 0, alpha 1), light = its colour (rgb > 0, alpha 1).
    const ImVec4 emptySpaceColor(0.15f, 0.15f, 0.15f, 1.0f);
    ImGui::ImageWithBg(editor.GetGUI()->GetTextureID(m_canvas), ImVec2(side, side), ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f), emptySpaceColor);

    // Step 1b: which canvas pixel is under the mouse, and is the left button painting there?
    // The image's rectangle and the mouse are both in SCREEN pixels, so (mouse - min) is the position inside the image;
    // dividing by the image size gives 0..1, times CanvasSize gives canvas pixels (y points down in both).
    // No clamp needed: the brush is only used while hovered, and ImGui's hover test is min <= mouse < max.
	ImVec2 rectMin = ImGui::GetItemRectMin(); // top-left of the image, screen space
	ImVec2 rectMax = ImGui::GetItemRectMax(); // bottom-right of the image, screen space
	ImVec2 mousePos = ImGui::GetMousePos(); // mouse, screen space

	m_brushPixelX = (mousePos.x - rectMin.x) / (rectMax.x - rectMin.x) * CanvasSize;
	m_brushPixelY = (mousePos.y - rectMin.y) / (rectMax.y - rectMin.y) * CanvasSize;
	m_painting = ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	ImGui::Text("Brush pixel: (%.1f, %.1f), painting: %s", m_brushPixelX, m_brushPixelY, m_painting ? "yes" : "no");

}
