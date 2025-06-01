#include "fullscreen_triangle.hlsli"

VSOutput VSMain(uint vid : SV_VertexID)
{
    VSOutput output;
    output.pos = float4(FullscreenTriangle[vid].xy, 0.0, 1.0);
    output.uv = FullscreenTriangle[vid].zw;
    return output;
}
