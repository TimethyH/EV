// rc_reference.hlsl – RC-0 step 2: the brute-force reference ("ground truth") that radiance cascades are compared with.
//
// Per pixel, a few rays per frame in all directions are marched through the canvas (March below), 1 pixel per step, until they hit
// something solid (its emitted light counts) or leave the canvas (nothing). The average over all directions is the
// fluence at that pixel. One frame's few rays are noisy, so the result is averaged over many frames into a persistent
// texture (a running average); RcToy restarts the average whenever the canvas changes.
//
// Canvas convention (docs/path-tracing/knowledge.md "### RC-0", Conventions):
//   rgb = emitted radiance (linear HDR), a = opacity (>= 0.5 = solid). Empty space = (0, 0, 0, 0).

// Sent by the CPU as root constants (RcToy::ReferenceConstants must have the same layout).
cbuffer ReferenceConstants : register(b0)
{
    uint frameIndex; // How many frames are already in the average (0 = start over: overwrite instead of blend).
    uint raysPerFrame; // Rays per pixel this frame.
};

// The painted scene, read only (SRV).
Texture2D<float4> canvas : register(t0);

// The running average (32-bit float: a 16-bit float stops changing once 1/frameIndex gets below its precision).
RWTexture2D<float4> reference : register(u0);

static const float TWO_PI = 6.28318530718;
static const float CANVAS_SIZE = 1024.0;
static const uint MAX_STEPS = 1500; // > the 1448-px diagonal, so every ray can cross the whole canvas.

// A random number in [0, 1) that differs per pixel AND per frame (PCG hash, Jarzynski & Olano 2020, "Hash Functions
// for GPU Rendering"). Same inputs = same number, so it is reproducible; different frames give new directions.
float Random01(uint2 pixel, uint frame)
{
    uint state = pixel.x * 1973u + pixel.y * 9277u + frame * 26699u;
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word >> 8u) / 16777216.0; // Top 24 bits: exactly representable in a float, so never reaches 1.0.
}

// March one ray through the canvas, 1 pixel per step, from `origin` (canvas pixels, a pixel centre) in the unit
// direction `direction`. Returns the light the ray sees: the emitted radiance (rgb) of the first SOLID texel it
// reaches (a wall gives 0, a light its colour), or 0 if it leaves the canvas without hitting anything.
float3 March(float2 origin, float2 direction)
{
    for (uint step = 1; step <= MAX_STEPS; ++step) // starts at 1: the pixel itself is empty (main checks that first)
    {
        float2 position = origin + direction * float(step);
        if (any(position < 0.0) || any(position >= CANVAS_SIZE))
        {
            return float3(0.0, 0.0, 0.0); // Left the canvas: nothing out there.
        }

        float4 texel = canvas[uint2(position)];
        if (texel.a >= 0.5)
        {
            return texel.rgb; // Hit: a light emits its colour, a wall emits 0.
        }
    }
    return float3(0.0, 0.0, 0.0);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    float3 color = float3(0.0, 0.0, 0.0);
    float rand = Random01(pixel, frameIndex); // One random rotation for all of this pixel's rays this frame.

    // The average so far. Frame 0 = start over: never read the old texel (it holds the previous average, or anything,
    // even NaN, on a fresh texture); with old = 0 the blend below gives 0 + (color - 0) / 1 = color.
    float3 oldCol;
    if (frameIndex == 0)
    {
        oldCol = float3(0.0, 0.0, 0.0);
    }
    else
    {
        oldCol = reference[pixel].rgb;
    }
    if (canvas[pixel].a >= 0.5)
    {
        color = canvas[pixel].rgb; // Solid: the pixel itself emits its color
    }
    else
    {
        for (uint ray = 0; ray < raysPerFrame; ++ray)
        {
            float angle = TWO_PI * (rand + float(ray) / float(raysPerFrame)); // Evenly spread over the circle, rotated by a random offset.
            float2 direction = float2(cos(angle), sin(angle));
            color += March(pixel + 0.5, direction); // March from the pixel centre in that direction.
        }
        color /= float(raysPerFrame); // Average over all rays this frame.
    }
    // Running average: old + (new - old) / (n + 1). Same result as (old * n + new) / (n + 1), without the big sum.
    reference[pixel] = float4(oldCol + (color - oldCol) / (frameIndex + 1), 1.0); // Blend into the running average.
}
