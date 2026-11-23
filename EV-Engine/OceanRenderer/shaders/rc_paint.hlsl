// rc_paint.hlsl – RC-0 step 1c: the brush that paints lights, walls or empty space onto the canvas.
//
// One thread per canvas pixel (the whole 1024x1024 canvas is dispatched while the left mouse button is down; cheap
// enough for a toy, and simpler than dispatching only the brush's bounding box). Pixels inside the circular brush
// are overwritten, all others are left untouched.
//
// Canvas convention (docs/path-tracing/knowledge.md "### RC-0", Conventions):
//   rgb = emitted radiance (linear HDR, may be > 1), a = opacity (>= 0.5 = solid, 0 = empty space).
//   A light is solid too: it emits from its surface and blocks rays like a wall.

// The brush, sent by the CPU as 8 root constants (RcToy::PaintConstants must have the same layout).
// HLSL packs a cbuffer into 16-byte registers: (center, radius, mode) fill register 0, (color, intensity) register 1.
cbuffer PaintConstants : register(b0)
{
    float2 brushCenter;    // Canvas pixels (float, from RcToy::DrawPanel).
    float  brushRadius;    // Canvas pixels.
    uint   brushMode;      // 0 = light, 1 = wall, 2 = eraser (RcToy::BrushMode).
    float3 brushColor;     // Light colour, 0..1 per channel.
    float  brushIntensity; // Light brightness: emitted radiance = brushColor * brushIntensity.
};

// The canvas, read and written as a UAV.
RWTexture2D<float4> canvas : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // SV_DispatchThreadID is the global thread index, which with one thread per pixel IS the canvas pixel.
    uint2 pixel = dispatchThreadID.xy;

    // Compare the pixel's CENTER (pixel (10,4) covers 10..11 x 4..5) with the brush center, both in canvas pixels.
    float2 fPixel = pixel + 0.5f;
    float distanceToBrush = length(fPixel - brushCenter);

    // Outside the brush: write nothing, so the rest of the canvas keeps what was painted before.
    if (distanceToBrush > brushRadius)
        return;

    // Pick the whole value per mode (they differ in rgb AND opacity). brushMode is the same for every thread,
    // so this switch never diverges within a wave.
    float alpha = 1.0;
    float3 color = brushColor * brushIntensity;
    switch (brushMode)
    {
        case 0: // light: emits colour * intensity, solid
            break;
        case 1: // wall: emits nothing, solid
            color = float3(0.0, 0.0, 0.0);
            alpha = 1.0;
            break;
        case 2: // eraser: back to empty space
            color = float3(0.0, 0.0, 0.0);
            alpha = 0.0;
            break;
    }
    canvas[pixel] = float4(color, alpha);
}
