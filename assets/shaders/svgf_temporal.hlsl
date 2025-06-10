#include "svgf_common.hlsli"

struct SVGFConstants
{
    uint2 resolution;
    float depthSigma;
    float alpha; // tweakable stability var
    float varianceEps; // small value
};
ConstantBuffer<SVGFConstants> g_SVGFConstants : register(b0, space0);

Texture2D<float3> t_RadianceHistory     : register(t0, space0);
Texture2D<float> t_Depth                : register(t1, space0);
Texture2D<float> t_DepthHistory         : register(t2, space0);
Texture2D<float3> t_Normal              : register(t3, space0);
Texture2D<float3> t_NormalHistory       : register(t4, space0);
Texture2D<float2> t_MomentHistory       : register(t5, space0);

RWTexture2D<float3> t_Radiance : register(u0, space0);
RWTexture2D<float2> t_Moment : register(u1, space0);
RWTexture2D<float> t_Variance : register(u2, space0);

[numthreads(8, 8, 1)]
void CSMain_SVGF_Temporal(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_SVGFConstants.resolution.x || tid.y >= g_SVGFConstants.resolution.y)
        return;

    uint2 loc = tid.xy;

    float3 curRadiance = t_Radiance[loc];
    float curDepth = t_Depth[loc];
    float3 curNormal = t_Normal[loc];

    // history look-up    
    float3 historyRadiance = t_RadianceHistory[loc];
    float historyDepth = t_DepthHistory[loc];
    float3 historyNormal = t_NormalHistory[loc];

    // stability norm
    float wDepth = SVGF_DWeight(curDepth, historyDepth, g_SVGFConstants.depthSigma);
    float wNormal = SVGF_NWeight(curNormal, historyNormal);
    float w = wDepth * wNormal;

    // lerp parameter:  Alpha_small -> faster reset, less ghosting
    //                  Alpha_large -> longer history, smoother noise
    float alpha = g_SVGFConstants.alpha * w; // modulate by stability
    float2 prevM = t_MomentHistory[loc];
    
    float3 accumulatedRadiance = lerp(curRadiance, historyRadiance, alpha);
    float3 newM1 = lerp(curRadiance, prevM.x, alpha);
    float3 newM2 = lerp(curRadiance * curRadiance, prevM.y, alpha);
    float3 var = max(newM2 - newM1 * newM1, g_SVGFConstants.varianceEps);

    t_Radiance[loc] = newM1; // filtered radiance (history)
    t_Moment[loc] = float2(newM1.x, newM2.y);
    t_Variance[loc] = (var.r + var.g + var.b) / 3.0; // luminance
}