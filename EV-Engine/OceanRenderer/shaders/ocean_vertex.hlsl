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

Texture2D DisplacementTexture : register(t12);
SamplerState linearSampler : register(s0);

// Ocean parameters
static const float HEIGHT_SCALE = 10.0f;
static const float OCEAN_SIZE = 100.0f;
static const float OCEAN_RES = 256.0f;
static const float TEXEL_SIZE = 1.0f / OCEAN_RES;
static const float SAMPLE_OFFSET = OCEAN_SIZE / OCEAN_RES;

VertexShaderOutput main(VertexPositionNormalTexture data)
{
    // Sample displacement at current position
    float height = DisplacementTexture.SampleLevel(linearSampler, data.TexCoord, 0).r;
    
    // Sample neighboring heights for normal calculation
    float heightRight = DisplacementTexture.SampleLevel(linearSampler, data.TexCoord + float2(TEXEL_SIZE, 0.0f), 0).r;
    float heightUp = DisplacementTexture.SampleLevel(linearSampler, data.TexCoord + float2(0.0f, TEXEL_SIZE), 0).r;
    
    // Calculate tangent vectors in object space (XZ plane, Y up)
    float3 tangentX = float3(SAMPLE_OFFSET, (heightRight - height) * HEIGHT_SCALE, 0.0f);
    float3 tangentZ = float3(0.0f, (heightUp - height) * HEIGHT_SCALE, SAMPLE_OFFSET);
    
    // Calculate normal using cross product (object space)
    float3 normalOS = normalize(cross(tangentZ, tangentX));
    
    // Apply displacement
    float3 displacedPosition = data.Position;
    displacedPosition.y += height * HEIGHT_SCALE;
    
    VertexShaderOutput OUT;
    OUT.Position = mul(matrixBuffer.MVP, float4(displacedPosition, 1.0f));
    OUT.PositionVS = mul(matrixBuffer.modelViewMatrix, float4(displacedPosition, 1.0f));
    
    // Transform the calculated normal to view space
    OUT.NormalVS = normalize(mul((float3x3) matrixBuffer.invTransposeModelViewMatrix, normalOS));
    
    OUT.TexCoord = data.TexCoord;
    
    return OUT;
}