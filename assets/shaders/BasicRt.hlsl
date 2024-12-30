// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
    float4 ColorAndDistance;
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
    float2 Bary;
};

struct ViewInfo
{
    // Requires 256 alignment
    float4x4 ViewProjInverse;
    float4 CameraWorldPos;
};

struct WorldInfo
{
    // x,y,z - dir, w - 1D intensity
    float4 dirLightDirectionAndIntensity;
    float3 dirLightPosition; // just to play around
};

// Global descriptors
ConstantBuffer<ViewInfo> cbViewInfo : register(b0);
ConstantBuffer<WorldInfo> cbWorldInfo : register(b1);
RaytracingAccelerationStructure gSceneBVH : register(t0);

// Raygen Local descriptors
// Raytracing output texture, accessed as a UAV
// Raytracing acceleration structure, accessed as a SRV
RWTexture2D<float4> gOutput : register(u0);

// specify shader type as an entrypoint attribute
[shader("raygeneration")]
void RayGen()
{
    // Initialize the ray payload
    HitInfo payload;
    payload.ColorAndDistance = float4(0.2, 0.6, 0.2, 1);

    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float2 d = (float2(launchIndex + 0.5f) / dims) * float2(2.0, -2.0) + float2(-1.0, 1.0);

    // How to go from device coordinates back to worldspace
    // https://feepingcreature.github.io/math.html 
    float4 pNearNDC = float4(d.x, d.y, 0, 1); // z near = 0 (or maybe -1?)
    float4 pFarNDC = float4(d.x, d.y, 1, 1); // z far = 1
 
    float4 pNearH = mul(pNearNDC, cbViewInfo.ViewProjInverse);
    float4 pFarH = mul(pFarNDC, cbViewInfo.ViewProjInverse);
 
    float3 p0 = pNearH.xyz / pNearH.w;
    float3 p1 = pFarH.xyz / pFarH.w;

    float3 rayDir = normalize(p1 - p0);

    // Define a ray, consisting of origin, direction, and the min-max distance values
    RayDesc ray;
    ray.Origin = cbViewInfo.CameraWorldPos.xyz;
    ray.Direction = rayDir;
    ray.TMin = 0;
    ray.TMax = 100;

    TraceRay(gSceneBVH, RAY_FLAG_NONE, 0xff,
        0, // RayContributionToHitGroupIndex
        0, // MultiplierForGeometryContributionToHitGroupIndex
        0, // MissShaderIndex
        ray,
        payload);

    gOutput[launchIndex] = float4(payload.ColorAndDistance.rgb, 1.f);
}

// Shadow rays are cast by basic ClosestHit
// thus forward-declare
struct ShadowHitPayload
{
    bool isShadowRay;
};

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attributes)
{
    uint triangleColorPalette = 1024;
    float instanceId = lerp(0.1, 0.4, float(InstanceIndex() % 4) / 4);
    float id = normalize(float3(float(PrimitiveIndex() % triangleColorPalette), 0, triangleColorPalette)).r;
    payload.ColorAndDistance = float4(float3(saturate(id * (0.8 - instanceId)), saturate(id * min(instanceId, 0.2)), saturate(id * (0.4 - instanceId))), RayTCurrent());

    float3 direction = normalize(cbWorldInfo.dirLightDirectionAndIntensity.xyz);
    float3 origin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection() + (0.01f * direction);

    // Cast a shadow ray. The direction is hardcoded currently 
    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = direction;
    shadowRay.TMin = 0.01;
    shadowRay.TMax = 100;

    ShadowHitPayload shadowPayload;
    shadowPayload.isShadowRay = false;

    // Trace the ray
    TraceRay(gSceneBVH, RAY_FLAG_NONE, 0xFF,
        1, // RayContributionToHitGroupIndex
        0, // MultiplierForGeometryContributionToHitGroupIndex
        1, // MissShaderIndex
        shadowRay,
        shadowPayload);

    float factor = shadowPayload.isShadowRay ? 0.1 : 1.0;
    payload.ColorAndDistance *= factor;
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    payload.ColorAndDistance = float4(0.2f, 0.2f, 0.3f, -1.f);
}

[shader("closesthit")]
void ShadowHit(inout ShadowHitPayload payload, Attributes attributes)
{
    payload.isShadowRay = true;
}

[shader("miss")]
void ShadowMiss(inout ShadowHitPayload payload : SV_RayPayload)
{
    payload.isShadowRay = false;
}