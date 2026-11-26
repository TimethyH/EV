/**
 *  @file rc_toy.cpp
 *
 *  @brief RC-0, the 2D radiance cascades toy. See rc_toy.h.
 */

#include "rc_toy.h"

#include <algorithm>
#include <cfloat>
#include <random>
#include <string>
#include <vector>
#include <d3dx12.h>

#include "core/EV.h"
#include "compute_pso.h"

using namespace EV;

// Root parameter slots (same order as created in Initialize).
namespace PaintRoot
{
    enum : uint32_t
    {
        CanvasUAV = 0, // u0
        Brush = 1,     // b0
    };
}

namespace ReferenceRoot
{
    enum : uint32_t
    {
        CanvasSRV = 0,    // t0
        ReferenceUAV = 1, // u0
        Constants = 2,    // b0
    };
}

namespace CascadeRoot
{
    enum : uint32_t
    {
        CanvasSRV = 0,  // t0
        CascadeUAV = 1, // u0
        Constants = 2,  // b0
        UpperSRV = 3,   // t1
    };
}

namespace ViewRoot
{
    enum : uint32_t
    {
        SourceSRV = 0, // t0
        ViewUAV = 1,   // u0
        Constants = 2, // b0
        SecondSRV = 3, // t1
    };
}

namespace FluenceRoot
{
    enum : uint32_t
    {
        Cascade0SRV = 0, // t0
        FluenceUAV = 1,  // u0
        CanvasSRV = 2,   // t1
    };
}

