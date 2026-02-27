#define BLOCK_SIZE 16
#define PI 3.14159265358979323846f

cbuffer SpecularCB : register(b0)
{
    uint cubemapSize;
    uint sampleCount;
    float roughness; // set per mip level dispatch (0.0 -> 1.0)
    float padding;
}

TextureCube<float4> environmentMap : register(t0);
RWTexture2DArray<float4> specularMap : register(u0);

SamplerState linearSampler : register(s0);

static const float3x3 RotateUV[6] =
{
    // +X
    float3x3(0, 0, 1, 0, -1, 0, -1, 0, 0),
    // -X
    float3x3(0, 0, -1, 0, -1, 0, 1, 0, 0),
    // +Y
    float3x3(1, 0, 0, 0, 0, 1, 0, 1, 0),
    // -Y
    float3x3(1, 0, 0, 0, 0, -1, 0, -1, 0),
    // +Z
    float3x3(1, 0, 0, 0, -1, 0, 0, 0, 1),
    // -Z
    float3x3(-1, 0, 0, 0, -1, 0, 0, 0, -1)
};

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}


float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= cubemapSize || DTid.y >= cubemapSize)
        return;

    float3 N = float3(DTid.xy / float(cubemapSize) - 0.5f, 0.5f);
    N = normalize(mul(RotateUV[DTid.z], N));

    float3 R = N;
    float3 V = R;

    float3 prefilteredColor = 0;
    float totalWeight = 0;

    float resolution = float(cubemapSize);
    float saTexel = 4.0 * PI / (6.0 * resolution * resolution);

    for (uint i = 0u; i < sampleCount; ++i)
    {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        if (NdotL > 0.0)
        {
            float D = DistributionGGX(NdotH, roughness);
            float pdf = (D * NdotH / (4.0 * HdotV)) + 0.0001;
            float saSample = 1.0 / (float(sampleCount) * pdf + 0.0001);
            float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefilteredColor += environmentMap.SampleLevel(linearSampler, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / max(totalWeight, 0.001f);
    specularMap[DTid] = float4(prefilteredColor, 1.0f);
}