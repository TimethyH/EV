struct Matrices
{
    matrix modelMatrix;
    matrix modelViewMatrix;
    matrix invTransposeModelViewMatrix;
    matrix MVP;
};

cbuffer matrixCB : register(b0)
{
    Matrices matrixBuffer;
}

cbuffer Constants : register(b3)
{
    float patchSize0;
    float patchSize1;
    float patchSize2;
    float patchSize3;
}

struct VertexPositionNormalTexture
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct VertexShaderOutput
{
    float3 PositionWS : POSITION_WS;
    float4 PositionVS : POSITION;
    float3 NormalVS : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Position : SV_Position;
    float WaveHeight : TEXCOORD1;
};

Texture2D DisplacementTexture0 : register(t6);
Texture2D DisplacementTexture1 : register(t9);
Texture2D DisplacementTexture2 : register(t12);
Texture2D DisplacementTexture3 : register(t15);

SamplerState linearWrapSampler : register(s2);
static const float HEIGHT_SCALE = 1.0f;

VertexShaderOutput main(VertexPositionNormalTexture data)
{
    float2 uv0 = data.Position.xz / patchSize0;
    float2 uv1 = data.Position.xz / patchSize1;
    float2 uv2 = data.Position.xz / patchSize2;
    float2 uv3 = data.Position.xz / patchSize3;

    float3 displacement0 = DisplacementTexture0.SampleLevel(linearWrapSampler, uv0, 0).rgb;
    float3 displacement1 = DisplacementTexture1.SampleLevel(linearWrapSampler, uv1, 0).rgb;
    float3 displacement2 = DisplacementTexture2.SampleLevel(linearWrapSampler, uv2, 0).rgb;
    float3 displacement3 = DisplacementTexture3.SampleLevel(linearWrapSampler, uv3, 0).rgb;
    float3 displacement = displacement0 + displacement1 + displacement2 + displacement3;

    float3 displacedPosition = data.Position;
    displacedPosition += displacement * HEIGHT_SCALE;

    VertexShaderOutput OUT;
    OUT.Position = mul(matrixBuffer.MVP, float4(displacedPosition, 1.0f));
    OUT.PositionVS = mul(matrixBuffer.modelViewMatrix, float4(displacedPosition, 1.0f));
    OUT.PositionWS = mul(matrixBuffer.modelMatrix, float4(displacedPosition, 1.0f)).xyz;
    OUT.NormalVS = mul((float3x3) matrixBuffer.invTransposeModelViewMatrix, data.Normal);
    OUT.TexCoord = data.TexCoord;
    OUT.WaveHeight = displacement.y;

    return OUT;
}