
// Hash functions for "controlled" chaos
// The derivative of this noise is the slope.

//  used in 3D value noise to hash a single index into one of the 8 cell corners.
float hash1(float n)
{
    // Multiply irrational nr 17, take its decimal and repeat (* 1/pi)
    return frac(n * 17.0 * frac(n * 0.3183099));
}

// hash for 2D value noise, for the terrain
float hash1(float2 p)
{
    p = 50.0 * frac(p * 0.3183099);
    return frac(p.x * p.y * (p.x + p.y));
}

// hash used for randomizing tree pos and scale within a grid cell
float2 hash2(float2 p)
{
    const float2 k = float2(0.3183099, 0.3678794); // 1/pi, 1/e
    float n = 111.0 * p.x + 113.0 * p.y;
    return frac(n * frac(k * n));
}

// TODO: understand a bit better how / why this works,
// for now accept it for what it is

// 2D value noise, no derivatives
float noise(float2 x)
{
    float2 p = floor(x);
    float2 t = frac(x);

    float2 u = t * t * t * (t * (t * 6.0 - 15.0) + 10.0); // quintic polynomials chosen by inigo quilez, has 0 derivative at each end.

    float a = hash1(p + float2(0, 0));
    float b = hash1(p + float2(1, 0));
    float c = hash1(p + float2(0, 1));
    float d = hash1(p + float2(1, 1));

    return -1.0 + 2.0 * (a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y);
}

// 2D value noise with analytical derivatives
// returns float3(value, d/dx, d/dy)
float3 noised(float2 x)
{
    float2 p = floor(x);
    float2 t = frac(x);

    float2 u = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    float2 du = 30.0 * t * t * (t * (t - 2.0) + 1.0);

    float a = hash1(p + float2(0, 0));
    float b = hash1(p + float2(1, 0));
    float c = hash1(p + float2(0, 1));
    float d = hash1(p + float2(1, 1));

    float k0 = a;
    float k1 = b - a;
    float k2 = c - a;
    float k4 = a - b - c + d;

    return float3(
        -1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k4 * u.x * u.y),
        2.0 * du.x * (k1 + k4 * u.y),
        2.0 * du.y * (k2 + k4 * u.x)
    );
}