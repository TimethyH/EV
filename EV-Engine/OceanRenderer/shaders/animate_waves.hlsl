#define OCEAN_RESOLUTION 256
#define OCEAN_SIZE 100
#define PI 3.14159265359f
#define GRAVITY 9.81f
#define REPEAT_TIME 100.0f


float2 complex_multiply(float2 W, float2 B)
{
    return float2(W.x * B.x - W.y * B.y,
                  W.x * B.y + W.y * B.x);
}

float2 EulerFormula(float x)
{
    return float2(cos(x), sin(x));
}

cbuffer Constants : register(b0)
{
    float time;
}

Texture2D<float4> H0Texture : register(t0);
RWTexture2D<float4> slopeTexture : register(u0);
RWTexture2D<float4> displacementTexture : register(u1);

[numthreads(16, 16, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    float4 H0Data = H0Texture.Load(int3(dispatchThreadID.xy, 0));
    float2 h0 = H0Data.rg;
    float2 h0conj = H0Data.ba;

    const uint lengthScale = OCEAN_RESOLUTION / 16;


    float kx = 2.0f * PI * (dispatchThreadID.x - OCEAN_RESOLUTION / 2.0f) / OCEAN_SIZE;
    float ky = 2.0f * PI * (dispatchThreadID.y - OCEAN_RESOLUTION / 2.0f) / OCEAN_SIZE;
    float k = length(float2(kx,ky));
    float kRcp = rcp(k);

    if (k < 0.0001f)
    {
        kRcp = 1.0f;
    }


    float w = 2.0f * PI / REPEAT_TIME;
    float dispersion = floor(sqrt(GRAVITY * k) / w) * w * time; // TODO: lookup where this version comes from

    float2 exponent = EulerFormula(dispersion);

    float2 htilde = complex_multiply(h0, exponent) + complex_multiply(h0conj, float2(exponent.x, -exponent.y));
    float2 ih = float2(-htilde.y, htilde.x);

    // Derivatives TODO: UNDERSTAND HOW THESE WORK!~~!~!~!

    float2 displacementX = ih * kx * kRcp;
    float2 displacementY = htilde;
    float2 displacementZ = ih * ky * kRcp;
    float2 displacementX_dx = -htilde * kx * kx * kRcp;
    float2 displacementY_dx = ih * kx;
    float2 displacementZ_dx = -htilde * kx * ky * kRcp;
    float2 displacementY_dz = ih * ky;
    float2 displacementZ_dz = -htilde * ky * ky * kRcp;


    float2 htildeDisplacementX = float2(displacementX.x - displacementZ.y, displacementX.y + displacementZ.x);
    float2 htildeDisplacementZ = float2(displacementY.x - displacementZ_dx.y, displacementY.y + displacementZ_dx.x);
        
    float2 htildeSlopeX = float2(displacementY_dx.x - displacementY_dz.y, displacementY_dx.y + displacementY_dz.x);
    float2 htildeSlopeZ = float2(displacementX_dx.x - displacementZ_dz.y, displacementX_dx.y + displacementZ_dz.x);


    displacementTexture[dispatchThreadID.xy] = float4(htildeDisplacementX, htildeDisplacementZ);
    slopeTexture[dispatchThreadID.xy] = float4(htildeSlopeX, htildeSlopeZ);

    // float phase = w * time;
    //
    // float2 iwt = float2(cos(phase), sin(phase));
    // float2 iwtNeg = float2(cos(phase), -sin(phase));
    //
    // float2 complexH0 = complex_multiply(h0, iwt) + complex_multiply(h0conj, iwtNeg);
    //
    // outputTexture[dispatchThreadID.xy] = complexH0;

}