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

struct PointLight
{
    float4 PositionWS; // Light position in world space.
    //----------------------------------- (16 byte boundary)
    float4 PositionVS; // Light position in view space.
    //----------------------------------- (16 byte boundary)
    float4 Color;
    //----------------------------------- (16 byte boundary)
    float Ambient;
    float ConstantAttenuation;
    float LinearAttenuation;
    float QuadraticAttenuation;
    //----------------------------------- (16 byte boundary)
    // Total:                              16 * 4 = 64 bytes
};

// struct SpotLight
// {
//     float4 PositionWS; // Light position in world space.
//     //----------------------------------- (16 byte boundary)
//     float4 PositionVS; // Light position in view space.
//     //----------------------------------- (16 byte boundary)
//     float4 DirectionWS; // Light direction in world space.
//     //----------------------------------- (16 byte boundary)
//     float4 DirectionVS; // Light direction in view space.
//     //----------------------------------- (16 byte boundary)
//     float4 Color;
//     //----------------------------------- (16 byte boundary)
//     float Ambient;
//     float SpotAngle;
//     float ConstantAttenuation;
//     float LinearAttenuation;
//     //----------------------------------- (16 byte boundary)
//     float QuadraticAttenuation;
//     float3 Padding;
//     //----------------------------------- (16 byte boundary)
//     // Total:                              16 * 7 = 112 bytes
// };

struct DirectionalLight
{
    float4 DirectionWS; // Light direction in world space.
    //----------------------------------- (16 byte boundary)
    float4 DirectionVS; // Light direction in view space.
    //----------------------------------- (16 byte boundary)
    float4 Color;
    //----------------------------------- (16 byte boundary)
    float Ambient;
    float3 Padding;
    //----------------------------------- (16 byte boundary)
    // Total:                              16 * 4 = 64 bytes
};

// struct LightProperties
// {
//     uint NumPointLights;
//     uint NumSpotLights;
//     uint NumDirectionalLights;
// };

// struct LightResult
// {
//     float4 Diffuse;
//     float4 Specular;
//     float4 Ambient;
// };

// cbuffer LightPropertiesCB : register(b1)
// {
//     LightProperties lightProperty;
// }

StructuredBuffer<PointLight> PointLights : register(t0);
// StructuredBuffer<SpotLight> SpotLights : register(t1);
StructuredBuffer<DirectionalLight> DirectionalLights : register(t2);


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

float3 PBRCalculateFresnel(const float cosTheta, float3 F0, float roughness)
{

	//float3 F0 = float3(0.04f);  // Base reflectivity for dielectrics

    // Calculate dot product between the half vector and view direction
    // float HdotV = max(dot(view, normal), 0.0f);
    float3 fresnel = fresnelSchlick(cosTheta, F0, roughness);

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

// Textures
Texture2D AmbientTexture : register(t3);
Texture2D EmissiveTexture : register(t4);
Texture2D DiffuseTexture : register(t5);
Texture2D SpecularTexture : register(t6);
Texture2D SpecularPowerTexture : register(t7);
Texture2D NormalTexture : register(t8);
Texture2D BumpTexture : register(t9);
Texture2D OpacityTexture : register(t10);
Texture2D MetallicRoughness : register(t11);


SamplerState linearSampler : register(s0);


float4 main(PixelShaderInput IN) : SV_Target
{
    float4 diffuse = DiffuseTexture.Sample(linearSampler, IN.TexCoord);
    float4 ao = AmbientTexture.Sample(linearSampler, IN.TexCoord);
    float3 normalTex = NormalTexture.Sample(linearSampler, IN.TexCoord).xyz  * 2.0f - 1.0f;
    float4 metallicRough = MetallicRoughness.Sample(linearSampler, IN.TexCoord);
    float4 emissive = EmissiveTexture.Sample(linearSampler, IN.TexCoord);
    float PI = 3.14159265358979323846264338327950288f;

    float3 ambientColor = float3(0.03f, 0.03f, 0.03f);
    float3 ambient = ambientColor * diffuse.rgb * ao.r;

    normalTex = normalize(IN.NormalVS);
    // return float4(1.0, 0.0, 0.0, 1.0);
    // return float4(diffuse); // Removed unnecessary parentheses

    // PBR stuff
 
    float3 pointLightBRDF = 0;
    float3 directionalLightBRDF = 0;
    float3 BRDF = 0;
    float roughness = metallicRough.g;
    float metallic = metallicRough.b;
		
    float3 viewDir = normalize(-IN.PositionVS.xyz);
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = F0 * (1.0f - metallic) + diffuse.rgb * metallic;
 
    {
     // -------------------------------
         // Point Light Contribution
         // -------------------------------
        float3 lightPos = PointLights[0].PositionVS;
        float3 pointLightDir = normalize(lightPos - IN.PositionVS);
        float3 halfVec = normalize(viewDir + pointLightDir);
        float dist = length(lightPos - IN.PositionVS);
        float HdotV = max(dot(halfVec, viewDir), 0.0);
        float attenuation = 1.0f / (dist * dist);
        // float3 pointLightColor = pLight.color * attenuation;
        float normDist = PBRCalculateNormalDistribution(roughness, normalTex, halfVec);
        float geometryFunc = PBRCalculateGeometry(normalTex, viewDir, pointLightDir, roughness);
        float3 fresnel = PBRCalculateFresnel(HdotV, F0, roughness);
        float3 kd = float3(1.0f, 1.0f, 1.0f) - fresnel;
        kd *= float3(1.0f, 1.0f, 1.0f) - metallic;
        float3 lightColor = PointLights[0].Color.xyz * attenuation;
        float intensity = 1.0f;
        pointLightBRDF += (kd * diffuse / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDir, pointLightDir, normalTex))
                                 * lightColor * intensity * max(dot(pointLightDir, normalTex), 0.0f);
    }
 
 
    // -------------------------------
    // Directional Light Contribution
    // -------------------------------
    float3 dirLightDir = normalize(-DirectionalLights[0].DirectionVS.xyz);
    float3 halfVec = normalize(viewDir + dirLightDir);
    float HdotV = max(dot(halfVec, viewDir), 0.0);
    
    float normDist = PBRCalculateNormalDistribution(roughness, normalTex, halfVec);
    float geometryFunc = PBRCalculateGeometry(normalTex, viewDir, dirLightDir, roughness);
    float3 fresnel = PBRCalculateFresnel(HdotV, F0, roughness);
    float3 kd = float3(1.0f,1.0f,1.0f) - fresnel;
    kd *= float3(1.0f,1.0f,1.0f) - metallic;
    float intensity = 1.0f;
 //  // Directional Light BRDF calculation
    directionalLightBRDF += (kd * diffuse / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDir, dirLightDir, normalTex))
                                * DirectionalLights[0].Color * intensity * max(dot(dirLightDir, normalTex), 0.0f);
 
 
    // return float4(roughness, roughness, roughness, roughness);

    // return float4(normalTex * 0.5 + 0.5, 1.0);


     // Combine ambient, point light, and directional light contributions
    // BRDF += emissive + pointLightBRDF + directionalLightBRDF + ambient;
    BRDF += emissive + pointLightBRDF + directionalLightBRDF;
    // BRDF = ambient;
    return float4(BRDF, 1.0f);
}