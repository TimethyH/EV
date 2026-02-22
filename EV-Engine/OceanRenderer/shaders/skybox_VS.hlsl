struct Mat
{
    matrix ViewProjectionMatrix;
};

struct VertexShaderInput
{
    float3 Position : POSITION;
};

ConstantBuffer<Mat> MatCB : register(b0);

struct VertexShaderOutput
{
    // Skybox texture coordinate
    float3 TexCoord : TEXCOORD;
    float4 Position : SV_POSITION;
};

VertexShaderOutput main(VertexShaderInput IN)
{
    VertexShaderOutput OUT;

    float4 clip = mul(MatCB.ViewProjectionMatrix, float4(IN.Position, 1.0f));
    OUT.Position = clip.xyww;
    OUT.TexCoord = IN.Position;

    return OUT;
}
