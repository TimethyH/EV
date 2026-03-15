struct PixelShaderInput
{
    float3 PositionWS : POSITION_WS;
    float4 PositionVS : POSITION;
    float3 NormalVS : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float WaveHeight : TEXCOORD1;
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

TextureCube<float4> diffuseMap : register(t3);
TextureCube<float4> specularMap : register(t4);
Texture2D<float2> brdfLUT : register(t5);

// Cascade 0 (lowest freq)
Texture2D SlopeTexture0 : register(t7);
Texture2D FoamTexture0 : register(t8);

// Cascade 1
Texture2D SlopeTexture1 : register(t10);
Texture2D FoamTexture1 : register(t11);

// Cascade 2
Texture2D SlopeTexture2 : register(t13);
Texture2D FoamTexture2 : register(t14);

// Cascade 3 (highest freq)
Texture2D SlopeTexture3 : register(t16);
Texture2D FoamTexture3 : register(t17);

SamplerState anisotropicSampler : register(s0); // for material textures
SamplerState linearClampSampler : register(s1); // for IBL
SamplerState linearWrapSampler : register(s2); // for cubic foam

cbuffer CameraCB : register(b1, space0)
{
    float3 cameraPosition;
    float camPad;
}

cbuffer OceanCB : register(b2)
{
    float4 oceanColor;
    float4 scatteredColor;
    float heightModifier;
    float peakScatterIntensity;
    float IBLIntensity;
    float pad1;
}

cbuffer Constants : register(b3)
{
    float patchSize0;
    float patchSize1;
    float patchSize2;
    float patchSize3;
}


// Cubic B-spline weights (same math as Atlas talk)
float4 CubicWeights(float a)
{
    float a2 = a * a;
    float a3 = a2 * a;
    float w0 = -a3 + a2 * 3.0 - a * 3.0 + 1.0;
    float w1 = a3 * 3.0 - a2 * 6.0 + 4.0;
    float w2 = -a3 * 3.0 + a2 * 3.0 + a * 3.0 + 1.0;
    float w3 = a3;
    return float4(w0, w1, w2, w3) / 6.0;
}

float SampleFoamBicubic(Texture2D foamTex, SamplerState linearSampler, float2 uv)
{
    float2 dims;
    foamTex.GetDimensions(dims.x, dims.y);
    float2 dimsInv = 1.0 / dims;

    // Move to texel space
    float2 tc = uv * dims + 0.5;
    float2 fuv = frac(tc);

    float4 wx = CubicWeights(fuv.x);
    float4 wy = CubicWeights(fuv.y);

    // Merge pairs of weights so we can use 4 bilinear taps instead of 16 point taps
    float4 g = float4(wx.xz + wx.yw, wy.xz + wy.yw); // g.xy = x merged weights, g.zw = y merged weights
    float4 h = (float4(wx.yw, wy.yw) / g + float2(-1.5, 0.5).xyxy + floor(tc).xxyy) * dimsInv.xxyy;

    // h.x, h.y = two sample x positions in UV space
    // h.z, h.w = two sample y positions in UV space
    float2 w = g.xz / (g.xz + g.yw);

    return lerp(
        lerp(foamTex.SampleLevel(linearSampler, float2(h.y, h.w), 0).r,
             foamTex.SampleLevel(linearSampler, float2(h.x, h.w), 0).r, w.x),
        lerp(foamTex.SampleLevel(linearSampler, float2(h.y, h.z), 0).r,
             foamTex.SampleLevel(linearSampler, float2(h.x, h.z), 0).r, w.x),
        w.y);
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

    float foamCascadeMod = 1.0f;
    float2 uv0 = IN.PositionWS.xz / patchSize0;
    float2 uv1 = IN.PositionWS.xz / patchSize1;
    float2 uv2 = IN.PositionWS.xz / patchSize2;
    float2 uv3 = IN.PositionWS.xz / patchSize3;

    float4 slope0 = SlopeTexture0.Sample(anisotropicSampler, uv0);
    float4 slope1 = SlopeTexture1.Sample(anisotropicSampler, uv1);
    float4 slope2 = SlopeTexture2.Sample(anisotropicSampler, uv2);
    float4 slope3 = SlopeTexture3.Sample(anisotropicSampler, uv3);
    float4 slope = slope0 + slope1 + slope2 + slope3;

    float foam0 = SampleFoamBicubic(FoamTexture0, linearWrapSampler, uv0);
    float foam1 = SampleFoamBicubic(FoamTexture1, linearWrapSampler, uv1);
    float foam2 = SampleFoamBicubic(FoamTexture2, linearWrapSampler, uv2);
    float foam3 = SampleFoamBicubic(FoamTexture3, linearWrapSampler, uv3) * foamCascadeMod;
    float foam = saturate(foam0 + foam1 + foam2 + foam3);

    // Reconstruct normal from slopes (object space, Y-up)
    float3 normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    // return float4(normal, 1.0f);
    float PI = 3.14159265358979323846264338327950288f;

    // TODO: add ImGUi for color change during runtime
    // float3 shallowColor = float3(0.0f, 0.2f, 0.35f);

    // PBR stuff
    float3 viewDirWS = normalize(cameraPosition - IN.PositionWS.xyz);
 
    float3 pointLightBRDF = 0;
    float3 directionalLightBRDF = 0;
    float3 BRDF = 0;
    
    float foamRoughnessModifier = 0.8f;
	float roughness = 0.2f + foam * foamRoughnessModifier;
    roughness = saturate(roughness);
	
	// float roughness = 0.2f; // TODO: hardcoded here AND ibl_specular
    float metallic = 0.0f;
		
    float3 F0 = float3(0.02f, 0.02f, 0.02f);
    F0 = F0 * (1.0f - metallic) + oceanColor * metallic;
 
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
    directionalLightBRDF += (kd * oceanColor / PI + PBRSpecular(normDist, geometryFunc, fresnel, viewDirWS, dirLightDir, normal))
                                * DirectionalLights[0].Color * intensity * max(dot(dirLightDir, normal), 0.0f);



    float3 R = reflect(-viewDirWS, normal);
    const float MAX_REFLECTION_LOD = 4.0f;
    float3 specularColor = specularMap.SampleLevel(linearClampSampler, R, roughness * MAX_REFLECTION_LOD);


    float NdotV = max(dot(normal, viewDirWS), 0.0f);
    float2 lutUV = float2(NdotV, roughness);
    float2 envBRDF = brdfLUT.Sample(linearClampSampler, lutUV).rg;

    float3 irradiance = diffuseMap.Sample(linearClampSampler, normal).rgb;
    float3 diffuse = irradiance * oceanColor;
    float3 fresnelIBL = PBRCalculateFresnelIBL(NdotV, F0, roughness);
    float3 kd_ibl = (float3(1.0f, 1.0f, 1.0f) - fresnelIBL) * (1.0f - metallic);
    float3 specular = specularColor * (fresnelIBL * envBRDF.x + envBRDF.y);
    float3 ambient = (kd_ibl * diffuse + specular);

    // SubSurfaceScatter
	// https://github.com/GarrettGunnell/Water/blob/1673a12e796c5745aea6fa26eda53261da8efa80/Assets/Shaders/FFTWater.shader
    float H = max(IN.WaveHeight, 0.0f) * heightModifier;
    float k1 = peakScatterIntensity * H * pow(saturate(dot(dirLightDir, -viewDirWS)), 4.0f) * pow(0.5f - 0.5f * dot(dirLightDir, normal), 3.0f);

    float3 sss = (1 - fresnel) * k1 * scatteredColor * DirectionalLights[0].Color;

     // Combine ambient, point light, and directional light contributions
    BRDF += sss + ambient * IBLIntensity + pointLightBRDF + directionalLightBRDF;
	// Add Foam
    float3 foamColor = float3(1.0f, 1.0f, 1.0f);
    BRDF = lerp(BRDF, foamColor, saturate(foam));
    return float4(BRDF, 1.0f);
}