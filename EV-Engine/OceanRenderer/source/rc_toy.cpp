/**
 *  @file rc_toy.cpp
 *
 *  @brief RC-0, the 2D radiance cascades toy. See rc_toy.h and docs/path-tracing/knowledge.md "### RC-0".
 */

#include "rc_toy.h"

#include <algorithm>
#include <string>
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

// Root parameter slots of the reference PSO (step 2).
namespace ReferenceRoot
{
    enum : uint32_t
    {
        CanvasSRV = 0,    // descriptor table, t0 (the canvas, read only)
        ReferenceUAV = 1, // descriptor table, u0 (the running average)
        Constants = 2,    // 2 root constants, b0 = RcToy::ReferenceConstants
    };
}

// Root parameter slots of the cascade PSO (step 3, + the upper cascade in step 4).
namespace CascadeRoot
{
    enum : uint32_t
    {
        CanvasSRV = 0,  // descriptor table, t0 (the canvas, read only)
        CascadeUAV = 1, // descriptor table, u0 (the cascade c_k being written)
        Constants = 2,  // 2 root constants, b0 = RcToy::CascadeConstants
        UpperSRV = 3,   // descriptor table, t1 (c_(k+1), already merged: the other ping-pong texture)
    };
}

// Root parameter slots of the viewer PSO (step 3, panel display only; t1 added in step 5).
namespace ViewRoot
{
    enum : uint32_t
    {
        SourceSRV = 0, // descriptor table, t0 (the texture to show)
        ViewUAV = 1,   // descriptor table, u0 (what ImGui draws)
        Constants = 2, // 3 root constants, b0 = RcToy::ViewConstants
        SecondSRV = 3, // descriptor table, t1 (the reference, read by the Difference view)
    };
}

// Root parameter slots of the fluence PSO (step 5).
namespace FluenceRoot
{
    enum : uint32_t
    {
        Cascade0SRV = 0, // descriptor table, t0 (c0, merged with c1 ... c5)
        FluenceUAV = 1,  // descriptor table, u0 (the fluence per canvas pixel)
        CanvasSRV = 2,   // descriptor table, t1 (the canvas: solid pixels show their own emission)
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

    // Step 2: the reference's running average. 32-bit float per channel: at frame n each new frame only moves the
    // average by 1/n, and a 16-bit float (10-bit mantissa) would stop changing after roughly a thousand frames.
    auto referenceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, CanvasSize, CanvasSize);
    referenceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    m_reference = Application::Get().CreateTexture(referenceDesc);
    m_reference->SetName(L"RC-0 Reference");

    // The reference shader reads the canvas (SRV, t0), writes the average (UAV, u0) and gets 2 root constants (b0).
    // Separate tables for the SRV and the UAV, like the ocean's H0 pass (ReadTextures / WriteTextures).
    CD3DX12_DESCRIPTOR_RANGE1 referenceCanvasRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // 1 SRV at t0
    CD3DX12_DESCRIPTOR_RANGE1 referenceOutputRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 UAV at u0
    CD3DX12_ROOT_PARAMETER1 referenceRootParameters[3];
    referenceRootParameters[ReferenceRoot::CanvasSRV].InitAsDescriptorTable(1, &referenceCanvasRange);
    referenceRootParameters[ReferenceRoot::ReferenceUAV].InitAsDescriptorTable(1, &referenceOutputRange);
    referenceRootParameters[ReferenceRoot::Constants].InitAsConstants(sizeof(ReferenceConstants) / sizeof(uint32_t), 0);
    m_referencePSO = std::make_shared<OceanCompute>(L"/rc_reference.cso", referenceRootParameters,
        static_cast<uint32_t>(_countof(referenceRootParameters)));

