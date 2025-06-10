// ================================
// From NRC sample on pathtracing
// ================================

// Jenkins's "one at a time" hash function
uint JenkinsHash(uint x)
{
    x += x << 10;
    x ^= x >> 6;
    x += x << 3;
    x ^= x >> 11;
    x += x << 15;
    return x;
}

// Maps integers to colors using the hash function (generates pseudo-random colors)
float3 HashAndColor(int i)
{
    uint hash = JenkinsHash(i);
    float r = ((hash >> 0) & 0xFF) / 255.0f;
    float g = ((hash >> 8) & 0xFF) / 255.0f;
    float b = ((hash >> 16) & 0xFF) / 255.0f;
    return float3(r, g, b);
}

uint InitRNG(uint2 pixel, uint2 resolution, uint frame)
{
    uint rngState = dot(pixel, uint2(1, resolution.x)) ^ JenkinsHash(frame);
    return JenkinsHash(rngState);
}

float UintToFloat(uint x)
{
    return asfloat(0x3f800000 | (x >> 9)) - 1.f;
}

uint XorShift(inout uint rngState)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

// Generates random normalized float from 0 to 1
float Rand(inout uint rngState)
{
    return UintToFloat(XorShift(rngState));
}

// Generates 2 random normalized floats from 0 to 1
float2 Rand2(inout uint rngState)
{
    return float2(Rand(rngState), Rand(rngState));
}

// Generates 3 random normalized floats from 0 to 1
float3 Rand3(inout uint rngState)
{
    return float3(Rand(rngState), Rand(rngState), Rand(rngState));
}