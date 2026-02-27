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
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float3 TexCoord : TEXCOORD; 
};

struct VertexShaderOutput
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

VertexShaderOutput main(VertexPositionNormalTexture data)
{
    VertexShaderOutput OUT;

    OUT.Position = mul(matrixBuffer.MVP, float4(data.Position, 1.0f));
    OUT.PositionVS = mul(matrixBuffer.modelViewMatrix, float4(data.Position, 1.0f));
    OUT.PositionWS = mul(matrixBuffer.modelMatrix, float4(data.Position, 1.0f));
    OUT.NormalVS = mul((float3x3) matrixBuffer.invTransposeModelViewMatrix, data.Normal);
    OUT.NormalWS = mul((float3x3) matrixBuffer.modelMatrix, data.Normal);
    OUT.TangentWS = mul((float3x3) matrixBuffer.modelMatrix, data.Tangent);
    OUT.BitangentWS = mul((float3x3) matrixBuffer.modelMatrix, data.Bitangent);
    OUT.TexCoord = data.TexCoord.xy; // take only xy
    return OUT;
}