struct PixelShaderInput
{
    float3 PositionWS : POSITION_WS;
    float4 PositionVS : POSITION;
    float3 NormalVS : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct DirectionalLight
{
    float4 DirectionWS;
    float4 DirectionVS;
    float4 Color;
    float3 Position;
    float Ambient;
};

StructuredBuffer<DirectionalLight> DirectionalLights : register(t2);

TextureCube<float4> diffuseMap : register(t12);
TextureCube<float4> specularMap : register(t13);
Texture2D<float2> brdfLUT : register(t14);

Texture2D SlopeTexture : register(t16);
Texture2D FoamTexture : register(t17);

SamplerState anisotropicSampler : register(s0); // for material textures
SamplerState linearClampSampler : register(s1); // for IBL

cbuffer CameraCB : register(b1, space0)
{
    float3 cameraPosition;
    float camPad;
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

float4 main(PixelShaderInput IN) : SV_Target
{
    // Slope texture contains analytical slopes after IFFT + permute
    float4 slope = SlopeTexture.Sample(anisotropicSampler, IN.TexCoord);
    float foam = FoamTexture.Sample(anisotropicSampler, IN.TexCoord);

    // Reconstruct normal from slopes (object space, Y-up)
    float3 normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    // return float4(normal, 1.0f);
    float PI = 3.14159265358979323846264338327950288f;

    // TODO: add ImGUi for color change during runtime
    // float3 shallowColor = float3(0.0f, 0.2f, 0.35f);
    float3 shallowColor = float3(0.0f, 0.1f, 0.25f);

    // PBR stuff
    float3 viewDirWS = normalize(cameraPosition - IN.PositionWS.xyz);
 
    float3 pointLightBRDF = 0;
    float3 directionalLightBRDF = 0;
    float3 BRDF = 0;
    float roughness = 0.2f; // TODO: hardcoded here AND ibl_specular
    float metallic = 0.0f;
		
    float3 F0 = float3(0.02f, 0.02f, 0.02f);
    F0 = F0 * (1.0f - metallic) + shallowColor * metallic;
 
    // {
    //  // -------------------------------
    //      // Point Light Contribution
    //      // -------------------------------
    //     float3 lightPos = PointLights[0].PositionVS;
    //     float3 pointLightDir = normalize(lightPos - IN.PositionVS);
    //     float3 halfVec = normalize(viewDir + pointLightDir);
    //     float dist = length(lightPos - IN.PositionVS);
    //     float HdotV = max(dot(halfVec, viewDir), 0.0);
    //     float attenuation = 1.0f / (dist * dist);
    //     // float3 pointLightColor = pLight.color * attenuation;
    //     float normDist = PBRCalculateNormalDistribution(roughness, normalTex, halfVec);
    //     float geometryFunc = PBRCalculateGeometry(normalTex, viewDir, pointLightDir, roughness);
    //     float3 fresnel = PBRCalculateFresnel(HdotV, F0, roughness);
    //     float3 kd = float3(1.0f, 1.0f, 1.0f) - fresnel;
    //     kd *= float3(1.0f, 1.0f, 1.0f) - metallic;
    //     float3 lightColor = PointLights[0].Color.xyz * attenuation;
    //     float intensity = 1.0f;
    //     pointLightBRDF += (kd * diffuse / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDir, pointLightDir, normalTex))
    //                              * lightColor * intensity * max(dot(pointLightDir, normalTex), 0.0f);
    // }
 
 
    // -------------------------------
    // Directional Light Contribution
    // -------------------------------
    float3 dirLightDir = normalize(-DirectionalLights[0].DirectionWS.xyz);
    float3 halfVec = normalize(viewDirWS + dirLightDir);
    float HdotV = max(dot(halfVec, viewDirWS), 0.0);
    
    float normDist = PBRCalculateNormalDistribution(roughness, normal, halfVec);
    float geometryFunc = PBRCalculateGeometry(normal, viewDirWS, dirLightDir, roughness);
    float3 fresnel = PBRCalculateFresnel(HdotV, F0);
    float3 kd = float3(1.0f, 1.0f, 1.0f) - fresnel;
    kd *= float3(1.0f, 1.0f, 1.0f) - metallic;
    float intensity = 1.0f;
   // Directional Light BRDF calculation
    directionalLightBRDF += (kd * shallowColor / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDirWS, dirLightDir, normal))
                                * DirectionalLights[0].Color * intensity * max(dot(dirLightDir, normal), 0.0f);



    float3 R = reflect(-viewDirWS, normal);
    const float MAX_REFLECTION_LOD = 4.0f;
    float3 specularColor = specularMap.SampleLevel(linearClampSampler, R, roughness * MAX_REFLECTION_LOD);


    float NdotV = max(dot(normal, viewDirWS), 0.0f);
    float2 lutUV = float2(NdotV, roughness);
    float2 envBRDF = brdfLUT.Sample(linearClampSampler, lutUV).rg;

    float3 irradiance = diffuseMap.Sample(linearClampSampler, normal).rgb;
    float3 diffuse = irradiance * shallowColor;
    float3 fresnelIBL = PBRCalculateFresnelIBL(NdotV, F0, roughness);
    float3 kd_ibl = (float3(1.0f, 1.0f, 1.0f) - fresnelIBL) * (1.0f - metallic);
    float3 specular = specularColor * (fresnelIBL * envBRDF.x + envBRDF.y);
    float3 ambient = (kd_ibl * diffuse + specular);



     // Combine ambient, point light, and directional light contributions
    BRDF += ambient + pointLightBRDF + directionalLightBRDF;
	// Add Foam
    float3 foamColor = float3(1.0f, 1.0f, 1.0f);

    BRDF = lerp(BRDF, foamColor, saturate(foam));
    return float4(BRDF, 1.0f);
}