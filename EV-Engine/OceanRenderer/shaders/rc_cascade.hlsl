// rc_cascade.hlsl – RC-0: gather one radiance cascade c_k and merge it with the already merged c_(k+1).
// One thread per texel = one ray (probe, direction, interval). Result: rgb = radiance, a = transmittance beta.
// Layout and conventions: knowledge.md "### RC-0" + the RC-0 Patroni boards.

// Must match RcToy::CascadeConstants.
cbuffer CascadeConstants : register(b0)
{
    uint cascadeIndex; // k: 0 = finest, 5 = coarsest.
    uint mergeWithUpper;
    uint overlap;
    uint bilinearFix;
    float intervalT0; // c0's interval length in px.
};

Texture2D<float4> canvas : register(t0); // rgb = emission, a = opacity (>= 0.5 = solid).
Texture2D<float4> upperCascade : register(t1); // c_(k+1), already merged.
RWTexture2D<float4> cascade : register(u0);

static const float TWO_PI = 6.28318530718;
static const float CANVAS_SIZE = 1024.0;
static const float C0_PROBE_SPACING = 2.0;
static const uint C0_DIRECTIONS = 4;

struct ProbeRay
{
    float2 origin; // Probe centre in canvas px.
    float2 direction; // Canvas y points down.
    float tStart;
    float tEnd;
    uint2 probe;
    uint n; // Direction index.
};

ProbeRay DecodeTexel(uint2 texel, uint k)
{
    ProbeRay ray;
    uint probeSide = 1u << (k + 1);
    uint directions = probeSide * probeSide; // N
    uint2 probeIndex = texel / probeSide;
    uint2 localTexel = texel & (probeSide - 1u); // texel inside probe's block
    float2 probeOrigin = (probeIndex + 0.5) * C0_PROBE_SPACING * float(1u << k);
    uint directionIndex = localTexel.x + localTexel.y * probeSide;
    float angle = TWO_PI * (directionIndex + 0.5) / directions;
    float2 dir = float2(cos(angle), sin(angle));
    // Lengths t0 * 4^k laid end to end.
    float2 interval = float2(intervalT0 * (pow(4, k) - 1) / 3.0, intervalT0 * (pow(4, (k + 1)) - 1) / 3.0);
    ray.origin = probeOrigin;
    ray.direction = dir;
    ray.tStart = interval.x;
    ray.tEnd = interval.y;
    ray.probe = probeIndex;
    ray.n = directionIndex;
    return ray;
}

// 1-px steps over [tStart, tEnd). Hit -> (emission, 0); no hit or off the canvas -> (0, 0, 0, 1).
float4 MarchInterval(float2 origin, float2 direction, float tStart, float tEnd)
{
    for (float t = tStart; t < tEnd; t++)
    {
        float2 position = origin + direction * t;

        // Check before the uint cast: a negative float would wrap.
        if (any(position >= CANVAS_SIZE) || any(position < 0.0))
        {
            break;
        }
        float4 canvasPixel = canvas[uint2(floor(position))];
        if (canvasPixel.a >= 0.5)
        {
            return float4(canvasPixel.rgb, 0.0);
        }
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}

// Average of direction n's 4 children (4n ... 4n+3) in one upper probe = the light through n's cone.
float4 ReadUpperCone(uint2 upperProbeId, uint n, uint upperSide)
{
    float4 accumulatedFar = float4(0.0, 0.0, 0.0, 0.0);

    for (uint i = 0; i < 4u; i++)
    {

        uint dirIndex = n * 4u + i;
        uint localX = dirIndex % upperSide;
        uint localY = dirIndex / upperSide;
        uint2 upperTexel = upperProbeId * upperSide + uint2(localX, localY);
        accumulatedFar += upperCascade[upperTexel];
    }

    return accumulatedFar / 4.0;
}

// The 4 upper probes a c_k probe blends + bilinear weights (sum 1).
struct UpperProbes
{
    uint2 ids[4]; // Clamped. Order: (0,0) (1,0) (0,1) (1,1).
    float weights[4];
    uint side; // c_(k+1) block width in texels = its probe spacing in px.
};

UpperProbes FindUpperProbes(ProbeRay ray, uint k)
{
    UpperProbes upper;

    uint upperSide = 1u << (k + 2);

    // -0.5 undoes the probe-centre shift, so upper probe q's centre lands on q.
    float2 center = ray.origin;
    float2 gridPos = center / upperSide - 0.5;
    int2 base = int2(floor(gridPos)); // Signed: -1 at the canvas edge.
    float2 weight = gridPos - float2(base);

    int upperCount = int(CANVAS_SIZE) / int(upperSide);

    int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
    float weights[4] = { (1.0 - weight.x) * (1.0 - weight.y), weight.x * (1.0 - weight.y), (1.0 - weight.x) * weight.y, weight.x * weight.y };
    for (int i = 0; i < 4; ++i)
    {
        // Clamp the probe, not the weight (edge probe used twice).
        upper.ids[i] = uint2(clamp(base + offsets[i], int2(0, 0), int2(upperCount - 1, upperCount - 1)));
        upper.weights[i] = weights[i];
    }
    upper.side = upperSide;
    return upper;
}

// Plain merge: near "over" the blended far.
float4 MergeWithUpper(ProbeRay ray, uint k, float4 near)
{
    UpperProbes upper = FindUpperProbes(ray, k);

    float4 far = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = 0; i < 4; ++i)
    {
        far += ReadUpperCone(upper.ids[i], ray.n, upper.side) * upper.weights[i];
    }

    return float4(near.rgb + (far.rgb * near.a), near.a * far.a);
}

// Bilinear fix: each upper probe gets its own near segment ending where its ray starts; sum of w_i * (near_i over far_i).
// Replaces the near march and the merge.
float4 MergeWithUpperFixed(ProbeRay ray, uint k)
{
    UpperProbes upper = FindUpperProbes(ray, k);

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = 0; i < 4; ++i)
    {
        uint2 upCenter = upper.ids[i] * upper.side + uint2(upper.side / 2, upper.side / 2);
        float2 segStart = ray.origin + ray.tStart * normalize(ray.direction);
        float2 segEnd = upCenter + ray.direction * ray.tEnd;
        float2 dir = normalize(segEnd - segStart);
        float dist = length(segEnd - segStart);

        float4 near = MarchInterval(segStart, dir, 0, dist);
        float4 far = ReadUpperCone(upper.ids[i], ray.n, upper.side);
        // Composite per probe first, then weight.
        result += float4(near.rgb + (far.rgb * near.a), near.a * far.a) * upper.weights[i];
    }
    return result;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 texel = dispatchThreadID.xy;
    ProbeRay ray = DecodeTexel(texel, cascadeIndex);

    if (mergeWithUpper != 0u && bilinearFix != 0u)
    {
                   cascade[texel] = MergeWithUpperFixed(ray, cascadeIndex);
        return;
    }

    // Local copy: the merge needs the true ray.tEnd.
    float marchEnd = ray.tEnd;

    // Overlap: march one upper probe spacing past tEnd.
    if (overlap != 0u && cascadeIndex < 5u)
    {
        uint upperSide = 1u << (cascadeIndex + 2);
        marchEnd += float(upperSide);
    }

    float4 result = MarchInterval(ray.origin, ray.direction, ray.tStart, marchEnd);
    if (mergeWithUpper != 0u)
    {
        result = MergeWithUpper(ray, cascadeIndex, result);
    }
    cascade[texel] = result;
}
