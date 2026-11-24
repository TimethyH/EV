// rc_fluence.hlsl – RC-0 step 5: turn the merged c0 into one light value per canvas pixel (the fluence).
//
// After step 4, every c0 texel holds the light arriving at one c0 probe through one of its 4 wide cones (90 degrees
// each), along the WHOLE ray (0 ... 2730 px), because c1 ... c5 were merged into it. A probe's fluence = the average
// of its 4 directions = the average light arriving from all around it. That is the same quantity the brute-force
// reference (rc_reference.hlsl) computes per pixel, so the two can be compared directly (Difference view).
//
// One thread per canvas PIXEL (1024 x 1024), not per cascade texel. Output: rgb = fluence (linear HDR), a = 1.
//
// c0 layout (docs/path-tracing/knowledge.md "### RC-0", Conventions): a probe every 2 px (512 x 512 probes), probe p's
// centre at (p + 0.5) * 2 px; probe p owns the 2 x 2 texel block starting at p * 2, direction n = x + y * 2 inside it.

Texture2D<float4> cascade0 : register(t0); // c0, merged with c1 ... c5 (rgb = radiance of the cone, a = beta).
Texture2D<float4> canvas : register(t1); // The painted scene: solid pixels show their own emission, like the reference.
RWTexture2D<float4> fluence : register(u0); // What the panel shows (through rc_view.hlsl).

static const uint C0_PROBE_SIDE = 2; // c0 block width in texels = c0 probe spacing in pixels.
static const uint C0_PROBE_COUNT = 512; // c0 probes per axis (1024 / 2).

// The fluence of ONE c0 probe: the average of its 4 merged directions.
float3 ProbeFluence(uint2 probe)
{
    float3 sum = float3(0.0, 0.0, 0.0);

    // The probe's 4 texels = its 2 x 2 block in cascade0 (block start = probe * side, like ReadUpperCone with side 2);
    // inside the block, direction n = x + y * 2, so offset (n % 2, n / 2) is direction n. The 4 cones of 90 degrees
    // together cover the whole circle, so their plain average is the light from all around the probe.
    for (int i = 0; i < 4; i++)
    {
        int2 offset = int2(i % 2, i / 2); // (0,0), (1,0), (0,1), (1,1)
        uint2 texelCoord = probe * C0_PROBE_SIDE + uint2(offset);
        sum += cascade0[texelCoord].rgb; // rgb only: a = beta says what got through, it is not light.
    }
    // Equal shares (4 directions of the same width) -> divide by 4. (Unlike a bilinear blend, whose weights are
    // already the shares.)
    sum /= 4.0;
    return sum;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    // A solid pixel (wall or light) shows its own emission, exactly like rc_reference.hlsl does, so the Difference
    // view only shows where RC itself differs (a probe next to a wall would otherwise "see through" into it).
    float4 canvasPixel = canvas[pixel];
    if (canvasPixel.a >= 0.5)
    {
        fluence[pixel] = float4(canvasPixel.rgb, 1.0);
        return;
    }

    float3 light = float3(0.0, 0.0, 0.0);

    // Round 5b: bilinear over the 4 nearest c0 probes – MergeWithUpper's round 2 one level down (pixels are the fine
    // grid now, c0 the coarse one). Positions stay float2 until the deliberate floor: a uint2 would cut the fractions.
    // The pixel centre (the reference marches from there too), in c0-probe units: / spacing - 0.5 puts probe q's
    // centre exactly on q. Pixels sit 1/4 or 3/4 of the way between two probe centres.
    float2 origin = (pixel + 0.5);
    float2 gridPos = origin / C0_PROBE_SIDE - 0.5;
    int2 base = int2(floor(gridPos)); // Signed: -1 at the left/top edge.
    float2 weight = gridPos - float2(base); // 0.25 or 0.75 per axis: the weight towards base + 1.

    // The 4 corners and their weights (x part times y part; they add up to 1, so no division).
    int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
    float weights[4] = { (1.0 - weight.x) * (1.0 - weight.y), weight.x * (1.0 - weight.y), (1.0 - weight.x) * weight.y, weight.x * weight.y };
    for (int i = 0; i < 4; ++i)
    {
        // Clamp the PROBE (not the weight) on signed ints: base can be -1 and base + 1 can be C0_PROBE_COUNT at the
        // edges; the edge probe is then simply used twice, like a sampler's clamp mode.
        int2 probe = clamp(base + offsets[i], int2(0, 0), int2(C0_PROBE_COUNT - 1, C0_PROBE_COUNT - 1));
        light += ProbeFluence(uint2(probe)) * weights[i];
    }

    fluence[pixel] = float4(light, 1.0);
}
