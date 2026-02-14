

float4 Permute(float4 data, float3 id)
{
    return data * (1.0f - 2.0f * ((id.x + id.y) % 2));
}

cbuffer Constants : register(b0)
{
    uint nada;
}

RWTexture2D<float4> outputTexture : register(u0);


[numthreads(16, 16, 1)]
void main( uint3 threadID : SV_DispatchThreadID )
{
    const float2 Lambda = float2(1.0f, 1.0f);

    float4 htildeSlope = Permute(outputTexture[threadID.xy], threadID);

    float2 dyxdyz = htildeSlope.rg;
    float2 dxxdzz = htildeSlope.ba;
	
    float2 slopes = dyxdyz.xy / (1 + abs(dxxdzz * Lambda));
	
    outputTexture[threadID.xy] = float4(slopes, 0.0f, 1.0f);
}