// ── Resources ────────────────────────────────────────────────────────────
void RcToy::Initialize()
{
    DXGI_FORMAT rcCanvasFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto rcDesc = CD3DX12_RESOURCE_DESC::Tex2D(rcCanvasFormat, CanvasSize, CanvasSize);
    rcDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // New memory starts zeroed = empty canvas.
    m_canvas = Application::Get().CreateTexture(rcDesc);
    m_canvas->SetName(L"RC-0 Canvas");

    CD3DX12_DESCRIPTOR_RANGE1 canvasRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 UAV starting at u0
    CD3DX12_ROOT_PARAMETER1 paintRootParameters[2];
    paintRootParameters[PaintRoot::CanvasUAV].InitAsDescriptorTable(1, &canvasRange);
    paintRootParameters[PaintRoot::Brush].InitAsConstants(sizeof(PaintConstants) / sizeof(uint32_t), 0); // b0
    m_paintPSO = std::make_shared<OceanCompute>(L"/rc_paint.cso", paintRootParameters,
        static_cast<uint32_t>(_countof(paintRootParameters)));

    // fp32: the running average moves by 1/n per frame, fp16 would stop changing after ~1000 frames.
    auto referenceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, CanvasSize, CanvasSize);
    referenceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    m_reference = Application::Get().CreateTexture(referenceDesc);
    m_reference->SetName(L"RC-0 Reference");

    CD3DX12_DESCRIPTOR_RANGE1 referenceCanvasRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // 1 SRV at t0
    CD3DX12_DESCRIPTOR_RANGE1 referenceOutputRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 UAV at u0
    CD3DX12_ROOT_PARAMETER1 referenceRootParameters[3];
    referenceRootParameters[ReferenceRoot::CanvasSRV].InitAsDescriptorTable(1, &referenceCanvasRange);
    referenceRootParameters[ReferenceRoot::ReferenceUAV].InitAsDescriptorTable(1, &referenceOutputRange);
    referenceRootParameters[ReferenceRoot::Constants].InitAsConstants(sizeof(ReferenceConstants) / sizeof(uint32_t), 0);
    m_referencePSO = std::make_shared<OceanCompute>(L"/rc_reference.cso", referenceRootParameters,
        static_cast<uint32_t>(_countof(referenceRootParameters)));

    // fp16 is enough: cascades are rewritten from scratch every frame.
    auto cascadeDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, CanvasSize, CanvasSize);
    cascadeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    m_cascades[0] = Application::Get().CreateTexture(cascadeDesc);
    m_cascades[0]->SetName(L"RC-0 Cascade A");
    m_cascades[1] = Application::Get().CreateTexture(cascadeDesc);
    m_cascades[1]->SetName(L"RC-0 Cascade B");
    m_viewTexture = Application::Get().CreateTexture(cascadeDesc);
    m_viewTexture->SetName(L"RC-0 View");

    CD3DX12_DESCRIPTOR_RANGE1 cascadeCanvasRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    CD3DX12_DESCRIPTOR_RANGE1 cascadeOutputRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0
    CD3DX12_DESCRIPTOR_RANGE1 cascadeUpperRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // t1
    CD3DX12_ROOT_PARAMETER1 cascadeRootParameters[4];
    cascadeRootParameters[CascadeRoot::CanvasSRV].InitAsDescriptorTable(1, &cascadeCanvasRange);
    cascadeRootParameters[CascadeRoot::CascadeUAV].InitAsDescriptorTable(1, &cascadeOutputRange);
    cascadeRootParameters[CascadeRoot::Constants].InitAsConstants(sizeof(CascadeConstants) / sizeof(uint32_t), 0);
    cascadeRootParameters[CascadeRoot::UpperSRV].InitAsDescriptorTable(1, &cascadeUpperRange);
    m_cascadePSO = std::make_shared<OceanCompute>(L"/rc_cascade.cso", cascadeRootParameters,
        static_cast<uint32_t>(_countof(cascadeRootParameters)));

    CD3DX12_DESCRIPTOR_RANGE1 viewSourceRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    CD3DX12_DESCRIPTOR_RANGE1 viewOutputRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0
    CD3DX12_DESCRIPTOR_RANGE1 viewSecondRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1
    CD3DX12_ROOT_PARAMETER1 viewRootParameters[4];
    viewRootParameters[ViewRoot::SourceSRV].InitAsDescriptorTable(1, &viewSourceRange);
    viewRootParameters[ViewRoot::ViewUAV].InitAsDescriptorTable(1, &viewOutputRange);
    viewRootParameters[ViewRoot::Constants].InitAsConstants(sizeof(ViewConstants) / sizeof(uint32_t), 0);
    viewRootParameters[ViewRoot::SecondSRV].InitAsDescriptorTable(1, &viewSecondRange);
    m_viewPSO = std::make_shared<OceanCompute>(L"/rc_view.cso", viewRootParameters,
        static_cast<uint32_t>(_countof(viewRootParameters)));

    m_fluence = Application::Get().CreateTexture(cascadeDesc);
    m_fluence->SetName(L"RC-0 Fluence");

    CD3DX12_DESCRIPTOR_RANGE1 fluenceCascadeRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    CD3DX12_DESCRIPTOR_RANGE1 fluenceOutputRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // u0
    CD3DX12_DESCRIPTOR_RANGE1 fluenceCanvasRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // t1
    CD3DX12_ROOT_PARAMETER1 fluenceRootParameters[3];
    fluenceRootParameters[FluenceRoot::Cascade0SRV].InitAsDescriptorTable(1, &fluenceCascadeRange);
    fluenceRootParameters[FluenceRoot::FluenceUAV].InitAsDescriptorTable(1, &fluenceOutputRange);
    fluenceRootParameters[FluenceRoot::CanvasSRV].InitAsDescriptorTable(1, &fluenceCanvasRange);
    m_fluencePSO = std::make_shared<OceanCompute>(L"/rc_fluence.cso", fluenceRootParameters,
        static_cast<uint32_t>(_countof(fluenceRootParameters)));
}

void RcToy::Release()
{
    m_fluencePSO.reset();
    m_fluence.reset();
    m_viewPSO.reset();
    m_viewTexture.reset();
    m_cascadePSO.reset();
    m_cascades[0].reset();
    m_cascades[1].reset();
    m_referencePSO.reset();
    m_reference.reset();
    m_paintPSO.reset();
    m_canvas.reset();
}

RcToy::ReferenceConstants RcToy::NextReferenceConstants()
{
    if (m_painting || m_clearRequested || m_raysPerFrame != m_lastRaysPerFrame)
    {
        m_referenceFrame = 0;
        m_lastRaysPerFrame = m_raysPerFrame;
    }

    ReferenceConstants constants;
    constants.frameIndex = m_referenceFrame;
    constants.raysPerFrame = static_cast<uint32_t>(m_raysPerFrame);
    ++m_referenceFrame;
    return constants;
}

