struct PixelShaderInput
{
    float4 PositionVS : POSITION;
    float3 NormalVS : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct Material
{
    float4 Diffuse;
    //------------------------------------ ( 16 bytes )
    float4 Specular;
    //------------------------------------ ( 16 bytes )
    float4 Emissive;
    //------------------------------------ ( 16 bytes )
    float4 Ambient;
    //------------------------------------ ( 16 bytes )
    float4 Reflectance;
    //------------------------------------ ( 16 bytes )
    float Opacity; // If Opacity < 1, then the material is transparent.
    float SpecularPower;
    float IndexOfRefraction; // For transparent materials, IOR > 0.
    float BumpIntensity; // When using bump textures (height maps) we need
                              // to scale the height values so the normals are visible.
    //------------------------------------ ( 16 bytes )
    float AlphaThreshold; // Pixels with alpha < m_AlphaThreshold will be discarded.
    bool HasAmbientTexture;
    bool HasEmissiveTexture;
    bool HasDiffuseTexture;
    //------------------------------------ ( 16 bytes )
    bool HasSpecularTexture;
    bool HasSpecularPowerTexture;
    bool HasNormalTexture;
    bool HasBumpTexture;
    //------------------------------------ ( 16 bytes )
    bool HasOpacityTexture;
    float3 Padding; // Pad to 16 byte boundary.
    //------------------------------------ ( 16 bytes )
    // Total:                              ( 16 * 9 = 144 bytes )
};

cbuffer MaterialCB : register(b0, space1)
{
    Material material; // Need to declare the actual member inside the cbuffer
};

Texture2D albedoTexture : register(t2);
SamplerState linearSampler : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    float4 diffuse = material.Diffuse; // Reference the member, not the cbuffer name
    float4 texColor = albedoTexture.Sample(linearSampler, IN.TexCoord);
    // return float4(1.0, 0.0, 0.0, 1.0);
	return texColor; // Removed unnecessary parentheses
}