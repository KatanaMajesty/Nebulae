// FROM WARPENGINE
#include "brdf.hlsli"

float4 _PSMain(VSOutput input) : SV_Target0
{
    // Frustum calculation for texel size
    matrix viewProjInv = mul(CbViewData.ProjInv, CbViewData.ViewInv);
    
    float sceneDepth = SceneDepth.SampleLevel(StaticSampler, input.Uv, 0.0);
    float3 worldPos = WorldPosFromDepth(viewProjInv, input.Uv, sceneDepth);
    float3 eye = CbViewData.ViewInv[3].xyz;
    
    float3 albedo = GbufferAlbedo.Sample(StaticSampler, input.Uv).rgb;
    float2 roughnessMetalness = GbufferRoughnessMetalness.Sample(StaticSampler, input.Uv);
    
    // As of 16.02.2024 -> .xy - geometry normal packed, .zw - surface normal packed
    float4 normals = GbufferNormal.Sample(StaticSampler, input.Uv);
    float3 GN = Oct16_FastUnpack(normals.xy);
    float3 SN = Oct16_FastUnpack(normals.zw);
    float3 V = normalize(eye - worldPos);
    float VdotN = clamp(dot(V, SN), 0.00001, 1.0);
    
    // Lighting
    // Single Directional Light (for RT Demo)
    float3 L = normalize(-light.Direction);
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
        
        // Offset in uv space only
    matrix lightMatrix = mul(light.LightView, light.LightProj);
    float4 lightpos = mul(float4(worldPos + NOffset, 1.0), lightMatrix);
        
    float3 projCoords = lightpos.xyz / lightpos.w;
    float2 uv = float2(projCoords.x * 0.5 + 0.5, projCoords.y * -0.5 + 0.5);
        
    float3 Lo = O * LdotN * light.Radiance * light.Intensity;
    
    const float3 ambient = float3(0.2, 0.2, 0.28) * albedo;
    return float4(ambient + Lo, 1.0); // TODO: Add support of alpha from albedo gbuffer (??)
}