// ── Render graph ─────────────────────────────────────────────────────────
void RcToy::AddPasses(RenderGraph& renderGraph)
{
    if (!m_enabled || !m_canvas)
    {
        return;
    }

    const RGTexture canvas = renderGraph.Import("RC-0 Canvas", m_canvas);

    // "Clear canvas" = one eraser stroke past the corners. Captured by value: the flag is reset before the lambda runs.
    const bool clearing = m_clearRequested;

    // Scatter tool: positions decided while recording, fixed seed. Also sets m_clearRequested (clear first).
    std::vector<PaintConstants> scatterLights;
    if (m_scatterRequested)
    {
        std::mt19937 random(1234u);
        const float margin = m_brushRadius + 2.0f; // keep every light fully on the canvas
        std::uniform_real_distribution<float> position(margin, static_cast<float>(CanvasSize) - margin);
        scatterLights.reserve(static_cast<size_t>(m_scatterCount));
        for (int i = 0; i < m_scatterCount; ++i)
        {
            PaintConstants light = {};
            light.centerX = position(random);
            light.centerY = position(random);
            light.radius = m_brushRadius;
            light.mode = static_cast<uint32_t>(BrushMode::Light);
            light.color[0] = m_brushColor[0];
            light.color[1] = m_brushColor[1];
            light.color[2] = m_brushColor[2];
            light.intensity = m_brushIntensity;
            scatterLights.push_back(light);
        }
        m_scatterRequested = false;
    }

    if (m_painting || clearing)
    {
        renderGraph.AddPass("RC-0 Paint")
            .Write(canvas)
            .Execute([this, canvas, clearing, scatterLights](RGPassContext& context)
                {
                    CommandList& commandList = context.GetCommandList();

                    m_paintPSO->SetPipelineState(commandList);
                    std::shared_ptr<Texture> canvasTexture = context.GetTexture(canvas);

                    PaintConstants constants = {};
                    constants.centerX = m_brushPixelX;
                    constants.centerY = m_brushPixelY;
                    constants.intensity = m_brushIntensity;
                    constants.mode = static_cast<uint32_t>(m_brushMode);
                    constants.radius = m_brushRadius;
                    constants.color[0] = m_brushColor[0];
                    constants.color[1] = m_brushColor[1];
                    constants.color[2] = m_brushColor[2];
                    if (clearing)
                    {
                        constants.centerX = CanvasSize * 0.5f;
                        constants.centerY = CanvasSize * 0.5f;
                        constants.radius = static_cast<float>(CanvasSize);
                        constants.mode = static_cast<uint32_t>(BrushMode::Eraser);
                    }

                    commandList.SetCompute32BitConstants(PaintRoot::Brush, sizeof(PaintConstants) / sizeof(float), &constants);
					commandList.SetUnorderedAccessView(PaintRoot::CanvasUAV, 0, canvasTexture, 0);
					// Group counts: numthreads(16, 16, 1) -> 64 x 64 groups.
					commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);

                    // One dispatch per scattered light. The barrier lets the clear finish first; overlapping lights
                    // write the same value, so they need none between them.
                    if (!scatterLights.empty())
                    {
                        commandList.UAVBarrier(canvasTexture);
                        for (const PaintConstants& light : scatterLights)
                        {
                            commandList.SetCompute32BitConstants(PaintRoot::Brush, sizeof(PaintConstants) / sizeof(float), &light);
                            commandList.SetUnorderedAccessView(PaintRoot::CanvasUAV, 0, canvasTexture, 0);
                            commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);
                        }
                    }
            });
    }

    // After the paint pass. Constants taken while recording and captured by value (m_referenceFrame moves on).
    const RGTexture referenceTexture = renderGraph.Import("RC-0 Reference", m_reference);
    const ReferenceConstants referenceConstants = NextReferenceConstants();
    m_clearRequested = false;

    renderGraph.AddPass("RC-0 Reference")
        .Read(canvas, EV::RGAccess::NonPixelShaderRead)
        .Write(referenceTexture)
        .Execute([this, canvas, referenceTexture, referenceConstants](RGPassContext& context)
            {
                CommandList& commandList = context.GetCommandList();
                m_referencePSO->SetPipelineState(commandList);

                std::shared_ptr<Texture> canvasTexture = context.GetTexture(canvas);
                std::shared_ptr<Texture> refTexture = context.GetTexture(referenceTexture);

                commandList.SetCompute32BitConstants(ReferenceRoot::Constants, sizeof(ReferenceConstants) / sizeof(float), &referenceConstants);
				commandList.SetShaderResourceView(ReferenceRoot::CanvasSRV, 0, canvasTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList.SetUnorderedAccessView(ReferenceRoot::ReferenceUAV, 0, refTexture, 0);
				commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);
            });

    // The rest only feeds the panel; the Canvas view shows m_canvas directly.
    if (m_view == View::Canvas)
    {
        return;
    }

    RGTexture viewSource = referenceTexture;

    // Fluence and Difference always need the whole merge down to c0.
    const bool fluenceView = (m_view == View::Fluence || m_view == View::Difference);
    if (m_view == View::Cascade || fluenceView)
    {
        const bool merge = fluenceView ? true : m_merge;

        // Top-down, c5 first, one pass per cascade (the graph puts the UAV -> SRV barrier between them).
        const RGTexture cascadeTextures[2] = {
            renderGraph.Import("RC-0 Cascade A", m_cascades[0]),
            renderGraph.Import("RC-0 Cascade B", m_cascades[1]),
        };
        const uint32_t shown = fluenceView ? 0u : static_cast<uint32_t>(m_shownCascade);
        const uint32_t top = merge ? CascadeCount - 1 : shown;

        for (int k = static_cast<int>(top); k >= static_cast<int>(shown); --k)
        {
            const RGTexture target = cascadeTextures[CascadeSlot(k)];
            const RGTexture upper = cascadeTextures[CascadeSlot(k) ^ 1u];

            CascadeConstants cascadeConstants;
            cascadeConstants.cascadeIndex = static_cast<uint32_t>(k);
            // c5 has nothing above it (upper is still bound: a root table must be set before a dispatch).
            cascadeConstants.mergeWithUpper = (merge && k < static_cast<int>(CascadeCount) - 1) ? 1u : 0u;
            cascadeConstants.overlap = m_overlap ? 1u : 0u;
            cascadeConstants.bilinearFix = m_bilinearFix ? 1u : 0u;
            cascadeConstants.intervalT0 = static_cast<float>(m_t0);

            renderGraph.AddPass("RC-0 Cascade c" + std::to_string(k))
                .Read(canvas, EV::RGAccess::NonPixelShaderRead)
                .Read(upper, EV::RGAccess::NonPixelShaderRead)
                .Write(target)
                .Execute([this, canvas, upper, target, cascadeConstants](RGPassContext& context)
                    {
                        CommandList& commandList = context.GetCommandList();
                        m_cascadePSO->SetPipelineState(commandList);

                        commandList.SetCompute32BitConstants(CascadeRoot::Constants,
                            sizeof(CascadeConstants) / sizeof(uint32_t), &cascadeConstants);
                        commandList.SetShaderResourceView(CascadeRoot::CanvasSRV, 0, context.GetTexture(canvas),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        commandList.SetShaderResourceView(CascadeRoot::UpperSRV, 0, context.GetTexture(upper),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        commandList.SetUnorderedAccessView(CascadeRoot::CascadeUAV, 0, context.GetTexture(target), 0);
                        commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);
                    });
        }
        const RGTexture cascadeTexture = cascadeTextures[CascadeSlot(shown)];
        viewSource = cascadeTexture;

        if (fluenceView)
        {
            const RGTexture fluenceTexture = renderGraph.Import("RC-0 Fluence", m_fluence);

            renderGraph.AddPass("RC-0 Fluence")
                .Read(cascadeTexture, EV::RGAccess::NonPixelShaderRead)
                .Read(canvas, EV::RGAccess::NonPixelShaderRead)
                .Write(fluenceTexture)
                .Execute([this, cascadeTexture, canvas, fluenceTexture](RGPassContext& context)
                    {
                        CommandList& commandList = context.GetCommandList();
                        m_fluencePSO->SetPipelineState(commandList);

                        commandList.SetShaderResourceView(FluenceRoot::Cascade0SRV, 0,
                            context.GetTexture(cascadeTexture), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        commandList.SetShaderResourceView(FluenceRoot::CanvasSRV, 0, context.GetTexture(canvas),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        commandList.SetUnorderedAccessView(FluenceRoot::FluenceUAV, 0,
                            context.GetTexture(fluenceTexture), 0);
                        commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);
                    });

            viewSource = fluenceTexture;
        }
    }

    // viewSource -> m_viewTexture with alpha 1.
    const RGTexture viewTexture = renderGraph.Import("RC-0 View", m_viewTexture);
    ViewConstants viewConstants;
    viewConstants.mode = (m_view == View::Difference) ? 2u : (m_view == View::Cascade && m_showBeta) ? 1u : 0u;
    viewConstants.tonemap = (m_tonemap && (m_view == View::Reference || m_view == View::Fluence)) ? 1u : 0u;
    viewConstants.differenceScale = m_differenceScale;

    // The Reference view declares the reference twice (t0 and t1); the graph allows that.
    renderGraph.AddPass("RC-0 View")
        .Read(viewSource, EV::RGAccess::NonPixelShaderRead)
        .Read(referenceTexture, EV::RGAccess::NonPixelShaderRead)
        .Write(viewTexture)
        .Execute([this, viewSource, referenceTexture, viewTexture, viewConstants](RGPassContext& context)
            {
                CommandList& commandList = context.GetCommandList();
                m_viewPSO->SetPipelineState(commandList);

                commandList.SetCompute32BitConstants(ViewRoot::Constants,
                    sizeof(ViewConstants) / sizeof(uint32_t), &viewConstants);
                commandList.SetShaderResourceView(ViewRoot::SourceSRV, 0, context.GetTexture(viewSource),
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                commandList.SetShaderResourceView(ViewRoot::SecondSRV, 0, context.GetTexture(referenceTexture),
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                commandList.SetUnorderedAccessView(ViewRoot::ViewUAV, 0, context.GetTexture(viewTexture), 0);
                commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);
            });
}

// ── Panel ────────────────────────────────────────────────────────────────
// Controls column left of the canvas image, or on top when the panel is narrow.
void RcToy::DrawPanel(Editor& editor)
{
    constexpr float ControlsWidth = 300.0f;
    constexpr float SliderWidth = 140.0f;
    constexpr float MinImageWidth = 400.0f;
    const bool sideBySide = ImGui::GetContentRegionAvail().x >= ControlsWidth + MinImageWidth;

    // Own child window so the controls scroll while the image keeps its size.
    const ImVec2 controlsSize = sideBySide ? ImVec2(ControlsWidth, 0.0f) : ImVec2(0.0f, 0.0f);
    const ImGuiChildFlags controlsFlags = sideBySide ? ImGuiChildFlags_Borders
        : (ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    if (!sideBySide)
    {
        // On top: at most 40% of the panel's height.
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
            ImVec2(FLT_MAX, std::max(80.0f, ImGui::GetContentRegionAvail().y * 0.4f)));
    }
    ImGui::BeginChild("##RcControls", controlsSize, controlsFlags);

    ImGui::Checkbox("Enable", &m_enabled);

    if (!m_canvas)
    {
        ImGui::TextDisabled("No canvas yet (RcToy::Initialize).");
        ImGui::EndChild();
        return;
    }

    ImGui::SeparatorText("Brush");
    int mode = static_cast<int>(m_brushMode);
    ImGui::RadioButton("Light", &mode, static_cast<int>(BrushMode::Light));
    ImGui::SameLine();
    ImGui::RadioButton("Wall", &mode, static_cast<int>(BrushMode::Wall));
    ImGui::SameLine();
    ImGui::RadioButton("Eraser", &mode, static_cast<int>(BrushMode::Eraser));
    m_brushMode = static_cast<BrushMode>(mode);
    ImGui::SetNextItemWidth(SliderWidth);
    ImGui::SliderFloat("Radius", &m_brushRadius, 1.0f, 64.0f, "%.0f px");
    if (ImGui::Button("Clear canvas"))
    {
        m_clearRequested = true;
    }

    ImGui::SetNextItemWidth(SliderWidth);
    ImGui::SliderInt("Lights", &m_scatterCount, 1, 2000, "%d", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Clear + scatter lights"))
    {
        m_scatterRequested = true;
        m_clearRequested = true; // clears first, and restarts the reference
    }
    if (m_brushMode == BrushMode::Light)
    {
        ImGui::ColorEdit3("Colour", m_brushColor, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(SliderWidth - 40.0f);
        ImGui::DragFloat("Intensity", &m_brushIntensity, 0.1f, 0.0f, 100.0f, "%.1f");
    }

    ImGui::SeparatorText("View");
    int view = static_cast<int>(m_view);
    ImGui::RadioButton("Canvas", &view, static_cast<int>(View::Canvas));
    ImGui::SameLine();
    ImGui::RadioButton("Reference", &view, static_cast<int>(View::Reference));
    ImGui::SameLine();
    ImGui::RadioButton("Cascade", &view, static_cast<int>(View::Cascade));
    ImGui::RadioButton("Fluence", &view, static_cast<int>(View::Fluence));
    ImGui::SameLine();
    ImGui::RadioButton("Difference", &view, static_cast<int>(View::Difference));
    m_view = static_cast<View>(view);
    ImGui::Checkbox("1:1 pixels (scroll)", &m_pixelPerfect);

    ImGui::SeparatorText("Reference");
    ImGui::SetNextItemWidth(SliderWidth);
    ImGui::SliderInt("Rays / frame", &m_raysPerFrame, 1, 64);
    ImGui::Text("%u frames accumulated", m_referenceFrame);

    if (m_view == View::Cascade)
    {
        ImGui::SeparatorText("Cascade");
        // Label must differ from the "Cascade" radio button (same label = same ImGui ID).
        ImGui::SetNextItemWidth(SliderWidth);
        ImGui::SliderInt("Shown cascade", &m_shownCascade, 0, static_cast<int>(CascadeCount) - 1, "c%d");
        ImGui::Checkbox("Show beta", &m_showBeta);
        ImGui::SameLine();
        ImGui::Checkbox("Merge", &m_merge);
    }

    if (m_view == View::Cascade || m_view == View::Fluence || m_view == View::Difference)
    {
        ImGui::SeparatorText("Fixes");
        ImGui::Checkbox("Overlap", &m_overlap);
        ImGui::SameLine();
        ImGui::Checkbox("Bilinear fix", &m_bilinearFix);
        ImGui::TextUnformatted("t0");
        for (int value : { 1, 2, 4, 8 })
        {
            ImGui::SameLine();
            ImGui::RadioButton((std::to_string(value) + " px").c_str(), &m_t0, value);
        }
    }

    if (m_view == View::Reference || m_view == View::Fluence)
    {
        ImGui::SeparatorText("Display");
        ImGui::Checkbox("Tonemap (Reinhard)", &m_tonemap);
    }
    else if (m_view == View::Difference)
    {
        ImGui::SeparatorText("Difference");
        ImGui::SetNextItemWidth(SliderWidth);
        ImGui::SliderFloat("Difference x", &m_differenceScale, 1.0f, 64.0f, "%.0f");
        ImGui::TextWrapped("Red = RC too bright, blue = too dark. Let the reference converge first.");
    }

    // Last frame's mouse mapping (computed after the image, below).
    ImGui::SeparatorText("Mouse");
    ImGui::Text("Brush pixel: (%.0f, %.0f)", m_brushPixelX, m_brushPixelY);
    ImGui::Text("Painting: %s", m_painting ? "yes" : "no");

    ImGui::EndChild();

    if (sideBySide)
    {
        ImGui::SameLine();
    }
    ImGui::BeginChild("##RcImage", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
        ImGuiWindowFlags_HorizontalScrollbar);

    // Fit: largest square in the space left. 1:1: CanvasSize screen pixels (child window scrolls).
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float side = m_pixelPerfect ? static_cast<float>(CanvasSize)
        : std::max(1.0f, std::min(available.x, available.y));

    // The canvas's alpha is opacity, so empty space would be transparent: draw it over dark grey.
    const ImVec4 emptySpaceColor(0.15f, 0.15f, 0.15f, 1.0f);
    const std::shared_ptr<Texture>& shown = (m_view != View::Canvas && m_viewTexture) ? m_viewTexture : m_canvas;
    ImGui::ImageWithBg(editor.GetGUI()->GetTextureID(shown), ImVec2(side, side), ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f), emptySpaceColor);

    // Mouse -> canvas pixel (both y-down). No clamp: only used while hovered (min <= mouse < max).
	ImVec2 rectMin = ImGui::GetItemRectMin(); // screen space
	ImVec2 rectMax = ImGui::GetItemRectMax();
	ImVec2 mousePos = ImGui::GetMousePos();

	m_brushPixelX = (mousePos.x - rectMin.x) / (rectMax.x - rectMin.x) * CanvasSize;
	m_brushPixelY = (mousePos.y - rectMin.y) / (rectMax.y - rectMin.y) * CanvasSize;
	m_painting = ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    ImGui::EndChild();
}
