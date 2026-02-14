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
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const float2 Lambda = float2(1.0f, 1.0f);

    float4 htildeDisplacement = Permute(outputTexture.Load(int3(dispatchThreadID.xy, 0)), dispatchThreadID);
	
    float2 dxdz = htildeDisplacement.rg;
    float2 dydxz = htildeDisplacement.ba;
	
    float3 displacement = float3(Lambda.x * dxdz.x, dydxz.x, Lambda.y * dxdz.y);
	
    outputTexture[dispatchThreadID.xy] = float4(displacement, 1.0f);
}