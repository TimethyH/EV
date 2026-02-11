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
    float4 PositionVS : POSITION;
    float3 NormalVS : NORMAL;
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

VertexShaderOutput main(VertexPositionNormalTexture data)
{
    VertexShaderOutput OUT;

    OUT.Position = mul(matrixBuffer.MVP, float4(data.Position, 1.0f));
    OUT.PositionVS = mul(matrixBuffer.modelViewMatrix, float4(data.Position, 1.0f));
    OUT.NormalVS = mul((float3x3) matrixBuffer.invTransposeModelViewMatrix, data.Normal);
    OUT.TexCoord = data.TexCoord;

    return OUT;
}