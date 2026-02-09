[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
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