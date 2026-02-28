struct PixelShaderInput
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
    float Metallic; // If Opacity < 1, then the material is transparent.
    float Roughness;
    float Opacity; // For transparent materials, IOR > 0.
    float SpecularPower; // When using bump textures (height maps) we need
                              // to scale the height values so the normals are visible.
    float indexOfRefraction; // For transparent materials, IOR > 0.
    float bumpIntensity;
    //------------------------------------ ( 16 bytes )
    uint hasAmbientTexture;
    uint hasEmissiveTexture;
    uint hasDiffuseTexture;
    uint hasSpecularTexture;
       // ---------------------------------- ( 16 bytes )
    uint hasSpecularPowerTexture;
    uint hasNormalTexture;
    uint hasBumpTexture;
    uint hasOpacityTexture;
    uint hasMetallicRoughnessTexture;
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
    float3 Position;
    float Ambient;
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
    float pad;
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
    // float kDirect = ((roughness + 1.0f) * (roughness + 1.0f)) / 8.0f;
    float kIBL = (roughness * roughness) * 0.5f;
    float nom = NdotV;
    float denom = NdotV * (1.0f - kIBL) + kIBL;
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


float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 PBRCalculateFresnel(const float cosTheta, float3 F0)
{
    return fresnelSchlick(cosTheta, F0);
}

float3 PBRCalculateFresnelIBL(const float cosTheta, float3 F0, float roughness)
{
    return fresnelSchlickRoughness(cosTheta, F0, roughness);
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
// IBL
TextureCube<float4> diffuseMap : register(t12);
TextureCube<float4> specularMap : register(t13);
Texture2D<float2> brdfLUT : register(t14);


SamplerState anisotropicSampler : register(s0); // for material textures
SamplerState linearClampSampler : register(s1); // for IBL


float4 main(PixelShaderInput IN) : SV_Target
{
    float3 albedo = DiffuseTexture.Sample(anisotropicSampler, IN.TexCoord).rgb;
    float4 ao = AmbientTexture.Sample(anisotropicSampler, IN.TexCoord);
    float3 normalTex = NormalTexture.Sample(anisotropicSampler, IN.TexCoord).xyz * 2.0f - 1.0f;
    float4 metallicRough = MetallicRoughness.Sample(anisotropicSampler, IN.TexCoord);
    float4 emissive = EmissiveTexture.Sample(anisotropicSampler, IN.TexCoord);
    float PI = 3.14159265358979323846264338327950288f;

    float3 ambientColor = float3(0.03f, 0.03f, 0.03f);

    float3 T = normalize(IN.TangentWS);
    float3 B = normalize(IN.BitangentWS);
    float3 N = normalize(IN.NormalWS);
    float3x3 TBN = float3x3(T, B, N);

    float3 normalWS;
    if (material.hasNormalTexture)
    {
        float3 normalTex = NormalTexture.Sample(anisotropicSampler, IN.TexCoord).xyz * 2.0f - 1.0f;
        float3 T = normalize(IN.TangentWS);
        float3 B = normalize(IN.BitangentWS);
        float3 N = normalize(IN.NormalWS);
        float3x3 TBN = float3x3(T, B, N);
        normalWS = normalize(mul(normalTex, TBN));
    }
    else
    {
        normalWS = normalize(IN.NormalWS);
    }

    // PBR stuff
 
    float3 pointLightBRDF = 0;
    float3 directionalLightBRDF = 0;
    float3 BRDF = 0;
    float roughness = metallicRough.g;
    float metallic = metallicRough.b;

    // return float4(roughness, roughness, roughness, 1.0);
		
		
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = F0 * (1.0f - metallic) + albedo * metallic;
 
    float3 viewDirWS = normalize(camera.Position - IN.PositionWS.xyz);

// Point light
    float3 lightPosWS = PointLights[0].PositionWS.xyz;
    float3 pointLightDir = normalize(lightPosWS - IN.PositionWS.xyz);
    float3 halfVec = normalize(viewDirWS + pointLightDir);
    float dist = length(lightPosWS - IN.PositionWS.xyz);
    float HdotV = max(dot(halfVec, viewDirWS), 0.0);
    float attenuation = 1.0f / (dist * dist);
    float normDist = PBRCalculateNormalDistribution(roughness, normalWS, halfVec);
    float geometryFunc = PBRCalculateGeometry(normalWS, viewDirWS, pointLightDir, roughness);
    float3 fresnel = PBRCalculateFresnel(HdotV, F0);
    float3 kd = float3(1.0f, 1.0f, 1.0f) - fresnel;
    kd *= float3(1.0f, 1.0f, 1.0f) - metallic;
    float3 lightColor = PointLights[0].Color.xyz * attenuation;
    pointLightBRDF += (kd * albedo / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDirWS, pointLightDir, normalWS))
                 * lightColor * max(dot(pointLightDir, normalWS), 0.0f);

// Directional light
    float3 dirLightDir = normalize(-DirectionalLights[0].DirectionWS.xyz);
    float3 halfVecDir = normalize(viewDirWS + dirLightDir);
    float HdotVDir = max(dot(halfVecDir, viewDirWS), 0.0);
    float normDistDir = PBRCalculateNormalDistribution(roughness, normalWS, halfVecDir);
    float geometryFuncDir = PBRCalculateGeometry(normalWS, viewDirWS, dirLightDir, roughness);
    float3 fresnelDir = PBRCalculateFresnel(HdotVDir, F0);
    float3 kdDir = (float3(1.0f, 1.0f, 1.0f) - fresnelDir) * (1.0f - metallic);
    directionalLightBRDF += (kdDir * albedo / PI + PBRSpecular(normDistDir, geometryFuncDir, fresnelDir, viewDirWS, dirLightDir, normalWS))
                       * DirectionalLights[0].Color * max(dot(dirLightDir, normalWS), 0.0f);
 

    float3 R = reflect(-viewDirWS, normalWS);
    const float MAX_REFLECTION_LOD = 4.0f;
    float3 specularColor = specularMap.SampleLevel(linearClampSampler, R, roughness * MAX_REFLECTION_LOD);


    float NdotV = max(dot(normalWS, viewDirWS), 0.0f);
    float2 lutUV = float2(NdotV, roughness);
    float2 envBRDF = brdfLUT.Sample(linearClampSampler, lutUV).rg;

    float3 irradiance = diffuseMap.Sample(linearClampSampler, normalWS).rgb;
    float3 diffuse = irradiance * albedo;
    float3 fresnelIBL = PBRCalculateFresnelIBL(NdotV, F0, roughness);
    float3 kd_ibl = (float3(1.0f, 1.0f, 1.0f) - fresnelIBL) * (1.0f - metallic);
    float3 specular = specularColor * (fresnelIBL * envBRDF.x + envBRDF.y);
    float3 ambient = (kd_ibl * diffuse + specular) * ao.r;

     // Combine ambient, point light, and directional light contributions
    BRDF += emissive + pointLightBRDF + directionalLightBRDF + ambient;
    return float4(BRDF, 1.0f);
}