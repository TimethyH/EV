// rc_view.hlsl – RC-0 panel viewer (tooling, not RC logic): copies a texture into one ImGui can show.
//
// Why it exists: ImGui alpha-blends images. A cascade texel's alpha is the transmittance beta, and beta = 0 means
// "this interval hit something" – exactly the texels that carry a light's colour – so ImGui would draw them fully
// transparent. This pass writes the same rgb with alpha 1 (mode 0), or shows beta itself as grey (mode 1):
// white = nothing hit in the interval, black = hit.

// Sent by the CPU as 1 root constant (RcToy::ViewConstants must have the same layout).
cbuffer ViewConstants : register(b0)
{
    uint mode; // 0 = rgb, 1 = alpha as grey.
};

Texture2D<float4> source : register(t0);  // What to show (a cascade texture), read only.
RWTexture2D<float4> view : register(u0);  // What the panel draws with ImGui::Image.

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float4 value = source[dispatchThreadID.xy];
    float3 color = (mode == 1) ? value.aaa : value.rgb;
    view[dispatchThreadID.xy] = float4(color, 1.0); // Alpha 1: never transparent in the panel.
}
