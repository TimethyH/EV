struct PixelShaderInput
{
    float4 PositionVS : POSITION;
    float3 NormalVS : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct DirectionalLight
{
    float4 DirectionWS;
    float4 DirectionVS;
    float4 Color;
    float Ambient;
    float3 Padding;
};

StructuredBuffer<DirectionalLight> DirectionalLights : register(t2);

cbuffer CameraCB : register(b1, space0)
{
    float3 cameraPosition;
}

float4 main(PixelShaderInput IN) : SV_Target
{
    float3 normal = normalize(IN.NormalVS);
    float3 viewDir = normalize(-IN.PositionVS.xyz);
    
    // Deep ocean color and shallow color
    float3 deepColor = float3(0.01f, 0.05f, 0.15f);
    float3 shallowColor = float3(0.0f, 0.3f, 0.5f);
    
    // Blend based on view angle (steeper = deeper)
    float facing = saturate(dot(normal, viewDir));
    float3 baseColor = lerp(deepColor, shallowColor, facing);
    
    // Directional light
    float3 lightDir = normalize(-DirectionalLights[0].DirectionVS.xyz);
    float3 lightColor = DirectionalLights[0].Color.rgb;
    
    // Diffuse
    float NdotL = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = baseColor * NdotL * lightColor;
    
    // Specular (Blinn-Phong)
    float3 halfVec = normalize(viewDir + lightDir);
    float NdotH = max(dot(normal, halfVec), 0.0f);
    float specularPower = 256.0f;
    float3 specular = lightColor * pow(NdotH, specularPower) * 0.8f;
    
    // Fresnel approximation (more reflective at grazing angles)
    float fresnel = pow(1.0f - facing, 4.0f);
    float3 skyColor = float3(0.4f, 0.6f, 0.9f);
    float3 reflection = skyColor * fresnel;
    
    // Ambient
    float3 ambient = baseColor * 0.15f;
    
    float3 finalColor = ambient + diffuse + specular + reflection;
    // return float4(normal * 0.5 + 0.5, 1.0);
    return float4(finalColor, 1.0f);
}