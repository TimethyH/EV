// The birthday game's screens: pictures and text that come out of the tonemapper (HDR_to_SDR_PS, ACES filmic
// with the default parameters) exactly as they are in the image. The color is put through the inverse of that curve,
// so the screens look like a monitor rather than a lit surface. Uses EffectPSO's root signature (vertex.hlsl).

struct PixelShaderInput
{
    float4 PositionVS : POSITION;
    float4 PositionWS : TEXCOORD1;
    float3 NormalVS : NORMAL;
    float3 NormalWS : TEXCOORD2;
    float3 TangentWS : TEXCOORD3;
    float3 BitangentWS : TEXCOORD4;
    float2 TexCoord : TEXCOORD0;
    float4 Position : SV_Position;
};

struct Material
{
    float4 Diffuse;
    float4 Specular;
    float4 Emissive;
    float4 Ambient;
    float4 Reflectance;
    float Metallic;
    float Roughness;
    float Opacity;
    float SpecularPower;
    float indexOfRefraction;
    float bumpIntensity;
    uint hasAmbientTexture;
    uint hasEmissiveTexture;
    uint hasDiffuseTexture;
    uint hasSpecularTexture;
    uint hasSpecularPowerTexture;
    uint hasNormalTexture;
    uint hasBumpTexture;
    uint hasOpacityTexture;
    uint hasMetallicRoughnessTexture;
};

cbuffer MaterialCB : register(b0, space1)
{
    Material material;
};

Texture2D EmissiveTexture : register(t4);
SamplerState anisotropicSampler : register(s0);

// The tonemapper's ACES filmic curve (sdr_pso.cpp defaults).
static const float A = 0.22f;
static const float B = 0.3f;
static const float C = 0.1f;
static const float D = 0.2f;
static const float E = 0.01f;
static const float F = 0.3f;
static const float LinearWhite = 11.2f;
static const float Gamma = 2.2f;

float3 Filmic(float3 x)
{
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// The HDR value that the curve turns into sdr: solve A(y-1)x^2 + B(y-C)x + D(yF-E) = 0 for x.
float3 InverseFilmic(float3 sdr)
{
    const float white = Filmic(LinearWhite).x;
    const float3 y = min(sdr, 0.995f) * white + E / F;
    const float3 a = A * (y - 1.0f);
    const float3 b = B * (y - C);
    const float3 c = D * (y * F - E);
    return max((-b - sqrt(max(b * b - 4.0f * a * c, 0.0f))) / (2.0f * a), 0.0f);
}

float3 LinearToSRGB(float3 color)
{
    const float3 low = color * 12.92f;
    const float3 high = 1.055f * pow(abs(color), 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(color, 0.0031308f));
}

float4 main(PixelShaderInput IN) : SV_Target
{
    // sRGB textures are read as linear values.
    const float3 color = material.hasEmissiveTexture ? EmissiveTexture.Sample(anisotropicSampler, IN.TexCoord).rgb : material.Emissive.rgb;

    // The tonemapper outputs pow(curve(x), 1 / Gamma) to an sRGB-encoded display; aim for the image's sRGB value.
    const float3 sdr = pow(saturate(LinearToSRGB(color)), Gamma);
    return float4(InverseFilmic(sdr), 1.0f);
}
