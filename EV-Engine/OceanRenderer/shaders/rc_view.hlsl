// rc_view.hlsl – RC-0 panel viewer (tooling, not RC logic): copies a texture into one ImGui can show.
//
// Why it exists: ImGui alpha-blends images. A cascade texel's alpha is the transmittance beta, and beta = 0 means
// "this interval hit something" – exactly the texels that carry a light's colour – so ImGui would draw them fully
// transparent. This pass always writes alpha 1. Modes:
//   0 = rgb (a cascade's radiance, the reference, or the fluence), optionally tonemapped,
//   1 = alpha as grey (a cascade's beta: white = nothing hit in the interval, black = hit),
//   2 = difference (step 5): source (the RC fluence) minus second (the reference), by brightness, times a scale:
//       red = RC too bright, blue = RC too dark, black = they agree.

// Sent by the CPU as 3 root constants (RcToy::ViewConstants must have the same layout).
cbuffer ViewConstants : register(b0)
{
    uint mode; // 0 = rgb, 1 = alpha as grey, 2 = difference.
    uint tonemap; // 1 = Reinhard on the rgb of mode 0 (HDR lights > 1 stop clipping to white).
    float differenceScale; // Mode 2: the difference is multiplied by this, so small errors become visible.
};

Texture2D<float4> source : register(t0); // What to show (a cascade, the reference or the fluence), read only.
RWTexture2D<float4> view : register(u0); // What the panel draws with ImGui::Image.
Texture2D<float4> second : register(t1); // Mode 2 only: the reference (bound in every mode, read only in mode 2).

// Brightness of a linear colour as the eye weighs it (Rec. 709 / sRGB primaries).
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
        // Signed brightness difference: positive = RC brighter than the reference.
        float difference = (Luminance(value.rgb) - Luminance(second[pixel].rgb)) * differenceScale;
        color = float3(max(difference, 0.0), 0.0, max(-difference, 0.0));
    }
    else
    {
        // Reinhard x / (1 + x): 0 stays 0, 1 becomes 0.5, a bright light approaches 1 instead of clipping. Applied the
        // same way to the reference and the fluence, so the two views stay comparable.
        color = (tonemap != 0u) ? value.rgb / (1.0 + value.rgb) : value.rgb;
    }

    view[pixel] = float4(color, 1.0); // Alpha 1: never transparent in the panel.
}
