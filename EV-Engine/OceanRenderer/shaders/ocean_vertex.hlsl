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
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

Texture2D DisplacementTexture : register(t12);
SamplerState linearSampler : register(s0);

static const float HEIGHT_SCALE = 1.0f;

VertexShaderOutput main(VertexPositionNormalTexture data)
{
    float3 displacement = DisplacementTexture.SampleLevel(linearSampler, data.TexCoord, 0).rgb;

    float3 displacedPosition = data.Position;
    displacedPosition += displacement * HEIGHT_SCALE;

    VertexShaderOutput OUT;
    OUT.Position = mul(matrixBuffer.MVP, float4(displacedPosition, 1.0f));
    OUT.PositionVS = mul(matrixBuffer.modelViewMatrix, float4(displacedPosition, 1.0f));
    OUT.PositionWS = mul(matrixBuffer.modelMatrix, float4(displacedPosition, 1.0f)).xyz;
    OUT.NormalVS = mul((float3x3) matrixBuffer.invTransposeModelViewMatrix, data.Normal);
    OUT.TexCoord = data.TexCoord;

    return OUT;
}