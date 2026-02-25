#define FOAM_DECAY 0.05f
#define FOAM_BIAS -0.5f
#define FOAM_ADD 0.5f
#define FOAM_THRESHOLD 0.0f


float4 Permute(float4 data, float3 id)
{
    return data * (1.0f - 2.0f * ((id.x + id.y) % 2));
}
cbuffer Constants : register(b0)
{
    uint nada;
}

RWTexture2D<float4> displacementTexture : register(u0);
RWTexture2D<float4> slopeTexture : register(u1);
RWTexture2D<float> foamTexture : register(u2);



[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const float2 Lambda = float2(1.3f, 1.3f);

    // permuting essentially makes sure that the FFT result is centered. (remmeber 3B1B video)
    float4 htildeDisplacement = Permute(displacementTexture.Load(int3(dispatchThreadID.xy, 0)), dispatchThreadID);
    float4 htildeSlope = Permute(slopeTexture[dispatchThreadID.xy], dispatchThreadID);
	
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
    float foam = htildeDisplacement.a;
    foam *= exp(-FOAM_DECAY);
    foam = saturate(foam);
	
    float biasedJacobian = max(0.0f, -(jacobian - FOAM_BIAS));
	
    if (biasedJacobian > FOAM_THRESHOLD)
        foam += FOAM_ADD * biasedJacobian;
	
    slopeTexture[dispatchThreadID.xy] = float4(slopes, 0.0f, 1.0f);
    displacementTexture[dispatchThreadID.xy] = float4(displacement, foam);
    foamTexture[dispatchThreadID.xy] = foam;
}