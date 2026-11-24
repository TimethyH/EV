// rc_cascade.hlsl – RC-0 steps 3 + 4: gather one radiance cascade c_k, then merge it with c_(k+1).
//
// One thread per cascade texel, and every texel is one ray: one probe of c_k looking in one direction, over that
// cascade's interval [tStart, tEnd) only. The ray's result is stored as (rgb = radiance seen in the interval,
// a = transmittance beta: 1 = nothing hit in the interval, 0 = hit).
//
// Step 4: the CPU dispatches this shader six times, c5 first. Each dispatch (except c5's) also reads the cascade above
// it, c_(k+1), which is ALREADY merged with everything above it, and continues the ray with it ("over" compositing):
//   L = L_near + beta_near * L_far,   beta = beta_near * beta_far
// So after the c0 dispatch, every c0 texel holds the light along its whole ray, 0 ... 2730 px.
//
// Conventions (docs/path-tracing/knowledge.md "### RC-0", Conventions):
//   c0: a probe every 2 px (512 x 512 probes), 4 directions each. Per cascade up: spacing x2, directions x4,
//   interval length x4, so every cascade is exactly 1024 x 1024 texels.
//   Probe-first layout: probe p of c_k owns a (2^(k+1) x 2^(k+1)) block of texels starting at p * 2^(k+1);
//   inside the block, texel (x, y) is direction n = y * blockSide + x.
//   Direction n of N: angle = 2 pi (n + 0.5) / N.
//   Interval of c_k: [T0 (4^k - 1) / 3, T0 (4^(k+1) - 1) / 3] -> c0 [0,2], c1 [2,10], c2 [10,42], ... c5 [682,2730].
//   Canvas: rgb = emitted radiance, a = opacity (>= 0.5 = solid).

// Sent by the CPU as 2 root constants (RcToy::CascadeConstants must have the same layout).
cbuffer CascadeConstants : register(b0)
{
    uint cascadeIndex; // k: 0 = finest (c0), 5 = coarsest (c5).
    uint mergeWithUpper; // 1 = merge with c_(k+1) in upperCascade; 0 = raw (c5 has nothing above, or Merge is off).
};

Texture2D<float4> canvas : register(t0); // The painted scene, read only.
Texture2D<float4> upperCascade : register(t1); // c_(k+1), already merged (the other ping-pong texture). Read only.
RWTexture2D<float4> cascade : register(u0); // The cascade c_k being written.

static const float TWO_PI = 6.28318530718;
static const float CANVAS_SIZE = 1024.0;
static const float C0_PROBE_SPACING = 2.0; // Canvas pixels between neighbouring c0 probes.
static const uint C0_DIRECTIONS = 4; // Rays per c0 probe.
static const float T0 = 2.0; // Length of c0's interval, in canvas pixels.

// What one cascade texel stands for: one ray of one probe, over one interval.
struct ProbeRay
{
    float2 origin; // The probe's centre, in canvas pixels.
    float2 direction; // Unit vector (canvas y points down, like the texture).
    float tStart; // Where this cascade's interval starts, in pixels along the ray from the origin.
    float tEnd; // Where it ends.
    uint2 probe; // Step 4: which probe of c_k (probe grid coordinates, 0 ... 1024/side - 1).
    uint n; // Step 4: which direction of that probe (0 ... N - 1).
};

// Decode a cascade texel into the ray it stands for (probe centre, direction, interval), for cascade k.
ProbeRay DecodeTexel(uint2 texel, uint k)
{
    ProbeRay ray;
    // One probe's square of texels: side = 2^(k+1) texels wide (= the probe spacing in canvas pixels),
    // and every texel in it is one direction, so N = side * side.
    uint probeSide = 1u << (k + 1);
    uint directions = probeSide * probeSide; // N
    // Which probe owns this texel (quotient) and where inside its block it sits (remainder; side is a power of two,
    // so the low bits are the remainder).
    uint2 probeIndex = texel / probeSide;
    uint2 localTexel = texel & (probeSide - 1u); // texel inside probe's block
    // The probe sits in the middle of its square of canvas: spacing = 2 * 2^k pixels.
    float2 probeOrigin = (probeIndex + 0.5) * C0_PROBE_SPACING * float(1u << k);
    // Direction n = row-major index inside the block; the +0.5 centres each direction in its slice of the circle.
    uint directionIndex = localTexel.x + localTexel.y * probeSide;
    float angle = TWO_PI * (directionIndex + 0.5) / directions;
    float2 dir = float2(cos(angle), sin(angle));
    // Intervals: lengths T0 * 4^k laid end to end, so the start is the geometric sum of the earlier lengths.
    float2 interval = float2(T0 * (pow(4, k) - 1) / 3.0, T0 * (pow(4, (k + 1)) - 1) / 3.0);
    ray.origin = probeOrigin;
    ray.direction = dir;
    ray.tStart = interval.x;
    ray.tEnd = interval.y;
    ray.probe = probeIndex;
    ray.n = directionIndex;
    return ray;
}

