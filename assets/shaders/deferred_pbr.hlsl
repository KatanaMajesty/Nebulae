#include "brdf.hlsli"
#include "octahedron_encoding.hlsli"
#include "fullscreen_triangle.hlsli"
#include "sun_disk_sampling.hlsli"

struct ViewData
{
    matrix viewInv;
    matrix projInv;
};

struct LightEnvironment
{
    float3 direction;
    float tanHalfAngle; // tan(0.265) = 4.63e-3
    float3 radiance; // linear RGB, W * sr^-1 * m^-2
    uint frameIndex;
};

//struct SunLight // 64 bytes, fits a CBV
//{
//    float3 radiance; 
//    float tanHalfAngle; 
//
//    float3 direction; // unit vector, world space (FROM pixel TO sun)
//    uint frameIndex; // for scrambling the sample pattern
//};

ConstantBuffer<ViewData> cbViewData : register(b0);
ConstantBuffer<LightEnvironment> cbLightEnvironment : register(b1);

#define GBUFFER_SLOT_ALBEDO 0
#define GBUFFER_SLOT_NORMAL 1
#define GBUFFER_SLOT_ROUGHNESS_METALNESS 2
#define GBUFFER_SLOT_WORLD_POS 3
#define GBUFFER_SLOT_NUM_SLOTS 4
Texture2D GbufferTextures[GBUFFER_SLOT_NUM_SLOTS] : register(t0, space1);
Texture2D SceneDepth : register(t0, space0);
RaytracingAccelerationStructure SceneTLAS : register(t1, space0);
RWTexture2D<float4> HDROutput : register(u0);

#define NUM_RAY_QUERY_SAMPLES 4    // TODO: tweak with denoiser

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
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
    

    // Inline shadow ray queries
    float3 visibility = 1.0;
    {
        float3 B = normalize(GetPerpendicularVector(L));
        float3 T = cross(B, L);

        uint rngState = InitRNG(tid.xy, gid.xy, cbLightEnvironment.frameIndex);
        float2 rand2 = float2(Rand(rngState), Rand(rngState));
        
        float angle = rand2.x * 2.0f * PI;
        float distance = sqrt(rand2.y);

        float3 incidentVector;
        incidentVector = L + (B * sin(angle) + T * cos(angle)) * cbLightEnvironment.tanHalfAngle * distance;
        incidentVector = normalize(incidentVector);

        RayDesc ray;
        //ray.Origin = worldPos; 
        //ray.Origin = OffsetRay(worldPos, SN); // escape the surface
        ray.Origin = worldPos + SN * 1e-2; // escape the surface
        ray.Direction = incidentVector;
        ray.TMin = 0.0;
        ray.TMax = FLT_MAX;

        // Inline 'depth' query with fastest flags
        // Instantiate ray query object.
        // Template parameter allows driver to generate a specialized implementation.
        // https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#ray-flags
        RayQuery < RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER > rq;
        rq.TraceRayInline(SceneTLAS, 0, 0xFF, ray);
        while (rq.Proceed()) // the DXR spec says you must loop until it returns false.
        {
        } // heaviest part of ray querying

        visibility = rq.CommittedStatus() != COMMITTED_NOTHING ? 0.0 : 1.0;
    }
    

    //float visibility = 0.0;
    //[loop]
    //for (uint i = 0; i < NUM_RAY_QUERY_SAMPLES; ++i)
    //{
    //    float2 xi = Hammersley(i, NUM_RAY_QUERY_SAMPLES);
    //    float2 d = ConcentricSampleDisk(xi) * cbLightEnvironment.tanHalfAngle;
    //    float3 dir = normalize(L + d.x * T + d.y * B);
    //
    //    
    //
    //    
    //    visibility += !occluded;
    //}
    //visibility /= NUM_RAY_QUERY_SAMPLES;

    float3 Lo = O * LdotN * cbLightEnvironment.radiance * visibility;
    
    const float3 ambient = float3(0.2, 0.2, 0.28) * albedo;
    //const float3 ambient = float3(0.0, 0.0, 0.0);
    HDROutput[tid.xy] = float4(ambient + Lo, 1.0); // TODO: Add support of alpha from albedo gbuffer (??)
}