#include "svgf_common.hlsli"
#include "octahedron_encoding.hlsli"

static const float kKernel[5] = { 1.0, 4.0, 6.0, 4.0, 1.0 };
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
Texture2D<float4> t_Normal      : register(t3, space0);

RWTexture2D<float3> u_Output : register(u0, space0);

[numthreads(8, 8, 1)]
void CSMain_SVGF_ATrous(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_AtrousConstants.resolution.x || tid.y >= g_AtrousConstants.resolution.y)
        return;

    const float K[5] = { 1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };

    uint2 p = tid.xy;
    float3 c0 = t_Radiance[p];
    float lum0 = Luminance(c0);
    float var = t_Variance[p];
    float varScale = g_AtrousConstants.phiColor * sqrt(max(var, 1e-8));

    float z0 = t_Depth[p];
    float3 n0 = Oct16_FastUnpack(t_Normal[p].zw);

    float3 sumC = 0.0;
    float  sumW = 0.0;

    // 5×5 kernel centred on p ------------------------------------------------
    [loop]
    for (int dy = -2; dy <= 2; ++dy)
    {
        int vy = dy * (int) g_AtrousConstants.step;
        float Ky = K[abs(dy)];

        [loop]
        for (int dx = -2; dx <= 2; ++dx)
        {
            int vx = dx * (int) g_AtrousConstants.step;
            float Kx = K[abs(dx)];

            int2 q = int2(p) + int2(vx, vy);

            // Clamp to edge (or early-continue if you'd rather)
            q = clamp(q, int2(0, 0), int2(g_AtrousConstants.resolution) - 1);

            float3 c = t_Radiance[q];
            float lum = Luminance(c);
            float sigma = t_Variance[q];
            float z = t_Depth[q];
            float3 n = Oct16_FastUnpack(t_Normal[q].zw);

            float wz = exp(-abs(z0 - z) / (g_AtrousConstants.phiDepth * g_AtrousConstants.step));
            float wn = pow(max(0.0, dot(n0, n)), g_AtrousConstants.phiNormal);
            float wl = exp(-abs(lum0 - lum) / max(varScale, 1e-6));

            float w = Kx * Ky * wz * wn * wl;

            sumC += w * c;
            sumW += w;
        }
    }

    u_Output[p] = sumC / max(sumW, 1e-4);
}