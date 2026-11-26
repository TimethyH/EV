// rc_paint.hlsl – RC-0 brush: paints lights, walls or empty space onto the canvas (one thread per canvas pixel).
// Canvas: rgb = emitted radiance (linear HDR), a = opacity (>= 0.5 = solid; lights are solid too).

// Must match RcToy::PaintConstants.
cbuffer PaintConstants : register(b0)
{
    float2 brushCenter;    // Canvas px.
    float  brushRadius;    // Canvas px.
    uint   brushMode;      // RcToy::BrushMode: 0 = light, 1 = wall, 2 = eraser.
    float3 brushColor;
    float  brushIntensity;
};

RWTexture2D<float4> canvas : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    float2 fPixel = pixel + 0.5f;
    float distanceToBrush = length(fPixel - brushCenter);

    if (distanceToBrush > brushRadius)
        return;

    float alpha = 1.0;
    float3 color = brushColor * brushIntensity;
    switch (brushMode)
    {
        case 0: // light
            break;
        case 1: // wall
            color = float3(0.0, 0.0, 0.0);
            alpha = 1.0;
            break;
        case 2: // eraser
            color = float3(0.0, 0.0, 0.0);
            alpha = 0.0;
            break;
    }
    canvas[pixel] = float4(color, alpha);
}
