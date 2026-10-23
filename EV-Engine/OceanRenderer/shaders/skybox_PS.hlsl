struct PixelShaderInput
{
    // Skybox texture coordinate
    float3 TexCoord : TEXCOORD;
};

TextureCube<float4> SkyboxTexture : register(t0);
SamplerState LinearClampSampler : register(s0);

// Distance fog (SkyboxPSO::SetFog): below the horizon, and a little above it, the sky fades to the fog color so the
// far edge of the ocean disappears in it.
cbuffer FogCB : register(b0)
{
    float3 FogColor;
    float FogAmount; // 0 = no fog.
}

float4 main(PixelShaderInput IN) : SV_Target
{
    float4 sky = SkyboxTexture.Sample(LinearClampSampler, IN.TexCoord);
    const float height = normalize(IN.TexCoord).y;
    const float fog = FogAmount * smoothstep(0.12f, -0.01f, height);
    return float4(lerp(sky.rgb, FogColor, fog), sky.a);
}