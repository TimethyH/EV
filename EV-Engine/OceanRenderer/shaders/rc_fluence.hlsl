// rc_fluence.hlsl – RC-0: fluence per canvas pixel = bilinear blend of the 4 nearest c0 probes' average light.
// One thread per canvas pixel. Output: rgb = fluence (linear HDR), a = 1.

Texture2D<float4> cascade0 : register(t0); // c0, merged with c1 ... c5.
Texture2D<float4> canvas : register(t1);
RWTexture2D<float4> fluence : register(u0);

static const uint C0_PROBE_SIDE = 2; // c0 block width in texels = c0 probe spacing in px.
static const uint C0_PROBE_COUNT = 512;

// Average of one c0 probe's 4 directions.
float3 ProbeFluence(uint2 probe)
{
    float3 sum = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < 4; i++)
    {
        int2 offset = int2(i % 2, i / 2); // (0,0), (1,0), (0,1), (1,1)
        uint2 texelCoord = probe * C0_PROBE_SIDE + uint2(offset);
        sum += cascade0[texelCoord].rgb; // a = beta, not light.
    }
    sum /= 4.0;
    return sum;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    // Solid pixels show their own emission, like rc_reference.hlsl.
    float4 canvasPixel = canvas[pixel];
    if (canvasPixel.a >= 0.5)
    {
        fluence[pixel] = float4(canvasPixel.rgb, 1.0);
        return;
    }

    float3 light = float3(0.0, 0.0, 0.0);

    // Pixel centre in c0-probe units; -0.5 puts probe q's centre on q.
    float2 origin = (pixel + 0.5);
    float2 gridPos = origin / C0_PROBE_SIDE - 0.5;
    int2 base = int2(floor(gridPos)); // Signed: -1 at the left/top edge.
    float2 weight = gridPos - float2(base);

    int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
    float weights[4] = { (1.0 - weight.x) * (1.0 - weight.y), weight.x * (1.0 - weight.y), (1.0 - weight.x) * weight.y, weight.x * weight.y };
    for (int i = 0; i < 4; ++i)
    {
        // Clamp the probe, not the weight.
        int2 probe = clamp(base + offsets[i], int2(0, 0), int2(C0_PROBE_COUNT - 1, C0_PROBE_COUNT - 1));
        light += ProbeFluence(uint2(probe)) * weights[i];
    }

    fluence[pixel] = float4(light, 1.0);
}
