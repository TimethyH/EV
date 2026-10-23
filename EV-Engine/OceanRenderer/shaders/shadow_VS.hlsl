// Shadow map (ShadowPSO): the casters' depth seen from the light.

struct Shadow
{
    matrix WorldLightViewProjection;
};

ConstantBuffer<Shadow> ShadowCB : register(b0);

struct VertexShaderInput
{
    float3 Position : POSITION;
};

float4 main(VertexShaderInput IN) : SV_Position
{
    return mul(ShadowCB.WorldLightViewProjection, float4(IN.Position, 1.0f));
}
