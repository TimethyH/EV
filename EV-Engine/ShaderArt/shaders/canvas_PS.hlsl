struct PSInput
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

float4 main(PSInput IN) : SV_TARGET
{
    return float4(IN.TexCoord, 0.0f, 1.0f); // debug: visualize UVs
}