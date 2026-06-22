#include "svgf_common.hlsli"
#include "octahedron_encoding.hlsli"

static const int2 OFFS[13] =
{
    int2(-2, -2), int2(0, -2), int2(2, -2),
    int2(-1, -1), int2(1, -1),
    int2(-2, 0), int2(0, 0), int2(2, 0),
    int2(-1, 1), int2(1, 1),
    int2(-2, 2), int2(0, 2), int2(2, 2)
};

static const float KERNEL_W[13] =
{
    0.0030, 0.0146, 0.0030, // row -2
    0.0133, 0.0541, // row -1
    0.0541, 0.2420, 0.0541, // row  0
    0.0133, 0.0541, // row +1
    0.0030, 0.0146, 0.0030 // row +2
};

struct SVGFAtrousConstants
{
    uint2 resolution;
    float step; // 1, 2, 4, … (powers of 2)
    float phiColor; // nominal constant (k)
    float phiNormal;
    float phiDepth;
};
ConstantBuffer<SVGFAtrousConstants> g_AtrousConstants : register(b0);

Texture2D<float3> t_Radiance    : register(t0, space0);
Texture2D<float> t_Variance     : register(t1, space0);
Texture2D<float> t_Depth        : register(t2, space0);
Texture2D<float4> t_Normal      : register(t3, space0);

RWTexture2D<float3> u_Output : register(u0, space0);

[numthreads(8, 8, 1)]
void CSMain_SVGF_ATrous(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_AtrousConstants.resolution.x || tid.y >= g_AtrousConstants.resolution.y)
        return;

    const int step = int(g_AtrousConstants.step); // dilates the kernel
    const float centerDepth = t_Depth.Load(int3(tid.xy, 0));
    const float3 centerNormal = Oct16_FastUnpack(t_Normal.Load(int3(tid.xy, 0)).zw);
    const float3 centerColor = t_Radiance.Load(int3(tid.xy, 0));
    const float centerVar = t_Variance.Load(int3(tid.xy, 0));

    const float sigmaColor = max(1e-4f, sqrt(centerVar)) * g_AtrousConstants.phiColor;

    float3 accum = 0;
    float wSum = 0;

    // iterate the sparse 5×5 kernel (13 samples used)
    [unroll]
    for (uint i = 0; i < 13; ++i)
    {
        int2 p = int2(tid.xy) + OFFS[i] * step;

        // clamp to frame
        p = clamp(p, int2(0, 0), int2(g_AtrousConstants.resolution) - 1);

        const float3 c = t_Radiance.Load(int3(p, 0));
        const float d = t_Depth.Load(int3(p, 0));
        const float3 n = Oct16_FastUnpack(t_Normal.Load(int3(p, 0)).zw);

        // Edge-stopping factors ---------------------------------------------------
        float w_d = exp(-abs(d - centerDepth) / g_AtrousConstants.phiDepth);

        float w_n = pow(max(0, dot(n, centerNormal)), g_AtrousConstants.phiNormal);

        float diff = length(c - centerColor);
        float w_c = exp(-diff / sigmaColor);

        float w = KERNEL_W[i] * w_d * w_n * w_c;

        accum += w * c;
        wSum += w;
    }

    //u_Output[tid.xy] = accum / max(wSum, 1e-4f);
}