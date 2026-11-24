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

    // Which texture the panel shows.
    enum class View : uint32_t
    {
        Canvas = 0,    // What you painted.
        Reference = 1, // The brute-force fluence (step 2).
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
};
