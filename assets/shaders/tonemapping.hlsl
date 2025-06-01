#include "fullscreen_triangle.hlsli"

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

// sRGB -> XYZ -> D65_2_D60 -> AP1 -> RRT_SAT
static const float3x3 ACESInputMat =
{
    { 0.59719, 0.35458, 0.04823 },
    { 0.07600, 0.90834, 0.01566 },
    { 0.02840, 0.13383, 0.83777 }
};

// ODT_SAT -> XYZ -> D60_2_D65 -> sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367 },
    { -0.10208, 1.10813, -0.00605 },
    { -0.00327, -0.07276, 1.07602 }
};

//https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
float3 ACESFitted(float3 color)
{
    //color *= 1.0 / 6.0;
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);
    return color;
}

Texture2D HDRImage : register(t0);
SamplerState StaticSampler : register(s0);

float4 PSMain(VSOutput input) : SV_Target0
{
    float3 color = HDRImage.Sample(StaticSampler, input.uv).rgb;
    //color *= computeExposure(g_ev100); // not used in Nebulae so far
    color = ACESFitted(color);
    
    // If FXAA, still apply each time
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return float4(color, luma);
}