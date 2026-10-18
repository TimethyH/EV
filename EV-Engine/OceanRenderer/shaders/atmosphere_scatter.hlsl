cbuffer AtmosphereConstants : register(b0)
{
    float4x4 InvProjection;
    float4x4 InvView;
    float3 CameraPositionWS;
    float pad;
    float2 Resolution; 
    float2 pad2;
};

RWTexture2D<float4> OutputTexture : register(u0);

float2 RaySphereIntersect(float3 rayOrigin, float3 rayDirection, float3 spherePos, float radius)
{
    float3 offset = rayOrigin - spherePos;
    float a = 1.0; // dotproduct of normalized rayDir
    float b = 2.0 * dot(offset, rayDirection);
    float c = dot(offset, offset) - radius * radius;
    float d = b * b - 4.0 * a * c;
    
    if (d > 0)
    {
        float s = sqrt(d);
        float dstToSphereNear = max(0.0, (-b - s) / (2.0 * a));
        float dstToSphereFar = (-b + s) / (2.0 * a);

        if (dstToSphereFar >= 0)
        {
            return float2(dstToSphereNear, dstToSphereFar - dstToSphereNear);
        }
    }
    
    return float2(-1.0, 0.0);
}

[numthreads(8, 8, 1)]
void main( uint3 dispatchThreadId : SV_DispatchThreadID )
{
    // Go from UV to clip, to view, to worldspace
    float2 uv = (dispatchThreadId.xy + 0.5) / Resolution;
    float2 clipCoords = float2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    float4 viewDir = mul(InvProjection, float4(clipCoords, 1.0, 1.0));
    viewDir /= viewDir.w;
    viewDir.w = 0.0;
    float3 rayDir = normalize(mul(InvView, viewDir)).xyz;
    float3 rayOrigin = CameraPositionWS;
    
    float2 hit = RaySphereIntersect(rayOrigin, rayDir, float3(0.0, 2.0, 0.0), 22.0);
    
    
    float col = hit.y / 44.0f * 10.0f;
    
    OutputTexture[dispatchThreadId.xy] = float4(col, col, col, 1.0);
    

}