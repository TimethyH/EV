#pragma once

/**
 *  @file rc_toy.h
 *
 *  @brief RC-0: a 2D ("flatland") radiance cascades toy on a paintable canvas (path tracing GI track).
 *
 *  You paint lights and walls on a 1024x1024 canvas; radiance cascades compute how much light reaches every pixel,
 *  next to a brute-force reference. It exists to learn RC (intervals, cascades, merging, ringing) before any 3D GI.
 *  The plan, conventions and steps are in docs/path-tracing/knowledge.md, "### RC-0".
 *
 *  Ocean owns one RcToy:
 *      Initialize()   once in Ocean::LoadContent (creates the GPU resources)
 *      AddPasses(rg)  every frame in Ocean::RecordRenderGraph (does nothing while the panel's Enable box is off)
 *      DrawPanel(...) the contents of the "RC-0" editor panel (a tab next to the Scene view)
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
    // Width and height of the canvas in pixels (and of every cascade texture later on).
    static constexpr uint32_t CanvasSize = 1024;

    // Create the GPU resources that live as long as the app: for now the canvas.
    void Initialize();

    // Add RC-0's passes to this frame's render graph. Later steps add the paint, reference, cascade and fluence passes.
    void AddPasses(EV::RenderGraph& renderGraph);

    // Contents of the "RC-0" panel; the Editor already wraps it in ImGui::Begin/End (Editor::AddPanel).
    void DrawPanel(EV::Editor& editor);

    // Drop the GPU resources (Ocean::UnloadContent).
    void Release();

    // What the brush paints (the numbers are what rc_paint.hlsl receives in brushMode).
    enum class BrushMode : uint32_t
    {
        Light = 0,  // rgb = colour * intensity, opacity 1
        Wall = 1,   // rgb = 0, opacity 1
        Eraser = 2, // back to empty space (0, 0, 0, 0)
    };

    // The brush as the paint shader sees it: 8 x 32-bit root constants. Must match cbuffer PaintConstants in
    // rc_paint.hlsl field by field (HLSL packs it into two 16-byte registers: center+radius+mode, color+intensity).
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

    // What rc_reference.hlsl receives each frame (cbuffer ReferenceConstants, b0): 2 root constants.
    struct ReferenceConstants
    {
        uint32_t frameIndex = 0;   // Frames already in the running average; 0 = start over.
        uint32_t raysPerFrame = 4; // Rays per pixel this frame.
    };
    static_assert(sizeof(ReferenceConstants) == 2 * sizeof(uint32_t), "ReferenceConstants must be 2 root constants");

    // Step 3: how many cascades RC-0 uses (c0..c5; c5's interval ends at 2730 px, past the 1448-px diagonal).
    static constexpr uint32_t CascadeCount = 6;

    // What rc_cascade.hlsl receives (cbuffer CascadeConstants, b0): 5 root constants.
    struct CascadeConstants
    {
        uint32_t cascadeIndex = 0;   // k: which cascade this dispatch fills (0 = finest, CascadeCount-1 = coarsest).
        uint32_t mergeWithUpper = 0; // Step 4: 1 = merge with c_(k+1) (bound at t1), 0 = raw (c5, or Merge off).
        uint32_t overlap = 0;        // Step 6.1: 1 = the near march goes one upper probe spacing past tEnd.
        uint32_t bilinearFix = 0;    // Step 6.2: 1 = merge with MergeWithUpperFixed (one near segment per upper probe).
        float    intervalT0 = 2.0f;  // Step 6.3: c0's interval length in px; every interval scales with it.
    };
    static_assert(sizeof(CascadeConstants) == 5 * sizeof(uint32_t), "CascadeConstants must be 5 root constants");

    // What rc_view.hlsl receives (cbuffer ViewConstants, b0): 3 root constants.
    struct ViewConstants
    {
        uint32_t mode = 0;             // 0 = rgb, 1 = alpha as grey (a cascade's beta), 2 = difference fluence - reference.
        uint32_t tonemap = 0;          // Step 5: 1 = Reinhard x / (1 + x) on mode 0's rgb.
        float    differenceScale = 1.0f; // Step 5, mode 2: the difference is multiplied by this.
    };
    static_assert(sizeof(ViewConstants) == 3 * sizeof(uint32_t), "ViewConstants must be 3 root constants");

    // Which texture the panel shows. Everything except the canvas goes through the viewer pass (rc_view.hlsl).
    enum class View : uint32_t
    {
        Canvas = 0,     // What you painted.
        Reference = 1,  // The brute-force fluence (step 2).
        Cascade = 2,    // One cascade, c0..c5, raw (step 3) or merged (step 4).
        Fluence = 3,    // Step 5: the RC result per pixel (merged c0 -> rc_fluence.hlsl).
        Difference = 4, // Step 5: fluence minus reference (red = RC too bright, blue = too dark).
    };

private:
    // The reference's constants for THIS frame, and advance the frame counter: restarts the average (frameIndex 0)
    // when the canvas changed or the rays-per-frame slider moved. Call once per frame, while recording.
    ReferenceConstants NextReferenceConstants();

    // The panel's "Enable" box: RC-0's passes only run while it is ticked.
    bool m_enabled = false;

    // Filled by DrawPanel every frame (step 1b), used by the paint pass (step 1c):
    // the canvas pixel under the mouse (0..CanvasSize-1 on both axes) and whether the left button paints there now.
    float m_brushPixelX = 0.0f;
    float m_brushPixelY = 0.0f;
    bool  m_painting = false;

    // Panel button "Clear canvas": the next frame's paint pass erases the whole canvas (one big eraser stroke).
    bool  m_clearRequested = false;

    // Step 7 test tool "Clear + scatter lights": the next frame clears the canvas and paints m_scatterCount lights at
    // random spots (fixed seed: the same layout every time, so measurements can be repeated).
    bool  m_scatterRequested = false;
    int   m_scatterCount = 100;

    // Brush settings from the panel.
    BrushMode m_brushMode = BrushMode::Light;
    float     m_brushColor[3] = { 1.0f, 0.85f, 0.6f }; // Warm white.
    float     m_brushIntensity = 5.0f;                 // HDR: lights may be brighter than 1.
    float     m_brushRadius = 8.0f;                    // Canvas pixels.

    // What you paint: R16G16B16A16_FLOAT, rgb = emitted radiance (linear HDR), a = opacity (>= 0.5 = solid).
    // Persistent (lives across frames), so what you painted stays.
    std::shared_ptr<EV::Texture> m_canvas;

    // rc_paint.hlsl + its root signature: [0] descriptor table with the canvas UAV (u0), [1] 8 root constants (b0).
    std::shared_ptr<OceanCompute> m_paintPSO;

    // Step 2: the brute-force reference. R32G32B32A32_FLOAT running average of the fluence, persistent like the canvas.
    std::shared_ptr<EV::Texture> m_reference;

    // rc_reference.hlsl + its root signature: [0] canvas SRV (t0), [1] reference UAV (u0), [2] 2 root constants (b0).
    std::shared_ptr<OceanCompute> m_referencePSO;

    View     m_view = View::Canvas;
    uint32_t m_referenceFrame = 0;      // Frames accumulated so far (shown in the panel).
    int      m_raysPerFrame = 4;        // Panel slider.
    int      m_lastRaysPerFrame = 4;    // To notice the slider moving (restart the average).

    // Step 3: the cascade textures, R16G16B16A16_FLOAT 1024^2, texel = (rgb radiance of the interval, a = beta).
    // Two of them because step 4 ping-pongs (c5 -> [0], c4 -> [1], c3 -> [0], ...: a dispatch reads c_(k+1) from one
    // and writes c_k into the other; a texture can't be read and written in the same dispatch). Not persistent in
    // meaning (rewritten every frame), but created up front like the canvas until the render graph can own textures
    // (docs/tasks/).
    std::shared_ptr<EV::Texture> m_cascades[2];

    // Which of m_cascades holds c_k: c5 goes into [0], then they alternate (c4 [1], c3 [0], ... c0 [1]).
    static uint32_t CascadeSlot(uint32_t k) { return (CascadeCount - 1 - k) & 1u; }

    // rc_cascade.hlsl + its root signature: [0] canvas SRV (t0), [1] cascade UAV (u0), [2] 2 root constants (b0),
    // [3] upper cascade SRV (t1, step 4).
    std::shared_ptr<OceanCompute> m_cascadePSO;

    // Viewer: ImGui alpha-blends images, and a cascade's alpha is beta (0 = "hit" = fully transparent in the panel), so
    // a raw cascade can't be shown with ImGui::Image directly. rc_view.hlsl copies it into m_viewTexture with
    // alpha 1 (or shows beta as grey, or the difference view). [0] source SRV (t0), [1] view UAV (u0),
    // [2] 3 root constants (b0), [3] second SRV (t1: the reference, for the difference view).
    std::shared_ptr<EV::Texture>  m_viewTexture;
    std::shared_ptr<OceanCompute> m_viewPSO;

    int  m_shownCascade = 0;    // Panel slider: which cascade is shown.
    bool m_showBeta = false;    // Panel checkbox: show the cascade's beta instead of its radiance.
    bool m_merge = true;        // Panel checkbox (step 4): merged c5 ... c_shown top-down, or only c_shown raw (step 3).

    // Step 6 panel controls (each an A/B against the Reference), sent in CascadeConstants.
    bool m_overlap = false;     // 6.1: interval overlap.
    bool m_bilinearFix = false; // 6.2: the bilinear fix.
    int  m_t0 = 2;              // 6.3: c0's interval length in px (1, 2, 4 or 8).

    // Step 5: the RC result per canvas pixel, R16G16B16A16_FLOAT 1024^2 (rgb = fluence, a = 1). Rewritten every frame.
    std::shared_ptr<EV::Texture> m_fluence;

    // rc_fluence.hlsl + its root signature: [0] merged c0 SRV (t0), [1] fluence UAV (u0), [2] canvas SRV (t1).
    std::shared_ptr<OceanCompute> m_fluencePSO;

    bool  m_tonemap = true;          // Panel checkbox (step 5): Reinhard on the Reference and Fluence views.
    float m_differenceScale = 4.0f;  // Panel slider (step 5): how strongly the Difference view amplifies errors.

    // Panel checkbox: show the canvas at 1 screen pixel per texel (1024 x 1024, scroll if the panel is smaller)
    // instead of scaled to fit. Scaling a 1024^2 image down skips texels, which shows up as moire lines.
    bool m_pixelPerfect = false;
};
