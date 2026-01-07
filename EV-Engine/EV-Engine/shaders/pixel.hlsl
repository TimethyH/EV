struct PixelShaderInput
{
    float4 PositionVS : POSITION;
    float3 NormalVS : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct Material
{
    float4 albedo;
};

cbuffer MaterialCB : register(b0, space1)
{
    Material material; // Need to declare the actual member inside the cbuffer
};

Texture2D albedoTexture : register(t2);
SamplerState linearSampler : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    float4 diffuse = material.albedo; // Reference the member, not the cbuffer name
    float4 texColor = albedoTexture.Sample(linearSampler, IN.TexCoord);
    return float4(1.0, 0.0, 0.0, 1.0);
	// return diffuse * texColor; // Removed unnecessary parentheses
}