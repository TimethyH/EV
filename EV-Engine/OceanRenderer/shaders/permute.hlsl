float4 Permute(float4 data, float3 id)
{
    return data * (1.0f - 2.0f * ((id.x + id.y) % 2));
}
cbuffer Constants : register(b0)
{
    float foamDecay;  // per second
    float foamBias;   // foam forms where the Jacobian is below this
    float foamAdd;    // per second, at full strength
    float foamRange;  // Jacobian range below foamBias to reach full strength
    float deltaTime;
}

RWTexture2D<float4> displacementTexture : register(u0);
RWTexture2D<float4> slopeTexture : register(u1);
RWTexture2D<float2> foamTexture : register(u2); // r = foam, g = lambda * Dxz



[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const float2 Lambda = float2(1.3f, 1.3f);

    // permuting essentially makes sure that the FFT result is centered. (remmeber 3B1B video)
    float4 htildeDisplacement = Permute(displacementTexture.Load(int3(dispatchThreadID.xy, 0)), dispatchThreadID);
    float4 htildeSlope = Permute(slopeTexture[dispatchThreadID.xy], dispatchThreadID);
    
    float foamData = foamTexture.Load(int3(dispatchThreadID.xy, 0)).r;

    // // Blur kernel. Adjusting centerweight for blur amount
    //int2 texel = int2(dispatchThreadID.xy);
    //float centerWeight = 1.0f;
    //float foamData = foamTexture.Load(int3(texel, 0)) * centerWeight;
    //float neighborSum = 0.0f;
	//[unroll]
    //for (int dx = -1; dx <= 1; dx++)
    //{
    //[unroll]
    //    for (int dy = -1; dy <= 1; dy++)
    //    {
    //        if (dx == 0 && dy == 0)
    //            continue;
    //        neighborSum += foamTexture.Load(int3(texel + int2(dx, dy), 0));
    //    }
    //}
    //foamData += neighborSum * ((1.0f - centerWeight) / 8.0f);
	
    float2 dxdz = htildeDisplacement.rg;
    float2 dydxz = htildeDisplacement.ba;
    float2 dyxdyz = htildeSlope.rg;
    float2 dxxdzz = htildeSlope.ba;
	
	
    float2 slopes = dyxdyz.xy / (1 + abs(dxxdzz * Lambda));
    float3 displacement = float3(Lambda.x * dxdz.x, dydxz.x, Lambda.y * dxdz.y);
    float covariance = slopes.x * slopes.y; // covariance is the correlation of change between x and y. 

    // Jacobian 
    float jacobian = (1.0f + Lambda.x * dxxdzz.x) * (1.0f + Lambda.y * dxxdzz.y) - Lambda.x * Lambda.y * dydxz.y * dydxz.y;
	
	// TODO: this is most probably garbage data. check how you're storing the data. 
    float foam = foamData * exp(-foamDecay * deltaTime);
    float forming = saturate((foamBias - jacobian) / max(foamRange, 0.0001f)); // ramp, not a step: no popping
    foam += foamAdd * forming * deltaTime;
    foam = saturate(foam);

    // Jacobian terms for the pixel shader, which sums them over all cascades (crest foam).
    slopeTexture[dispatchThreadID.xy] = float4(slopes, Lambda.x * dxxdzz.x, Lambda.y * dxxdzz.y);
    displacementTexture[dispatchThreadID.xy] = float4(displacement, foam);
    foamTexture[dispatchThreadID.xy] = float2(foam, Lambda.x * dydxz.y);
}