// What one ray sees inside its interval only: walk it through the canvas in 1-px steps from tStart towards tEnd and
// stop at the first solid pixel. Returns (rgb = radiance seen, a = transmittance beta):
//   hit a solid pixel        -> (that pixel's emission, 0)   – a wall gives (0,0,0, 0), a light its colour.
//   reached tEnd, no hit     -> (0,0,0, 1)                   – nothing here; a farther cascade must look further.
//   left the canvas, no hit  -> (0,0,0, 1)                   – nothing out there (the knowledge.md convention).
float4 MarchInterval(float2 origin, float2 direction, float tStart, float tEnd)
{
    // Half-open [tStart, tEnd): tEnd belongs to the next cascade, so no distance is checked twice or skipped.
    for (float t = tStart; t < tEnd; t++)
    {
        float2 position = origin + direction * t;

        // Bounds on the position (per axis), before the uint cast: a negative float would wrap to a huge index.
        if (any(position >= CANVAS_SIZE) || any(position < 0.0))
        {
            break;
        }
        // Solid pixel = hit: return its emission (a wall's is black) with beta 0, nothing gets through.
        float4 canvasPixel = canvas[uint2(floor(position))];
        if (canvasPixel.a >= 0.5)
        {
            return float4(canvasPixel.rgb, 0.0);
        }
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}

// Step 4 round 1b (developer's code, moved into its own function for round 2): the light arriving through direction
// n's cone, as seen by ONE upper probe of c_(k+1). upperSide = width of one probe's block of texels in c_(k+1).
float4 ReadUpperCone(uint2 upperProbeId, uint n, uint upperSide)
{
    float4 accumulatedFar = float4(0.0, 0.0, 0.0, 0.0);
    
    // Direction n's slice of the circle is split into 4 in c_(k+1): children 4n ... 4n+3. Together they cover exactly
    // n's slice, so their average is the light arriving through n's whole cone.
    for (uint i = 0; i < 4u; i++)
    {
        
        uint dirIndex = n * 4u + i;
        // Where that child is stored = DecodeTexel backwards: the spot inside the block (undoes n = x + y * side) ...
        uint localX = dirIndex % upperSide;
        uint localY = dirIndex / upperSide;
        // ... = where the upper probe's block starts + the spot.
        uint2 upperTexel = upperProbeId * upperSide + uint2(localX, localY);
        accumulatedFar += upperCascade[upperTexel];
    }
    
    // Averaging beta too: if 2 of the 4 children are blocked, half the cone gets through (soft shadow edges).
    return accumulatedFar / 4.0;
}

// Step 4: continue this ray with what c_(k+1) saw beyond tEnd, and composite it behind this interval's result.
//   near = this texel's own interval (MarchInterval), (rgb = L_near, a = beta_near).
//   far  = c_(k+1)'s answer for the same direction, read from upperCascade (already merged with c_(k+2) ... c5).
// Returns (L_near + beta_near * L_far, beta_near * beta_far).
float4 MergeWithUpper(ProbeRay ray, uint k, float4 near)
{
    // Width of one probe's block of texels in c_(k+1) – and, the same number, the c_(k+1) probe spacing in pixels.
    uint upperSide = 1u << (k + 2);

    // Round 2a: where this probe sits among the upper probes. ray.origin is the probe centre in pixels and the upper
    // spacing in pixels is upperSide, so px -> upper index = / spacing - 0.5 (the -0.5 undoes the +0.5 centre shift:
    // upper probe q's centre lands exactly on q). Every probe sits 1/4 or 3/4 of the way between two upper centres.
    float2 center = ray.origin;
    float2 gridPos = center / upperSide - 0.5;
    // The upper probe to the top-left. Signed: floor(-0.25) = -1 at the canvas edge (floor first, then convert).
    int2 base = int2(floor(gridPos));
    // How far past base this probe is, per axis (0.25 or 0.75): the bilinear weight towards base + 1.
    float2 weight = gridPos - float2(base);

    // How many upper probes there are per axis (c1: 256, c2: 128 ...): the valid probe ids are 0 ... upperCount - 1.
    int upperCount = int(CANVAS_SIZE) / int(upperSide);

    float4 far = float4(0.0, 0.0, 0.0, 0.0);

    // Round 2b: blend the 4 nearest upper probes (bilinear). Per axis, offset 0 gets (1 - weight) and offset 1 gets
    // weight; a corner's weight is its x part times its y part. The 4 add up to 1, so far stays one cone's worth of
    // light and beta stays 0 ... 1 (no division needed).
    int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
    float weights[4] = { (1.0 - weight.x) * (1.0 - weight.y), weight.x * (1.0 - weight.y), (1.0 - weight.x) * weight.y, weight.x * weight.y };
    for (int i = 0; i < 4; ++i)
    {
        // Clamp the PROBE (not the weight) on signed ints: base can be -1 and base + 1 can be upperCount at the edges;
        // the edge probe is then simply used twice, like a sampler's clamp mode.
        int2 upperProbeId = clamp(base + offsets[i], int2(0, 0), int2(upperCount - 1, upperCount - 1));
        far += ReadUpperCone(uint2(upperProbeId), ray.n, upperSide) * weights[i];
    }
    
    // "Over": the far light only counts as much as this interval lets through (near.a = beta_near).
        return float4(near.rgb + (far.rgb * near.a), near.a * far.a);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 texel = dispatchThreadID.xy;
    ProbeRay ray = DecodeTexel(texel, cascadeIndex);
    float4 result = MarchInterval(ray.origin, ray.direction, ray.tStart, ray.tEnd);
    if (mergeWithUpper != 0u)
    {
        result = MergeWithUpper(ray, cascadeIndex, result);
    }
    cascade[texel] = result;
}
