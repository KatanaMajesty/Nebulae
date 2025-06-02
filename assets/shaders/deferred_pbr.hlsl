#include "brdf.hlsli"
#include "octahedron_encoding.hlsli"
#include "fullscreen_triangle.hlsli"

struct ViewData
{
    matrix viewInv;
    matrix projInv;
};

struct LightEnvironment
{
    float intensity;
    float3 direction;
    float3 radiance;
};

ConstantBuffer<ViewData> cbViewData : register(b0);
ConstantBuffer<LightEnvironment> cbLightEnvironment : register(b1);

#define GBUFFER_SLOT_ALBEDO 0
#define GBUFFER_SLOT_NORMAL 1
#define GBUFFER_SLOT_ROUGHNESS_METALNESS 2
#define GBUFFER_SLOT_WORLD_POS 3
#define GBUFFER_SLOT_NUM_SLOTS 4
Texture2D GbufferTextures[GBUFFER_SLOT_NUM_SLOTS] : register(t0, space1);
Texture2D SceneDepth : register(t0, space0);
RWTexture2D<float4> HDROutput : register(u0);

//float3 WorldPosFromDepth(in matrix viewProjInv, in float2 uv, in float depth)
//{
//    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
//    ndc.y = -ndc.y; // Inverse .y as we are in HLSL
//    
//    float4 world = mul(ndc, viewProjInv);
//    float3 worldPos = world.xyz / world.w;
//    return worldPos;
//}

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    // Frustum calculation for texel size
    matrix viewProjInv = mul(cbViewData.projInv, cbViewData.viewInv);
    // integer pixel coords   (x,y,mip)
    int3 p = int3(tid.xy, 0);

    // float sceneDepth = SceneDepth.Load(p).r;
    float3 worldPos = GbufferTextures[GBUFFER_SLOT_WORLD_POS].Load(p).xyz;
    float3 eye = cbViewData.viewInv[3].xyz;
    
    float3 albedo = GbufferTextures[GBUFFER_SLOT_ALBEDO].Load(p).rgb;
    float2 roughnessMetalness = GbufferTextures[GBUFFER_SLOT_ROUGHNESS_METALNESS].Load(p).rg;
    
    // As of 16.02.2024 -> .xy - geometry normal packed, .zw - surface normal packed
    float4 normals = GbufferTextures[GBUFFER_SLOT_NORMAL].Load(p);
    float3 GN = Oct16_FastUnpack(normals.xy);
    float3 SN = Oct16_FastUnpack(normals.zw);
    float3 V = normalize(eye - worldPos);
    float VdotN = clamp(dot(V, SN), 0.00001, 1.0);
    
    // Lighting
    // Single Directional Light (for RT Demo)
    float3 L = normalize(-cbLightEnvironment.direction);
    float3 H = normalize(L + V);
    float LdotN = clamp(dot(L, SN), 0.00001, 1.0);
    float VdotH = clamp(dot(V, H), 0.00001, 1.0);
    float NdotH = clamp(dot(SN, H), 0.00001, 1.0);
        
    // To handle both metals and non metals, we consider a purely non metallic surface to have base reflectivity of (0.04, 0.04, 0.04) 
    // and lerp between this value of base reflectivity (F0) based on the metallic factor of the surface.
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, roughnessMetalness.g);
    float3 F = Brdf_FresnelSchlick(F0, VdotH);
    float3 Kd = 1.0 - F; // We assume F to represent Ks -> Kd = 1.0 - Ks
    float3 O = Kd * Brdf_Diffuse_Lambertian(albedo) + Brdf_Specular_CookTorrance(F, roughnessMetalness.r, VdotN, LdotN, VdotH, NdotH);
        
    float3 Lo = O * LdotN * cbLightEnvironment.radiance * cbLightEnvironment.intensity;
    
    const float3 ambient = float3(0.2, 0.2, 0.28) * albedo;
    HDROutput[tid.xy] = float4(ambient + Lo, 1.0); // TODO: Add support of alpha from albedo gbuffer (??)
}