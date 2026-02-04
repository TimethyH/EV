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

struct Camera
{
    float3 Position;
};

cbuffer CameraCB : register(b1, space0)
{
    Camera camera;
}
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


float PBRCalculateNormalDistribution(float roughness, const float3 normal, const float3 halfvec)
{
  
    float PI = 3.14159265358979323846264338327950288f;

    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(normal, halfvec), 0.0);
    float NdotH2 = NdotH * NdotH;
	
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float PBRCalculateGeometryGGX(float NdotV, float roughness)
{
    // Roughness here is dependent on IBL or Direct lighting.
    // This implementation will only take into account Direct Lighting.
    float kDirect = ((roughness + 1.0f) * (roughness + 1.0f)) / 8.0f;
    // float kIBL = (roughness * roughness) * 0.5f;
    float nom = NdotV;
    float denom = NdotV * (1.0f - kDirect) + kDirect;
    denom = max(denom, 0.000001f); // prevent / 0
    return nom / denom;
}

float PBRCalculateGeometry(const float3 normal, const float3 viewDir, const float3 lightDir, float roughness)
{

    float NdotV = max(dot(normal, viewDir), 0.0f);
    float NdotL = max(dot(normal, lightDir), 0.0f);

    float ggx1 = PBRCalculateGeometryGGX(NdotV, roughness);
    float ggx2 = PBRCalculateGeometryGGX(NdotL, roughness);
    return ggx1 * ggx2;
}


float3 fresnelSchlick(float cosTheta, float3 F0, float roughness)
{

   // #ifdef IBL
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
   // #else
   // return F0 + (float3(1.0f) - F0) * pow(1.0f - cosTheta, 5.0f);
}

float3 PBRCalculateFresnel(const float3 view, const float3 normal, float3 F0, float roughness)
{

	//float3 F0 = float3(0.04f);  // Base reflectivity for dielectrics

    // Calculate dot product between the half vector and view direction
    float HdotV = max(dot(view, normal), 0.0f);
    float3 fresnel = fresnelSchlick(HdotV, F0, roughness);

    return fresnel;
}

float3 PBRDiffuse(float3 color)
{
    float PI = 3.14159265358979323846264338327950288f;
    return color / PI;

}

float3 PBRSpecular(float normalDist, float geometry, float3 fresnel, float3 viewDir, float3 lightDir, float3 normal)
{
    float3 nom = normalDist * geometry * fresnel;
    float denom = 4.0f * (max(dot(viewDir, normal), 0.0f) * max(dot(lightDir, normal), 0.0f)); // max in case denom == 0
    denom = max(denom, 0.00001f);
    return nom / denom;
}

float4 main(PixelShaderInput IN) : SV_Target
{
    float4 diffuse = float4(camera.Position, 1.0);
    float4 texColor = AmbientTexture.Sample(linearSampler, IN.TexCoord);
    // return float4(1.0, 0.0, 0.0, 1.0);
	return diffuse; // Removed unnecessary parentheses

 //    // PBR stuff
 //
 //    float3 pointLightBRDF = 0;
 //    float3 directionalLightBRDF = 0;
 //    float3 BRDF = 0;
 //
 //    float3 viewDir = normalize(srt - > pCamera - > position - gPos);
 //    float3 F0 = float3(0.04f, 0.04f, 0.04f);
 //    F0 = F0 * (1.0f - metallic) + gAlbedo * metallic;
 //
 //     // -------------------------------
 //         // Point Light Contribution
 //         // -------------------------------
 //    float3 pointLightDir = normalize(pLight.position - gPos);
 //    float3 halfVec = normalize(viewDir + pointLightDir);
 //        // float dist    = length(pLight.position - gPos);
 //        // float attenuation = 1.0f / (dist * dist);
 //        // float3 pointLightColor = pLight.color * attenuation;
 //    float normDist = PBRCalculateNormalDistribution(roughness, gNormal.xyz, halfVec);
 //    float geometryFunc = PBRCalculateGeometry(gNormal.xyz, viewDir, pointLightDir, roughness);
 //    float3 fresnel = PBRCalculateFresnel(viewDir, gNormal.xyz, F0, roughness);
 //    float3 kd = float3(1.0f) - fresnel;
 //    kd *= float3(1.0f) - metallic;
 //    pointLightBRDF += (kd * gAlbedo / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDir, pointLightDir, gNormal.xyz))
 //                                 * pLight.color * pLight.intensity * max(dot(pointLightDir, gNormal.xyz), 0.0f);
 //
 //
 //    // -------------------------------
 //    // Directional Light Contribution
 //    // -------------------------------
 //    float3 dirLightDir = normalize(srt - > dLight - > direction);
 //    halfVec = normalize(viewDir + dirLightDir);
 //    
 //    normDist = PBRCalculateNormalDistribution(roughness, gNormal.xyz, halfVec);
 //    geometryFunc = PBRCalculateGeometry(gNormal.xyz, viewDir, dirLightDir, roughness);
 //    fresnel = PBRCalculateFresnel(viewDir, gNormal.xyz, F0, roughness);
 //    kd = float3(1.0f) - fresnel;
 //    kd *= float3(1.0f) - metallic;
 //   
 // //  // Directional Light BRDF calculation
 //    directionalLightBRDF += (kd * gAlbedo / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDir, dirLightDir, gNormal.xyz))
 //                                * srt - > dLight - > color * srt - > dLight - > intensity * max(dot(dirLightDir, gNormal.xyz), 0.0f);
 //
 //
 //     // Combine ambient, point light, and directional light contributions
 //    BRDF += emissive + pointLightBRDF + directionalLightBRDF + ambient;
}