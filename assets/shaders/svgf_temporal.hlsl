#include "svgf_common.hlsli"
#include "octahedron_encoding.hlsli"

struct SVGFTemporalConstants
{
    uint2 resolution;
    float depthSigma;
    float alpha; // tweakable stability var
    float varianceEps; // small value
};
ConstantBuffer<SVGFTemporalConstants> g_SVGFConstants : register(b0, space0);

Texture2D<float3> t_RadianceHistory     : register(t0, space0);
Texture2D<float> t_Depth                : register(t1, space0);
Texture2D<float> t_DepthHistory         : register(t2, space0);
Texture2D<float4> t_Normal              : register(t3, space0);
Texture2D<float4> t_NormalHistory       : register(t4, space0);
Texture2D<float2> t_MomentHistory       : register(t5, space0);

RWTexture2D<float3> t_Radiance : register(u0, space0);
RWTexture2D<float2> t_Moment : register(u1, space0);
RWTexture2D<float> t_Variance : register(u2, space0);

// From brdf.hlsli
//
// converts an RGB triplet to a single 'brightness' number that approximates how the human eye weights red, green and blue light.
// Ref: ITU-R BT.709-6, Section 3: Signal Format, 'Derivation of luminance signal'
float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

[numthreads(8, 8, 1)]
void CSMain_SVGF_Temporal(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_SVGFConstants.resolution.x || tid.y >= g_SVGFConstants.resolution.y)
        return;

    uint2 loc = tid.xy;

    float3 Ccurr = t_Radiance[loc];
    float  Dcurr = t_Depth[loc];

    float4 EncodedN = t_Normal[loc];
    float3 Ncurr = Oct16_FastUnpack(EncodedN.xy);

    float3 Chist = t_RadianceHistory[loc];
    float2 Mhist = t_MomentHistory[loc];
    float  Dhist = t_DepthHistory[loc];
    float4 EncodedNhist = t_NormalHistory[loc];
    float3 Nhist = Oct16_FastUnpack(EncodedNhist.xy);

    // stability norm
    float wDepth = SVGF_DWeight(Dcurr, Dhist, g_SVGFConstants.depthSigma);
    float wNormal = SVGF_NWeight(Ncurr, Nhist);
    float w = wDepth * wNormal;

    // lerp parameter:  Alpha_small -> faster reset, less ghosting
    //                  Alpha_large -> longer history, smoother noise
    float alpha = lerp(1.0, g_SVGFConstants.alpha, w); // 1 -> reset, a -> blend
    //float alpha = g_SVGFConstants.alpha * w; // modulate by stability
    //float2 prevM = t_MomentHistory[loc];

    float3 Caccum = lerp(Ccurr, Chist, alpha);

    float Ycurr = Luminance(Ccurr);
    float Yhist = Mhist.x; // <Y>
    float Y2hist = Mhist.y; // <Y^2>

    float Yaccum = lerp(Ycurr, Yhist, alpha);
    float Y2accum = lerp(Ycurr * Ycurr, Y2hist, alpha);
    float var = max(Y2accum - Yaccum * Yaccum, g_SVGFConstants.varianceEps);

    t_Radiance[loc] = Caccum; // filtered radiance (history)
    t_Moment[loc] = float2(Yaccum, Y2accum);
    t_Variance[loc] = var;
}