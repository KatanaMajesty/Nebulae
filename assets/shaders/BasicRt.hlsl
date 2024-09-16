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

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

// specify shader type as an entrypoint attribute
[shader("raygeneration")]
void RayGen()
{
    // Initialize the ray payload
    HitInfo payload;
    payload.ColorAndDistance = float4(0.9, 0.6, 0.2, 1);

    // Get the location within the dispatched 2D grid of work items
    // (often maps to pixels, so this could represent a pixel coordinate).
    uint2 launchIndex = DispatchRaysIndex().xy;

    gOutput[launchIndex] = float4(payload.ColorAndDistance.rgb, 1.f);
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attributes)
{
    payload.ColorAndDistance = float4(1.0, 1.0, 0.0, RayTCurrent());
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    payload.ColorAndDistance = float4(0.2f, 0.2f, 0.8f, -1.f);
}