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

// Textures
Texture2D AmbientTexture : register(t3);
Texture2D EmissiveTexture : register(t4);
Texture2D DiffuseTexture : register(t5);
Texture2D SpecularTexture : register(t6);
Texture2D SpecularPowerTexture : register(t7);
Texture2D NormalTexture : register(t8);
Texture2D BumpTexture : register(t9);
Texture2D OpacityTexture : register(t10);

SamplerState linearSampler : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    float4 diffuse = DiffuseTexture.Sample(linearSampler, IN.TexCoord);
    float4 texColor = AmbientTexture.Sample(linearSampler, IN.TexCoord);
    // return float4(1.0, 0.0, 0.0, 1.0);
	return diffuse; // Removed unnecessary parentheses
}