#include "svgf_common.hlsli"

static const int2 kOffsets[5] =
{
    int2(0, 0),
    int2(1, 0), int2(-1, 0),
    int2(0, 1), int2(0, -1)
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
Texture2D<float3> t_Normal      : register(t3, space0);

RWTexture2D<float3> u_Output : register(u0, space0);

[numthreads(8, 8, 1)]
void CSMain_SVGF_ATrous(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_AtrousConstants.resolution.x || tid.y >= g_AtrousConstants.resolution.y)
        return;

    uint2 loc = tid.xy;
    float3 centerC = t_Radiance[tid.xy];
    float var = t_Variance[tid.xy];
    float varDenom = SVGF_LuminanceWeightVarDenom(g_AtrousConstants.phiColor, var);

    float3 nCenter = t_Normal[tid.xy];
    float zCenter = t_Depth[tid.xy];

    float3 sum = 0.0;
    float cumW = 0.0;

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        int2 off = kOffsets[i] * (int) g_AtrousConstants.step;
        int2 p = int2(tid.xy) + off;

        float3 c = t_Radiance[p];
        float3 n = t_Normal[p];
        float z = t_Depth[p];

        float wL = SVGF_LuminanceWeight(c, centerC, varDenom);
        float wN = SVGF_NWeight(n, nCenter) * g_AtrousConstants.phiNormal;
        float wD = SVGF_DWeight(z, zCenter, g_AtrousConstants.phiDepth);

        float w = wL * wN * wD;
        sum += w * c;
        cumW += w;
    }

    u_Output[loc] = sum / cumW;
}