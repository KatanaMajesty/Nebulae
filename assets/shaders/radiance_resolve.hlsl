#include "rtxgi/Nrc.hlsli"
#include "rtxgi/NrcHelpers.hlsli"

struct ScreenConstants
{
    uint2 resolution;
};

ConstantBuffer<NrcConstants> g_NrcConstants : register(b0, space0);
ConstantBuffer<ScreenConstants> g_Screen : register(b1, space0);

StructuredBuffer<NrcPackedQueryPathInfo> t_QueryPathInfo : register(t0, space0); // Misc path info (vertexCount, queryIndex)
StructuredBuffer<float3> t_QueryRadiance : register(t1, space0);

RWTexture2D<float4> u_HDROutput : register(u0, space1);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    const uint2 launchIndex = tid.xy;
    if (any(launchIndex >= g_Screen.resolution)) 
        return;

    const uint sampleIndex = 0;
    const uint samplesPerPixel = 1;
    const uint pathIndex = NrcCalculateQueryPathIndex(g_Screen.resolution, launchIndex, sampleIndex, samplesPerPixel);
    const NrcQueryPathInfo path = NrcUnpackQueryPathInfo(t_QueryPathInfo[pathIndex]);

    if (path.queryBufferIndex < 0xFFFFFFFF)
    {
        float3 radiance = NrcUnpackQueryRadiance(g_NrcConstants, t_QueryRadiance[path.queryBufferIndex]) * path.prefixThroughput;
        u_HDROutput[launchIndex] += float4(radiance, 0.0);
    }
}