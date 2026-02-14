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
    float Ambient;
    float3 Padding;
};

StructuredBuffer<DirectionalLight> DirectionalLights : register(t2);

Texture2D SlopeTexture : register(t13);
SamplerState linearSampler : register(s0);

cbuffer CameraCB : register(b1, space0)
{
    float3 cameraPosition;
}

float4 main(PixelShaderInput IN) : SV_Target
{
    // Slope texture contains analytical slopes after IFFT + permute
    float4 slope = SlopeTexture.Sample(linearSampler, IN.TexCoord);

    // Reconstruct normal from slopes (object space, Y-up)
    float3 normal = normalize(float3(-slope.x, 1.0f, -slope.y));

    // All lighting in world space
    float3 viewDir = normalize(cameraPosition - IN.PositionWS);

    // Water colors
    float3 deepColor = float3(0.01f, 0.05f, 0.15f);
    float3 shallowColor = float3(0.0f, 0.3f, 0.5f);

    // Directional light (world space)
    float3 lightDir = normalize(-DirectionalLights[0].DirectionWS.xyz);
    float3 lightColor = DirectionalLights[0].Color.rgb;

    // Diffuse
    float NdotL = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = shallowColor * NdotL * lightColor;

    return float4(diffuse, 1.0f);
    //
    // // Specular (Blinn-Phong)
    // float3 halfVec = normalize(viewDir + lightDir);
    // float NdotH = max(dot(normal, halfVec), 0.0f);
    // float3 specular = lightColor * pow(NdotH, 256.0f) * 0.8f;
    //
    // // Fresnel (Schlick approximation)
    // float fresnel = pow(1.0f - facing, 4.0f);
    // float3 skyColor = float3(0.4f, 0.6f, 0.9f);
    // float3 reflection = skyColor * fresnel;
    //
    // // Ambient
    // float3 ambient = shallowColor * 0.15f;
    //
    // float3 finalColor = ambient + diffuse + specular + reflection;
    //
    // return float4(finalColor, 1.0f);
}