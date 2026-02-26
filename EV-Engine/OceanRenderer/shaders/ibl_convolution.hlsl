#define BLOCK_SIZE 16
#define PI 3.14159265358979323846f

cbuffer IrradianceCB : register(b0)
{
    uint cubemapSize;
	uint sampleCount;
    float padding;
    float padding2;
}

TextureCube<float4> environmentMap : register(t0);
RWTexture2DArray<float4> irradianceMap : register(u0);

SamplerState linearSampler : register(s0);

static const float3x3 RotateUV[6] =
{
    // +X
    float3x3(0, 0, 1,
              0, -1, 0,
             -1, 0, 0),
    // -X
    float3x3(0, 0, -1,
              0, -1, 0,
              1, 0, 0),
    // +Y
    float3x3(1, 0, 0,
              0, 0, 1,
              0, 1, 0),
    // -Y
    float3x3(1, 0, 0,
              0, 0, -1,
              0, -1, 0),
    // +Z
    float3x3(1, 0, 0,
              0, -1, 0,
              0, 0, 1),
    // -Z
    float3x3(-1, 0, 0,
              0, -1, 0,
              0, 0, -1)
};

void BuildTBN(float3 N, out float3 T, out float3 B)
{
	// this is done so program doesnt crash when N is close to up
    float3 up = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}


[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint3 texCoord = dispatchThreadID;

    // if we're outside of the mip size, we get out.
    if (texCoord.x >= cubemapSize || texCoord.y >= cubemapSize)
    {
        return;
    }

    // Get the worldspace normal for the
    float3 N = float3(texCoord.xy / float(cubemapSize) - 0.5f, 0.5f);
    N = normalize(mul(RotateUV[texCoord.z], N));

    float3 T;
    float3 B;
    BuildTBN(N, T, B);

    // Riemann sum
    float3 irradiance = 0;
    float sampleDelta = (PI / 2.0f) / float(sampleCount);
    uint numSamples = 0;

    // phi  = azimuth, which is the horizontal angle
    for (float phi = 0.0f; phi < 2.0f * PI; phi += sampleDelta)
    {
	    // theta is the polar, which is half the sphere going up.
        for (float theta = 0.0f; theta < PI * 0.5f; theta += sampleDelta)
        {
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));

             // Transform to world space
            float3 sampleDir = tangentSample.x * T
                             + tangentSample.y * B
                             + tangentSample.z * N;

            irradiance += environmentMap.SampleLevel(linearSampler, sampleDir, 0).rgb
                        * cos(theta) * sin(theta);

            numSamples++;

        }

    }
    irradiance = PI * irradiance / float(numSamples);

    irradianceMap[texCoord] = float4(irradiance, 1.0f);
}