// Mips 1-9 of one cascade's 512x512 slope and foam textures in one dispatch of 8x8 groups. Each group reduces its
// 64x64 tile to one texel of mip 6 in groupshared memory; the last group to finish (atomic counter, like AMD's Single
// Pass Downsampler) builds mips 7-9 from mip 6. 2x2 box filter.

#define MIP_COUNT 10
#define GROUPS_PER_SIDE 8 // 512 / 64

// globallycoherent: the last group reads the mip 6 texels the other groups wrote.
globallycoherent RWTexture2D<float4> SlopeMips[MIP_COUNT] : register(u0);
globallycoherent RWTexture2D<float2> FoamMips[MIP_COUNT] : register(u10);
RWByteAddressBuffer FinishedGroups : register(u20);

groupshared float4 sharedSlope[16][16];
groupshared float2 sharedFoam[16][16];
groupshared uint sharedIsLastGroup;

// Halves the size x size tile in groupshared memory into mip 'mip' at tileOrigin (in texels of that mip).
void ReduceShared(uint mip, uint size, uint2 tileOrigin, uint2 thread)
{
    const bool active = all(thread < size);
    float4 slope = 0.0f;
    float2 foam = 0.0f;
    if (active)
    {
        const uint2 c = thread * 2;
        slope = 0.25f * (sharedSlope[c.y][c.x] + sharedSlope[c.y][c.x + 1] + sharedSlope[c.y + 1][c.x] + sharedSlope[c.y + 1][c.x + 1]);
        foam = 0.25f * (sharedFoam[c.y][c.x] + sharedFoam[c.y][c.x + 1] + sharedFoam[c.y + 1][c.x] + sharedFoam[c.y + 1][c.x + 1]);
        SlopeMips[mip][tileOrigin + thread] = slope;
        FoamMips[mip][tileOrigin + thread] = foam;
    }
    // All reads of the level above finish before it is overwritten.
    GroupMemoryBarrierWithGroupSync();
    if (active)
    {
        sharedSlope[thread.y][thread.x] = slope;
        sharedFoam[thread.y][thread.x] = foam;
    }
    GroupMemoryBarrierWithGroupSync();
}

[numthreads(16, 16, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    const uint2 group = groupId.xy;
    const uint2 thread = groupThreadId.xy;

    // Mip 1 from mip 0 (2x2 texels per thread), mip 2 from those (1 texel per thread).
    float4 slope2 = 0.0f;
    float2 foam2 = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        const uint2 texel1 = group * 32 + thread * 2 + uint2(i & 1, i >> 1);
        const uint2 texel0 = texel1 * 2;
        const float4 slope = 0.25f * (SlopeMips[0][texel0] + SlopeMips[0][texel0 + uint2(1, 0)] +
                                      SlopeMips[0][texel0 + uint2(0, 1)] + SlopeMips[0][texel0 + uint2(1, 1)]);
        const float2 foam = 0.25f * (FoamMips[0][texel0] + FoamMips[0][texel0 + uint2(1, 0)] +
                                     FoamMips[0][texel0 + uint2(0, 1)] + FoamMips[0][texel0 + uint2(1, 1)]);
        SlopeMips[1][texel1] = slope;
        FoamMips[1][texel1] = foam;
        slope2 += 0.25f * slope;
        foam2 += 0.25f * foam;
    }
    SlopeMips[2][group * 16 + thread] = slope2;
    FoamMips[2][group * 16 + thread] = foam2;
    sharedSlope[thread.y][thread.x] = slope2;
    sharedFoam[thread.y][thread.x] = foam2;
    GroupMemoryBarrierWithGroupSync();

    // Mips 3-6: 8x8, 4x4, 2x2 and 1x1 texels per group.
    ReduceShared(3, 8, group * 8, thread);
    ReduceShared(4, 4, group * 4, thread);
    ReduceShared(5, 2, group * 2, thread);
    ReduceShared(6, 1, group * 1, thread);

    // Fence first, so the last group sees this group's mip 6 texel.
    if (groupIndex == 0)
    {
        DeviceMemoryBarrier();
        uint finishedBefore;
        FinishedGroups.InterlockedAdd(0, 1, finishedBefore);
        sharedIsLastGroup = finishedBefore == GROUPS_PER_SIDE * GROUPS_PER_SIDE - 1;
    }
    GroupMemoryBarrierWithGroupSync();
    if (!sharedIsLastGroup)
    {
        return;
    }

    // Last group: mips 7-9 from mip 6 (8x8).
    DeviceMemoryBarrierWithGroupSync();
    if (all(thread < GROUPS_PER_SIDE))
    {
        sharedSlope[thread.y][thread.x] = SlopeMips[6][thread];
        sharedFoam[thread.y][thread.x] = FoamMips[6][thread];
    }
    GroupMemoryBarrierWithGroupSync();
    ReduceShared(7, 4, 0, thread);
    ReduceShared(8, 2, 0, thread);
    ReduceShared(9, 1, 0, thread);

    // Reset for the next frame.
    if (groupIndex == 0)
    {
        FinishedGroups.Store(0, 0);
    }
}
