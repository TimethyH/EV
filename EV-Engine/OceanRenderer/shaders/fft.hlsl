#define TOTALPOINTS 512
#define PI 3.14159265359f

cbuffer Constants : register(b0)
{
    uint columnPass;
}

RWTexture2D<float4> outputTexture : register(u0);

float2 complex_multiply(float2 a, float2 b)
{
    return float2(a.x * b.x - a.y * b.y,
                  a.x * b.y + a.y * b.x);
}

groupshared float2 bufferRG[2][TOTALPOINTS];
groupshared float2 bufferBA[2][TOTALPOINTS];


[numthreads(TOTALPOINTS, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    float4 data;
    if (columnPass)
        data = outputTexture.Load(int3(groupID.x, groupIndex, 0)); // column: x fixed, y varies
    else
        data = outputTexture.Load(int3(groupIndex, groupID.x, 0)); // row: x varies, y fixed

    bufferRG[0][groupIndex] = data.rg;
    bufferBA[0][groupIndex] = data.ba;

    GroupMemoryBarrierWithGroupSync();
    
    uint flag = 0;
    for (uint stage = 0; stage < log2(TOTALPOINTS); ++stage)
    {
        uint b = TOTALPOINTS >> (stage + 1);
        uint w = b * (groupIndex / b);
        uint i = (w + groupIndex) % TOTALPOINTS;

        // rotation = e^2 * pi * i * t, this is counterclockwise roation, fft requires clockwise.
		// you scale an amplitude by this rotation over time
        float angle = -2.0f * PI * float(w) / float(TOTALPOINTS);
        float2 twiddle = float2(cos(angle), -sin(angle));

        bufferRG[1 - flag][groupIndex] = bufferRG[flag][i] + complex_multiply(twiddle, bufferRG[flag][i + b]);
        bufferBA[1 - flag][groupIndex] = bufferBA[flag][i] + complex_multiply(twiddle, bufferBA[flag][i + b]);

        flag = 1 - flag;
        GroupMemoryBarrierWithGroupSync();
    }

    float2 resultRG = bufferRG[flag][groupIndex];
    float2 resultBA = bufferBA[flag][groupIndex];

    // if (columnPass)
    // {
    //     resultRG /= float(TOTALPOINTS);
    //     resultBA /= float(TOTALPOINTS);
    // }

    if (columnPass)
        outputTexture[int2(groupID.x, groupIndex)] = float4(resultRG, resultBA);
    else
        outputTexture[int2(groupIndex, groupID.x)] = float4(resultRG, resultBA);
}


/*
 *
 *-------------------------------------- JIM CODE ------------------------------------------------

```cpp
#include "commonFFT.incl"

// https://www.tutorialspoint.com/cplusplus-program-to-perform-complex-number-multiplication
float2 ComplexMult(float2 a, float2 b) {
    return float2(
        a.x * b.x - a.y * b.y,
        a.x * b.y + a.y * b.x
    );
}

float2 ComplexAdd(float2 a, float2 b)
{
    return a + b;
}

float2 ComplexSub(float2 a, float2 b){
    return a - b;
}

float2 ComplexExp(float2 a) {
    return float2(cos(a.y), sin(a.y) * exp(a.x));
}

// Function from https://github.com/GarrettGunnell/Water/blob/main/Assets/Shaders/FFTWater.compute
float4 ComputeTwiddleFactorAndInputIndices(uint2 id) {
    uint b = N >> (id.x + 1);
    float2 mult = 2 * 3.14159265359 * float2(0.0f, 1.0f) / N;
    uint i = (2 * b * (id.y / b) + id.y % b) % N;
    float2 twiddle = ComplexExp(-mult * ((id.y / b) * b));

    return float4(twiddle, i, i + b);
}

void ButterflyValues(uint steps, uint index, out uint2 indices, out float2 twiddle) {
    uint b = N >> (steps + 1);
    uint w = b * (index / b);
    uint i = (w + index) % N;
    sincos(-6.28318530718 / N * w, twiddle.y, twiddle.x); // 6.2831 = PI * 2

    //This is what makes it the inverse FFT
    twiddle.y = -twiddle.y;
    indices = uint2(i, i + b);
}

threadgroupmemory float4 fftGroupBuffer[2][N];

float4 FFT(uint tID, float4 input) {
    fftGroupBuffer[0][tID] = input;
    ThreadGroupMemoryBarrierSync();
    bool flag = false;

    [unroll]
    for (uint steps = 0; steps < LOG_SIZE; ++steps) {
        uint2 inputsIndices;
        float2 twiddle;
        ButterflyValues(steps, threadIndex, inputsIndices, twiddle);

        float4 v = fftGroupBuffer[flag][inputsIndices.y];
        fftGroupBuffer[!flag][tID] = fftGroupBuffer[flag][inputsIndices.x] + float4(ComplexMult(twiddle, v.xy), ComplexMult(twiddle, v.zw));

        flag = !flag;
// sync shared mem
        ThreadGroupMemoryBarrierSync();
    }

    return fftGroupBuffer[flag][threadIndex];
}
```



Texture2D<float4> precomputedData : register(t0);
Texture2D<float4> precomputedData1 : register(t1);

RW_Texture2D<float4> FourierTarget0 : register(u0);
RW_Texture2D<float4> FourierTarget1 : register(u1);

[thread(N, 1, 1)]
void main(uint3 DTid : DISPATCH_THREAD_ID)
{
    FourierTarget0[DTid.xy] = FFT(DTid.x, precomputedData[DTid.xy]);
    FourierTarget1[DTid.xy] = FFT(DTid.x, precomputedData1[DTid.xy]);
}


this is for the horizontal fft
and for thevertical you just swap the yx like this

FourierTarget0[DTid.yx] = FFT(DTid.x, precomputedData[DTid.yx]);
FourierTarget1[DTid.yx] = FFT(DTid.x, precomputedData1[DTid.yx]);
you can also just do it in the same shader tbf
I do it 2 times 1 for the slope map i think and the other is displacement
slope being the normals derivative of the waves and the displacement you apply to a plane




*/