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

TextureCube<float4> environmentMap : register(t12);
Texture2D SlopeTexture : register(t14);
Texture2D FoamTexture : register(t15);
SamplerState linearSampler : register(s0);

cbuffer CameraCB : register(b1, space0)
{
    float3 cameraPosition;
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

float4 main(PixelShaderInput IN) : SV_Target
{
    // Slope texture contains analytical slopes after IFFT + permute
    float4 slope = SlopeTexture.Sample(linearSampler, IN.TexCoord);
    float foam = FoamTexture.Sample(linearSampler, IN.TexCoord);

    // Reconstruct normal from slopes (object space, Y-up)
    float3 normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    // return float4(normal, 1.0f);
    float PI = 3.14159265358979323846264338327950288f;

    float3 shallowColor = float3(0.0f, 0.3f, 0.5f);

    // PBR stuff
 
    float3 pointLightBRDF = 0;
    float3 directionalLightBRDF = 0;
    float3 BRDF = 0;
    float roughness = 0.2f;
    float metallic = 0.0f;
		
    float3 viewDir = normalize(cameraPosition - IN.PositionWS);
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
    float3 halfVec = normalize(viewDir + dirLightDir);
    float HdotV = max(dot(halfVec, viewDir), 0.0);
    
    float normDist = PBRCalculateNormalDistribution(roughness, normal, halfVec);
    float geometryFunc = PBRCalculateGeometry(normal, viewDir, dirLightDir, roughness);
    float3 fresnel = PBRCalculateFresnel(HdotV, F0, roughness);
    float3 kd = float3(1.0f, 1.0f, 1.0f) - fresnel;
    kd *= float3(1.0f, 1.0f, 1.0f) - metallic;
    float intensity = 1.0f;
 //  // Directional Light BRDF calculation
    directionalLightBRDF += (kd * shallowColor / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDir, dirLightDir, normal))
                                * DirectionalLights[0].Color * intensity * max(dot(dirLightDir, normal), 0.0f);
 
 
    // return float4(roughness, roughness, roughness, roughness);

    // return float4(normalTex * 0.5 + 0.5, 1.0);



    float3 irradiance = environmentMap.Sample(linearSampler, normal).rgb;
    float3 diffuse = irradiance * shallowColor;
    float3 ambient = (kd * diffuse) ; // TODO: ocean doesnt have an AO so cannot be multiplied heree. look at how atlas/warthunnder do it.

     // Combine ambient, point light, and directional light contributions
    // float3 ambient = shallowColor * 0.15f;
    BRDF += ambient + pointLightBRDF + directionalLightBRDF;
	// Add Foam
    float3 foamColor = float3(1.0f, 1.0f, 1.0f);

    BRDF = lerp(BRDF, foamColor, saturate(foam));
    return float4(BRDF, 1.0f);
}