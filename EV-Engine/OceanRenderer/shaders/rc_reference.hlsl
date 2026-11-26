// rc_reference.hlsl – RC-0 brute-force reference: a few rays per pixel per frame, marched 1 px per step, averaged over
// frames into a persistent texture (RcToy restarts the average when the canvas changes).

// Must match RcToy::ReferenceConstants.
cbuffer ReferenceConstants : register(b0)
{
    uint frameIndex; // Frames already in the average (0 = start over).
    uint raysPerFrame;
};

Texture2D<float4> canvas : register(t0);

// 32-bit float: 16-bit stops changing once 1/frameIndex drops below its precision.
RWTexture2D<float4> reference : register(u0);

static const float TWO_PI = 6.28318530718;
static const float CANVAS_SIZE = 1024.0;
static const uint MAX_STEPS = 1500; // > the 1448-px diagonal.

// PCG hash (Jarzynski & Olano 2020) -> [0, 1), per pixel and frame.
float Random01(uint2 pixel, uint frame)
{
    uint state = pixel.x * 1973u + pixel.y * 9277u + frame * 26699u;
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word >> 8u) / 16777216.0; // Top 24 bits: never reaches 1.0.
}

// Emission of the first solid texel along the ray, or 0 when it leaves the canvas.
float3 March(float2 origin, float2 direction)
{
    for (uint step = 1; step <= MAX_STEPS; ++step) // From 1: main already checked the pixel itself.
    {
        float2 position = origin + direction * float(step);
        if (any(position < 0.0) || any(position >= CANVAS_SIZE))
        {
            return float3(0.0, 0.0, 0.0);
        }

        float4 texel = canvas[uint2(position)];
        if (texel.a >= 0.5)
        {
            return texel.rgb;
        }
    }
    return float3(0.0, 0.0, 0.0);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    float3 color = float3(0.0, 0.0, 0.0);
    float rand = Random01(pixel, frameIndex);

    // Frame 0: never read the old texel (may hold anything, even NaN).
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
        color = canvas[pixel].rgb;
    }
    else
    {
        for (uint ray = 0; ray < raysPerFrame; ++ray)
        {
            float angle = TWO_PI * (rand + float(ray) / float(raysPerFrame)); // Evenly spread, randomly rotated.
            float2 direction = float2(cos(angle), sin(angle));
            color += March(pixel + 0.5, direction);
        }
        color /= float(raysPerFrame);
    }
    // Running average: old + (new - old) / (n + 1).
    reference[pixel] = float4(oldCol + (color - oldCol) / (frameIndex + 1), 1.0);
}
