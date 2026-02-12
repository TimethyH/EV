#define OCEAN_RESOLUTION 256
#define OCEAN_SIZE 100
#define PI 3.14159265359f
#define GRAVITY 9.81f


float2 complex_multiply(float2 W, float2 B)
{
    return float2(W.x * B.x - W.y * B.y,
                  W.x * B.y + W.y * B.x);
}

cbuffer Constants : register(b0)
{
    float time;
}

Texture2D<float4> H0Texture : register(t0);
RWTexture2D<float2> outputTexture : register(u0);

[numthreads(16, 16, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    float4 H0Data = H0Texture.Load(int3(dispatchThreadID.xy, 0));
    float2 h0 = H0Data.rg;
    float2 h0conj = H0Data.ba;

    float kx = 2.0f * PI * (dispatchThreadID.x - OCEAN_RESOLUTION / 2.0f) / OCEAN_SIZE;
    float ky = 2.0f * PI * (dispatchThreadID.y - OCEAN_RESOLUTION / 2.0f) / OCEAN_SIZE;
    float k = length(float2(kx,ky));

    float w = sqrt(GRAVITY * k);

    float phase = w * time;

    float2 iwt = float2(cos(phase), sin(phase));
    float2 iwtNeg = float2(cos(phase), -sin(phase));

    float2 complexH0 = complex_multiply(h0, iwt) + complex_multiply(h0conj, iwtNeg);

    outputTexture[dispatchThreadID.xy] = complexH0;

}