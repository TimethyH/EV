#include "noise.hlsli"

struct PSInput
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

float4 main(PSInput IN) : SV_TARGET
{
    float2 p = floor(IN.TexCoord * 20.0);
    float h = hash1(p);
    return float4(h, h, h, 1.0);
}