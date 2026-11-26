// rc_view.hlsl – RC-0 panel viewer: copies a texture into one ImGui can show, always with alpha 1
// (ImGui alpha-blends, and a cascade's alpha = beta would make hit texels invisible).

// Must match RcToy::ViewConstants.
cbuffer ViewConstants : register(b0)
{
    uint mode; // 0 = rgb, 1 = alpha as grey, 2 = difference (red = RC too bright, blue = too dark).
    uint tonemap; // Reinhard on mode 0.
    float differenceScale;
};

Texture2D<float4> source : register(t0);
RWTexture2D<float4> view : register(u0);
Texture2D<float4> second : register(t1); // Mode 2: the reference.

// Rec. 709 luminance.
float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    float4 value = source[pixel];
    float3 color;

    if (mode == 1)
    {
        color = value.aaa;
    }
    else if (mode == 2)
    {
        float difference = (Luminance(value.rgb) - Luminance(second[pixel].rgb)) * differenceScale;
        color = float3(max(difference, 0.0), 0.0, max(-difference, 0.0));
    }
    else
    {
        color = (tonemap != 0u) ? value.rgb / (1.0 + value.rgb) : value.rgb;
    }

    view[pixel] = float4(color, 1.0);
}
