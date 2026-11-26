#pragma once

/**
 *  @file rc_toy.h
 *
 *  @brief RC-0: a 2D radiance cascades toy on a paintable 1024x1024 canvas, next to a brute-force reference.
 *  Plan and conventions: knowledge.md "### RC-0" + the RC-0 Patroni boards.
 *
 *  Ocean owns one RcToy: Initialize() in LoadContent, AddPasses(rg) every frame, DrawPanel() for the "RC-0" panel.
 */

#include <cstdint>
#include <memory>

namespace EV
{
    class Editor;
    class RenderGraph;
    class Texture;
}
class OceanCompute;

class RcToy
{
public:
    static constexpr uint32_t CanvasSize = 1024;

    void Initialize();

    // Does nothing while the panel's Enable box is off.
    void AddPasses(EV::RenderGraph& renderGraph);

    // Panel contents; Editor::AddPanel already wraps it in ImGui::Begin/End.
    void DrawPanel(EV::Editor& editor);

    void Release();

    // Values = rc_paint.hlsl's brushMode.
    enum class BrushMode : uint32_t
    {
        Light = 0,
        Wall = 1,
        Eraser = 2,
    };

    // Must match cbuffer PaintConstants in rc_paint.hlsl.
    struct PaintConstants
    {
        float    centerX = 0.0f;
        float    centerY = 0.0f;
        float    radius = 0.0f;
        uint32_t mode = 0;
        float    color[3] = { 1.0f, 1.0f, 1.0f };
        float    intensity = 1.0f;
    };
    static_assert(sizeof(PaintConstants) == 8 * sizeof(uint32_t), "PaintConstants must be 8 root constants");

    // Must match cbuffer ReferenceConstants in rc_reference.hlsl.
    struct ReferenceConstants
    {
        uint32_t frameIndex = 0;   // 0 = start over.
        uint32_t raysPerFrame = 4;
    };
    static_assert(sizeof(ReferenceConstants) == 2 * sizeof(uint32_t), "ReferenceConstants must be 2 root constants");

    // c0..c5; c5's interval ends at 2730 px, past the 1448-px diagonal.
    static constexpr uint32_t CascadeCount = 6;

    // Must match cbuffer CascadeConstants in rc_cascade.hlsl.
    struct CascadeConstants
    {
        uint32_t cascadeIndex = 0;
        uint32_t mergeWithUpper = 0;
        uint32_t overlap = 0;
        uint32_t bilinearFix = 0;
        float    intervalT0 = 2.0f;
    };
    static_assert(sizeof(CascadeConstants) == 5 * sizeof(uint32_t), "CascadeConstants must be 5 root constants");

    // Must match cbuffer ViewConstants in rc_view.hlsl.
    struct ViewConstants
    {
        uint32_t mode = 0;             // 0 = rgb, 1 = alpha as grey, 2 = difference.
        uint32_t tonemap = 0;
        float    differenceScale = 1.0f;
    };
    static_assert(sizeof(ViewConstants) == 3 * sizeof(uint32_t), "ViewConstants must be 3 root constants");

    // Which texture the panel shows.
    enum class View : uint32_t
    {
        Canvas = 0,
        Reference = 1,
        Cascade = 2,
        Fluence = 3,
        Difference = 4,
    };

private:
    // Returns this frame's reference constants and advances the counter (restarts when the canvas or the slider changed).
    ReferenceConstants NextReferenceConstants();

    bool m_enabled = false;

    // Canvas pixel under the mouse, set by DrawPanel, used by the paint pass.
    float m_brushPixelX = 0.0f;
    float m_brushPixelY = 0.0f;
    bool  m_painting = false;

    bool  m_clearRequested = false;

    // "Clear + scatter lights" test tool (fixed seed, so measurements repeat).
    bool  m_scatterRequested = false;
    int   m_scatterCount = 100;

    BrushMode m_brushMode = BrushMode::Light;
    float     m_brushColor[3] = { 1.0f, 0.85f, 0.6f };
    float     m_brushIntensity = 5.0f;
    float     m_brushRadius = 8.0f;                    // Canvas px.

    // R16G16B16A16_FLOAT, rgb = emission, a = opacity (>= 0.5 = solid). Persistent.
    std::shared_ptr<EV::Texture> m_canvas;

    // Root: [0] canvas UAV table (u0), [1] 8 root constants (b0).
    std::shared_ptr<OceanCompute> m_paintPSO;

    // R32G32B32A32_FLOAT running average, persistent.
    std::shared_ptr<EV::Texture> m_reference;

    // Root: [0] canvas SRV (t0), [1] reference UAV (u0), [2] 2 root constants (b0).
    std::shared_ptr<OceanCompute> m_referencePSO;

    View     m_view = View::Canvas;
    uint32_t m_referenceFrame = 0;
    int      m_raysPerFrame = 4;
    int      m_lastRaysPerFrame = 4;    // To notice the slider moving.

    // Ping-pong pair (a dispatch reads c_(k+1) from one, writes c_k into the other). Created up front until the render
    // graph can own persistent textures (docs/tasks/).
    std::shared_ptr<EV::Texture> m_cascades[2];

    // c5 -> [0], c4 -> [1], c3 -> [0], ...
    static uint32_t CascadeSlot(uint32_t k) { return (CascadeCount - 1 - k) & 1u; }

    // Root: [0] canvas SRV (t0), [1] cascade UAV (u0), [2] 5 root constants (b0), [3] upper cascade SRV (t1).
    std::shared_ptr<OceanCompute> m_cascadePSO;

    // rc_view.hlsl writes alpha 1 (ImGui would draw beta = 0 texels transparent).
    // Root: [0] source SRV (t0), [1] view UAV (u0), [2] 3 root constants (b0), [3] reference SRV (t1).
    std::shared_ptr<EV::Texture>  m_viewTexture;
    std::shared_ptr<OceanCompute> m_viewPSO;

    int  m_shownCascade = 0;
    bool m_showBeta = false;
    bool m_merge = true;

    bool m_overlap = false;
    bool m_bilinearFix = false;
    int  m_t0 = 2;              // 1, 2, 4 or 8 px.

    std::shared_ptr<EV::Texture> m_fluence;

    // Root: [0] merged c0 SRV (t0), [1] fluence UAV (u0), [2] canvas SRV (t1).
    std::shared_ptr<OceanCompute> m_fluencePSO;

    bool  m_tonemap = true;
    float m_differenceScale = 4.0f;

    // 1 screen pixel per texel (scaling down skips texels -> moire).
    bool m_pixelPerfect = false;
};