    // Step 3: two cascade textures (ping-pong in step 4) and the viewer's output, all 1024^2 fp16 with UAV access.
    // fp16 is enough here: each cascade is rewritten from scratch every frame (no running average like the reference).
    auto cascadeDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, CanvasSize, CanvasSize);
    cascadeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    m_cascades[0] = Application::Get().CreateTexture(cascadeDesc);
    m_cascades[0]->SetName(L"RC-0 Cascade A");
    m_cascades[1] = Application::Get().CreateTexture(cascadeDesc);
    m_cascades[1]->SetName(L"RC-0 Cascade B");
    m_viewTexture = Application::Get().CreateTexture(cascadeDesc);
    m_viewTexture->SetName(L"RC-0 View");

    // The cascade shader has one more input than the shared shape: c_(k+1) at t1 (step 4), in its own table so the
    // canvas table stays as it was.
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

    // The viewer: the texture to show (t0), its output (u0), 3 root constants (b0), and a second texture (t1) that only
    // the Difference view reads (step 5: the reference).
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

    // Step 5: the fluence, one value per canvas pixel. fp16 like the cascades (rewritten from scratch every frame).
    m_fluence = Application::Get().CreateTexture(cascadeDesc);
    m_fluence->SetName(L"RC-0 Fluence");

    // rc_fluence.hlsl reads the merged c0 (t0) and the canvas (t1), and writes the fluence (u0). No constants.
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
    // The canvas changed (painting) or the ray count moved: the old average belongs to another scene, start over.
    if (m_painting || m_raysPerFrame != m_lastRaysPerFrame)
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

    // Step 2: the brute-force reference, every frame, AFTER the paint pass (the graph runs passes in the order they
    // are added, so it sees this frame's paint). The constants are taken NOW, while recording: frameIndex is this
    // frame's value, and m_referenceFrame has already moved on by the time the pass's lambda runs.
    // So the lambda gets them BY VALUE and never changes RcToy's state itself.
    const RGTexture referenceTexture = renderGraph.Import("RC-0 Reference", m_reference);
    const ReferenceConstants referenceConstants = NextReferenceConstants();

    // Reads the canvas as an SRV in a compute shader (NonPixelShaderRead: the graph's barrier and the SRV binding
    // below ask for the same state, so no second transition) and writes the running average as a UAV.
    renderGraph.AddPass("RC-0 Reference")
        .Read(canvas, EV::RGAccess::NonPixelShaderRead)
        .Write(referenceTexture)
        .Execute([this, canvas, referenceTexture, referenceConstants](RGPassContext& context)
            {
                CommandList& commandList = context.GetCommandList();
                m_referencePSO->SetPipelineState(commandList);

                // Handles -> the real textures (only valid inside the lambda).
                std::shared_ptr<Texture> canvasTexture = context.GetTexture(canvas);
                std::shared_ptr<Texture> refTexture = context.GetTexture(referenceTexture);

                commandList.SetCompute32BitConstants(ReferenceRoot::Constants, sizeof(ReferenceConstants) / sizeof(float), &referenceConstants);
				commandList.SetShaderResourceView(ReferenceRoot::CanvasSRV, 0, canvasTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				commandList.SetUnorderedAccessView(ReferenceRoot::ReferenceUAV, 0, refTexture, 0);
				commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1); // 64 x 64 groups of 16 x 16
            });

    // Everything below only feeds the panel's views; the Canvas view shows m_canvas directly.
    if (m_view == View::Canvas)
    {
        return;
    }

    // What the viewer pass shows (t0). The Reference view shows the reference itself; the others replace it below.
    RGTexture viewSource = referenceTexture;

    // Steps 3 + 4: the cascades, for the Cascade view and (step 5) for the fluence. The Fluence and Difference views
    // always need the WHOLE merge down to c0 (all 6 cascades, Merge on), whatever the Cascade-view settings are.
    const bool fluenceView = (m_view == View::Fluence || m_view == View::Difference);
    if (m_view == View::Cascade || fluenceView)
    {
        const bool merge = fluenceView ? true : m_merge;

        // Step 4, top-down: c5 first (raw, nothing above it), then c4 ... c_shown, each merging with the one just
        // written. Each cascade is its own pass, so the graph puts a barrier between them (c_(k+1) written as UAV ->
        // read as SRV). Merge off = step 3's behaviour: only c_shown, raw.
        const RGTexture cascadeTextures[2] = {
            renderGraph.Import("RC-0 Cascade A", m_cascades[0]),
            renderGraph.Import("RC-0 Cascade B", m_cascades[1]),
        };
        const uint32_t shown = fluenceView ? 0u : static_cast<uint32_t>(m_shownCascade);
        const uint32_t top = merge ? CascadeCount - 1 : shown;

        for (int k = static_cast<int>(top); k >= static_cast<int>(shown); --k)
        {
            const RGTexture target = cascadeTextures[CascadeSlot(k)];    // c_k is written here...
            const RGTexture upper = cascadeTextures[CascadeSlot(k) ^ 1u]; // ...and c_(k+1) was written to the other.

            CascadeConstants cascadeConstants;
            cascadeConstants.cascadeIndex = static_cast<uint32_t>(k);
            // c5 has nothing above it. `upper` is still bound for c5 (a root table must be set before a dispatch),
            // the shader just doesn't read it.
            cascadeConstants.mergeWithUpper = (merge && k < static_cast<int>(CascadeCount) - 1) ? 1u : 0u;

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
                        // One thread per cascade TEXEL (= one ray: one probe, one direction). Every cascade is 1024^2.
                        commandList.Dispatch(CanvasSize / 16, CanvasSize / 16, 1);
                    });
        }
        const RGTexture cascadeTexture = cascadeTextures[CascadeSlot(shown)]; // c_shown (c0 for the fluence views).
        viewSource = cascadeTexture;

        // Step 5: merged c0 -> one light value per canvas pixel (rc_fluence.hlsl). One thread per canvas pixel.
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

    // Viewer: viewSource -> m_viewTexture with alpha 1, for the panel. Modes (rc_view.hlsl): 0 = rgb (tonemapped for
    // Reference + Fluence when the box is ticked, so both look the same), 1 = a cascade's beta as grey,
    // 2 = Difference (viewSource = fluence, t1 = reference).
    const RGTexture viewTexture = renderGraph.Import("RC-0 View", m_viewTexture);
    ViewConstants viewConstants;
    viewConstants.mode = (m_view == View::Difference) ? 2u : (m_view == View::Cascade && m_showBeta) ? 1u : 0u;
    viewConstants.tonemap = (m_tonemap && (m_view == View::Reference || m_view == View::Fluence)) ? 1u : 0u;
    viewConstants.differenceScale = m_differenceScale;

    // The reference is read twice in the Reference view (t0 and t1): fine, the graph allows declaring a read twice.
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

    // Which texture to show (step 2), and the reference's settings.
    int view = static_cast<int>(m_view);
    ImGui::RadioButton("Canvas", &view, static_cast<int>(View::Canvas));
    ImGui::SameLine();
    ImGui::RadioButton("Reference", &view, static_cast<int>(View::Reference));
    ImGui::SameLine();
    ImGui::RadioButton("Cascade", &view, static_cast<int>(View::Cascade));
    ImGui::SameLine();
    ImGui::RadioButton("Fluence", &view, static_cast<int>(View::Fluence));
    ImGui::SameLine();
    ImGui::RadioButton("Difference", &view, static_cast<int>(View::Difference));
    m_view = static_cast<View>(view);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Rays / frame", &m_raysPerFrame, 1, 64);
    ImGui::SameLine();
    ImGui::Text("Reference: %u frames", m_referenceFrame);

    // Step 3: which raw cascade to compute and show, and radiance vs beta.
    if (m_view == View::Cascade)
    {
        // ImGui hashes the label into the widget ID: "Cascade" is already the radio button above, so this label must
        // differ (same label in one window = conflicting IDs, hovering one highlights both).
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderInt("Shown cascade", &m_shownCascade, 0, static_cast<int>(CascadeCount) - 1, "c%d");
        ImGui::SameLine();
        ImGui::Checkbox("Show beta", &m_showBeta);
        ImGui::SameLine();
        // Step 4: off = the raw interval only (step 3), on = c5 ... c_shown merged top-down.
        ImGui::Checkbox("Merge", &m_merge);
    }

    // Step 5: Reference and Fluence share the tonemap setting (compare like with like); Difference has its own scale.
    if (m_view == View::Reference || m_view == View::Fluence)
    {
        ImGui::Checkbox("Tonemap (Reinhard)", &m_tonemap);
    }
    else if (m_view == View::Difference)
    {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Difference x", &m_differenceScale, 1.0f, 64.0f, "%.0f");
        ImGui::SameLine();
        ImGui::TextDisabled("red = RC too bright, blue = too dark (let the reference converge first)");
    }

    // Largest square that fits in the space left in the panel.
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float side = std::max(1.0f, std::min(available.x, available.y));

    // ImGui alpha-blends images, and the canvas's alpha is OPACITY (0 = empty space), so a plain Image() of an empty
    // canvas is fully transparent. Drawing it over a dark grey background turns that into a readable view:
    // empty = grey (background shows through), wall = black (rgb 0, alpha 1), light = its colour (rgb > 0, alpha 1).
    const ImVec4 emptySpaceColor(0.15f, 0.15f, 0.15f, 1.0f);
    // Every view except the canvas is drawn by the viewer pass into m_viewTexture (alpha 1, tonemap, difference).
    const std::shared_ptr<Texture>& shown = (m_view != View::Canvas && m_viewTexture) ? m_viewTexture : m_canvas;
    ImGui::ImageWithBg(editor.GetGUI()->GetTextureID(shown), ImVec2(side, side), ImVec2(0.0f, 0.0f),
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
