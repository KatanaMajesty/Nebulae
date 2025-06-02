#define PI 3.1415926535
#define FLT_MAX 3.402823466e+38f

// Hammersley 2-D point generator
// reference: PBRT v4, "Halton & Hammersley Sequences" 8.6
float2 Hammersley(uint i, uint N)
{
    // radical-inverse in base-2 (Van der Corput) with bit-twiddling reversal
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

    float radical = float(bits) * 2.3283064365386963e-10f; // 1/2^32
    return float2(float(i) / float(N), radical);
}

// Shirley–Chiu concentric map from [0,1)^2 -> unit disk
// reference: "A Low-Distortion Map Between Disk and Square" (Shirley & Chiu, 1997)
float2 ConcentricSampleDisk(float2 xi)
{
    // Shift to -1...1 square
    float2 a = 2.0f * xi - 1.0f;

    // Handle the origin explicitly to avoid NANs
    if (a.x == 0.0f && a.y == 0.0f)
        return float2(0.0f, 0.0f);

    float r, phi;
    if (abs(a.x) > abs(a.y))
    {
        r = a.x;
        phi = (PI * 0.25f) * (a.y / a.x);
    }
    else
    {
        r = a.y;
        phi = (PI * 0.5f) - (PI * 0.25f) * (a.x / a.y);
    }

    return r * float2(cos(phi), sin(phi));
}

float3 GetPerpendicularVector(float3 u)
{
    float3 a = abs(u);
    uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
    uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, float3(xm, ym, zm));
}

// Clever offset_ray function from Ray Tracing Gems chapter 6
// Offsets the ray origin from current position p, along normal n (which must be geometric normal)
// so that no self-intersection can occur.
float3 OffsetRay(const float3 p, const float3 n)
{
    static const float origin = 1.0f / 32.0f;
    static const float float_scale = 1.0f / 65536.0f;
    static const float int_scale = 256.0f;

    int3 of_i = int3(int_scale * n.x, int_scale * n.y, int_scale * n.z);

    float3 p_i =
        float3(asfloat(asint(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)), asfloat(asint(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)), asfloat(asint(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return float3(abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x, abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y, abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

// ================================
// From RTXGI sample on pathtracing
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

float Rand(inout uint rngState)
{
    return UintToFloat(XorShift(rngState));
}