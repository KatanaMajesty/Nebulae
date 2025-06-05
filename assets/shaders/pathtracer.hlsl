#define ENABLE_NRC 1
#include "rtxgi/Nrc.hlsli"
#include "rtxgi/NrcStructures.h"
#include "octahedron_encoding.hlsli"
#include "rand.hlsli"
#include "brdf.hlsli"

#define TRACING_MAX_DISTANCE 1000.0f

struct GlobalConstants
{
    uint frameIndex;
    uint samplesPerPixel;

    float3 cameraWorldPos;
    float4x4 cameraViewToWorld;
    float4x4 cameraViewToClip;

    uint nrcMaxPathVertices;
    uint nrcTrainingDownscale;
};

ConstantBuffer<NrcConstants> g_NrcConstants : register(b0);
ConstantBuffer<GlobalConstants> g_Global : register(b1);

RWStructuredBuffer<NrcPackedQueryPathInfo> u_QueryPathInfo : register(u0, space0); // Misc path info (vertexCount, queryIndex)
RWStructuredBuffer<NrcPackedTrainingPathInfo> u_TrainingPathInfo : register(u1, space0); // Misc path info (vertexCount, queryIndex)
RWStructuredBuffer<NrcPackedPathVertex> u_TrainingPathVertices : register(u2, space0); // Path vertex data used to train the neural radiance cache
RWStructuredBuffer<NrcRadianceParams> u_QueryRadianceParams : register(u3, space0);
RWStructuredBuffer<uint> u_CountersData : register(u4, space0);

enum GbufferSlot
{
    Gbuffer_Albedo = 0,
    Gbuffer_Normal = 1,
    Gbuffer_RoughnessMetalness = 2,
    Gbuffer_WorldPos = 3,
    Gbuffer_NumSlots,
};

Texture2D t_GbufferTextures[Gbuffer_NumSlots] : register(t0, space1);
Texture2D t_SceneDepth : register(t0, space0);
Texture2D<uint> t_SceneStencil : register(t1, space0);
RaytracingAccelerationStructure SceneBVH : register(t2, space0);

// space 3 for bindless heap, essentially an entire SRV heap
Texture2D t_BindlessTextures[] : register(t0, space3);
ByteAddressBuffer t_BindlessBuffers[] : register(t0, space4);

// @see StaticMesh.h
struct InstanceData
{
    int geometryIndexOffset;
    float4x4 instanceToWorld;
};

// @see StaticMesh.h
enum GeometryAttributeType
{
    GeometryAttribute_Position,
    GeometryAttribute_Normal,
    GeometryAttribute_TexCoords,
    GeometryAttribute_Tangents,
    GeometryAttribute_NumTypes,
};

// @see StaticMesh.h
struct GeometryData
{
    int materialIndex;
    
    int indexBufferIndex;
    int indexBufferOffset; // in bytes
    int indexBufferStride; // either 2 of 4 for uint32 or uint16
    int attributeBufferIndices[GeometryAttribute_NumTypes];
    int attributeBufferOffsets[GeometryAttribute_NumTypes]; // in bytes
    int attributeBufferStrides[GeometryAttribute_NumTypes]; // in bytes
};

// @see Material.h
enum MaterialTextureType
{
    MaterialTexture_Albedo,
    MaterialTexture_Normal,
    MaterialTexture_RoughnessMetalness,
    MaterialTexture_NumTypes,
};

// @see Material.h
struct MaterialData
{
    int textureIndices[MaterialTexture_NumTypes];

    float4 albedo; // if no albedo texture available
    float2 roughnessMetalness; // if no roughness/metalness map available
};

StructuredBuffer<InstanceData> t_InstanceData : register(t1, space5);
StructuredBuffer<GeometryData> t_GeometryData : register(t2, space5);
StructuredBuffer<MaterialData> t_MaterialData : register(t3, space5);
SamplerState s_MaterialSampler : register(s0);


struct RayPayload
{
    float hitDistance;
    uint instanceID;
    uint primitiveIndex;
    uint geometryIndex;
    float2 barycentrics;

    bool IsHit()
    {
        return hitDistance > 0.0f;
    }

    bool IsFrontFacing()
    {
        return asuint(hitDistance) & 0x1;
    }
};

const bool IsGbufferMiss(in uint2 loc)
{
    return t_SceneStencil.Load(uint3(loc.x, loc.y, 0)) == 0;
}

RayDesc QueryReconstructedHemisphereRay(float3 worldPos, float3 SN, inout uint rng, out float pdf)
{
    // Set up the ray
    float3 wi = CosineSampleHemisphere(Rand2(rng), pdf);
    float3 Nwi = normalize(SN) * wi;

    RayDesc ray;
    ray.Origin = worldPos;
    ray.Direction = Nwi;
    ray.TMin = 0.0;
    ray.TMax = TRACING_MAX_DISTANCE;
    return ray;
}

struct SurfaceSample
{
    // Geometry sample data
    float3 worldPos;
    float3 GN; // .xy - geometry normal packed
    float3 SN; // .zw - surface normal packed
    
    // Material sample data
    float3 albedo;
    float roughness;
    float metalness;
};

struct BRDFData
{
    float roughness;
    float alpha;

    float3 V; // view vector
    float3 L; // light dir
    float3 N; // normal
    float3 H; // half-vector between V and L
    
    // dot products are not saturated (can be negative)
    float LdotN;
    float LdotH;
    float VdotH;
    float VdotN;
    float NdotH;

    float3 specularF0;
    float3 diffuseReflectance;
};

// Precalculates commonly used terms in BRDF evaluation
// dot-products are NOT clamped and thus, they can be used to determine whether vectors are backfacing towards the surface/each-other
BRDFData PrepareBRDFData(float3 N, float3 L, float3 V, in SurfaceSample surfaceSample, in float2 u)
{
    BRDFData data;
    data.roughness = surfaceSample.roughness;
    data.alpha = data.roughness * data.roughness;

    data.V = V;
    data.L = L;
    data.N = N;

    // Sample a half-vector of specular BRDF
    // this needs to be a random distribution as we want to randomly sample across different directions
    data.H = Ndf_HalfvectorSampleGGXVndf(V, float2(data.alpha, data.alpha), u);
    
    float3 H = data.H;
    data.LdotN = dot(L, N);
    data.LdotH = dot(L, H);
    data.VdotH = dot(V, H);
    data.VdotN = dot(V, N);
    data.NdotH = dot(N, H);

    // Unpack material properties
    data.specularF0 = Brdf_GetSpecularF0(surfaceSample.albedo, surfaceSample.metalness);
    data.diffuseReflectance = Brdf_GetDiffuseReflectance(surfaceSample.albedo, surfaceSample.metalness);
    return data;
}

bool EvaluateBRDF(in uint rng,
                  in SurfaceSample surfaceSample,
                  in float3 V,
                  out float3 rayDirection,
                  out float3 sampleWeight,
                  out float pdf,
                  out float specularProbability)
{
    float3 SN = surfaceSample.SN;
    float3 GN = surfaceSample.GN;

    // Ignore incident ray coming from "below" the hemisphere
    rayDirection = 0.0;
    sampleWeight = 0.0;
    pdf = 0.0f;
    specularProbability = 0.0;
    if (dot(SN, V) <= 0.0)
        return false;

    float3 L = normalize(CosineSampleHemisphere(Rand2(rng), pdf)); // randomly sample ray direction from the surface hemisphere
    BRDFData data = PrepareBRDFData(SN, L, V, surfaceSample, Rand2(rng));

    // Function 'diffuseTerm' is predivided by PDF of sampling the cosine weighted hemisphere
    // as we use lambertian BRDF, diffuse sample weight == diffuse component == diffuse reflectance factor
    sampleWeight = data.diffuseReflectance;

    // Clamp LdotH to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
    float VdotH = clamp(data.VdotH, 0.00001, 1.0);
    sampleWeight *= (1.0 - Brdf_FresnelSchlick(data.specularF0, VdotH));

    // Prevent tracing direction with no contribution
    if (Luminance(sampleWeight) == 0.0f)
        return false;

    // Prevent tracing direction "under" the hemisphere (behind the triangle)
    if (dot(GN, rayDirection) <= 0.0f)
        return false;

    specularProbability = (1.0 - Brdf_GetSpecularProbability(saturate(data.VdotN), data.specularF0, surfaceSample.albedo));
    rayDirection = L;
    return true;
}

static const int InvalidIndex = -1;

float2 InterpolateBary(float2 attributes[3], float3 bary)
{
    return attributes[0] * bary[0] + attributes[1] * bary[1] + attributes[2] * bary[2];
}

float3 InterpolateBary(float3 attributes[3], float3 bary)
{
    return attributes[0] * bary[0] + attributes[1] * bary[1] + attributes[2] * bary[2];
}

float4 InterpolateBary(float4 attributes[3], float3 bary)
{
    return attributes[0] * bary[0] + attributes[1] * bary[1] + attributes[2] * bary[2];
}

uint3 ReadUint32Indices(in ByteAddressBuffer buffer, in uint offsetInBytes)
{
    uint3 indices = buffer.Load3(offsetInBytes);
    return indices;
}

uint3 ReadUint16Indices(in ByteAddressBuffer buffer, in uint offsetInBytes)
{
    uint3 indices;
    uint packed1 = buffer.Load(offsetInBytes);      // reads bytes 0-3
    uint packed2 = buffer.Load(offsetInBytes + 4);  // reads bytes 4-7
    indices.x = (packed1) & 0xffff;         // u0
    indices.y = (packed1 >> 16) & 0xffff;   // u1
    indices.z = (packed2) & 0xffff;         // u2
    return indices;
}

bool ReconstructSurfaceData(uint instanceIndex,
    uint triangleIndex,
    uint geometryIndex,
    float2 rayBarycentrics,
    StructuredBuffer<InstanceData> instanceBuffer,
    StructuredBuffer<GeometryData> geometryBuffer,
    StructuredBuffer<MaterialData> materialBuffer,
    out SurfaceSample surfaceSample)
{
    InstanceData instance = instanceBuffer[instanceIndex];
    GeometryData geometry = geometryBuffer[instance.geometryIndexOffset + geometryIndex];

    float3 barycentrics;
    barycentrics.yz = rayBarycentrics;
    barycentrics.x = 1.0 - (barycentrics.y + barycentrics.z);
    
    if (geometry.indexBufferIndex == InvalidIndex ||
        geometry.attributeBufferIndices[GeometryAttribute_Position] == InvalidIndex ||
        geometry.attributeBufferIndices[GeometryAttribute_Normal] != InvalidIndex ||
        geometry.attributeBufferIndices[GeometryAttribute_TexCoords] != InvalidIndex || 
        geometry.attributeBufferIndices[GeometryAttribute_Tangents] != InvalidIndex)
        return false;

    ByteAddressBuffer ib = t_BindlessBuffers[NonUniformResourceIndex(geometry.indexBufferIndex)];
    uint3 indices = (geometry.indexBufferStride == 2) ? 
        ReadUint16Indices(ib, geometry.indexBufferIndex + triangleIndex * geometry.indexBufferStride) : 
        ReadUint32Indices(ib, geometry.indexBufferIndex + triangleIndex * geometry.indexBufferStride);

    ByteAddressBuffer positionBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.attributeBufferIndices[GeometryAttribute_Position])];
    float3 rawPositions[3];
    rawPositions[0] = asfloat(positionBuffer.Load3(geometry.attributeBufferOffsets[GeometryAttribute_Position] + indices[0] * geometry.attributeBufferStrides[GeometryAttribute_Position]));
    rawPositions[1] = asfloat(positionBuffer.Load3(geometry.attributeBufferOffsets[GeometryAttribute_Position] + indices[1] * geometry.attributeBufferStrides[GeometryAttribute_Position]));
    rawPositions[2] = asfloat(positionBuffer.Load3(geometry.attributeBufferOffsets[GeometryAttribute_Position] + indices[2] * geometry.attributeBufferStrides[GeometryAttribute_Position]));
    float3 vertexPosition = InterpolateBary(rawPositions, barycentrics);

    // Potentially normals could be stored as a uint32_t (or 24 bytes only, 8 byte per float, RGB8_SNORM format)
    // But specifically in Nebulae this is not done, so just unpack them as you normally would
    ByteAddressBuffer normalBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.attributeBufferIndices[GeometryAttribute_Normal])];
    float3 rawGeometryNormals[3];
    rawGeometryNormals[0] = asfloat(normalBuffer.Load3(geometry.attributeBufferOffsets[GeometryAttribute_Normal] + indices[0] * geometry.attributeBufferStrides[GeometryAttribute_Normal]));
    rawGeometryNormals[1] = asfloat(normalBuffer.Load3(geometry.attributeBufferOffsets[GeometryAttribute_Normal] + indices[1] * geometry.attributeBufferStrides[GeometryAttribute_Normal]));
    rawGeometryNormals[2] = asfloat(normalBuffer.Load3(geometry.attributeBufferOffsets[GeometryAttribute_Normal] + indices[2] * geometry.attributeBufferStrides[GeometryAttribute_Normal]));
    surfaceSample.GN = InterpolateBary(rawGeometryNormals, barycentrics);
    surfaceSample.GN = normalize(mul(float4(surfaceSample.GN, 0.0), instance.instanceToWorld).xyz);

    ByteAddressBuffer uvBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.attributeBufferIndices[GeometryAttribute_TexCoords])];
    float2 rawUvs[3];
    rawUvs[0] = asfloat(uvBuffer.Load2(geometry.attributeBufferOffsets[GeometryAttribute_TexCoords] + indices[0] * geometry.attributeBufferStrides[GeometryAttribute_TexCoords]));
    rawUvs[1] = asfloat(uvBuffer.Load2(geometry.attributeBufferOffsets[GeometryAttribute_TexCoords] + indices[1] * geometry.attributeBufferStrides[GeometryAttribute_TexCoords]));
    rawUvs[2] = asfloat(uvBuffer.Load2(geometry.attributeBufferOffsets[GeometryAttribute_TexCoords] + indices[2] * geometry.attributeBufferStrides[GeometryAttribute_TexCoords]));
    float2 uv = InterpolateBary(rawUvs, barycentrics);

    ByteAddressBuffer tangentBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.attributeBufferIndices[GeometryAttribute_Tangents])];
    float4 rawTangents[3]; // float4 as last element is a length of bitangent
    rawTangents[0] = asfloat(tangentBuffer.Load4(geometry.attributeBufferOffsets[GeometryAttribute_Tangents] + indices[0] * geometry.attributeBufferStrides[GeometryAttribute_Tangents]));
    rawTangents[1] = asfloat(tangentBuffer.Load4(geometry.attributeBufferOffsets[GeometryAttribute_Tangents] + indices[1] * geometry.attributeBufferStrides[GeometryAttribute_Tangents]));
    rawTangents[2] = asfloat(tangentBuffer.Load4(geometry.attributeBufferOffsets[GeometryAttribute_Tangents] + indices[2] * geometry.attributeBufferStrides[GeometryAttribute_Tangents]));
    float4 tangent = normalize(InterpolateBary(rawTangents, barycentrics));
    float3 bitangent = normalize(cross(surfaceSample.GN, tangent.xyz) * tangent.w);
    
    if (geometry.materialIndex == InvalidIndex)
        return false; // No material, dont do any further calculations, maybe in future support such no-material meshes

    MaterialData material = materialBuffer[geometry.materialIndex];
    if (material.textureIndices[MaterialTexture_Albedo] == InvalidIndex)
    {
        surfaceSample.albedo = material.albedo.rgb;
    }
    else
    {
        surfaceSample.albedo = t_BindlessTextures[material.textureIndices[MaterialTexture_Albedo]].SampleLevel(s_MaterialSampler, uv, 0).rgb;
    }
    
    if (material.textureIndices[MaterialTexture_Normal] == InvalidIndex)
        surfaceSample.SN = surfaceSample.GN; // if no normal map then surface normal == geometry normal
    else
    {
        Texture2D normalMap = t_BindlessTextures[material.textureIndices[MaterialTexture_Normal]];
        float3x3 TBN = float3x3(tangent.xyz, bitangent, surfaceSample.GN);
        float3 N = normalMap.SampleLevel(s_MaterialSampler, uv, 0).xyz * 2.0 - 1.0;
        surfaceSample.SN = normalize(mul(N, TBN));
    }

    if (material.textureIndices[MaterialTexture_RoughnessMetalness] == InvalidIndex)
    {
        surfaceSample.roughness = material.roughnessMetalness.r;
        surfaceSample.metalness = material.roughnessMetalness.g;
    }
    else
    {
        // in Nebulae roughness-metalness textures are following glTF spec and are RGBA, where B for metalness, G for roughness (R, A channels ignored)
        Texture2D roughnessMetalnessMap = t_BindlessTextures[material.textureIndices[MaterialTexture_RoughnessMetalness]];
        float2 RM = roughnessMetalnessMap.SampleLevel(s_MaterialSampler, uv, 0).gb;
        surfaceSample.roughness = RM.r;
        surfaceSample.metalness = RM.g;
    }
    return true;
}

[shader("raygeneration")]
void PathtracerRG()
{
    const uint2 loc = DispatchRaysIndex().xy;
    uint rng = InitRNG(loc, DispatchRaysDimensions().xy, g_Global.frameIndex);

    float2 pixel = float2(loc);
    float2 dimensions = float2(DispatchRaysDimensions().xy);

    // Context + per-frame buffers
    NrcBuffers nrcBufs;
    nrcBufs.queryPathInfo = u_QueryPathInfo;
    nrcBufs.trainingPathInfo = u_TrainingPathInfo;
    nrcBufs.trainingPathVertices = u_TrainingPathVertices;
    nrcBufs.queryRadianceParams = u_QueryRadianceParams;
    nrcBufs.countersData = u_CountersData;
    NrcContext ctx = NrcCreateContext(g_NrcConstants, nrcBufs, loc);
    
    float3 albedo = t_GbufferTextures[Gbuffer_Albedo].Load(float3(loc, 0)).xyz;
    float3 worldPos = t_GbufferTextures[Gbuffer_WorldPos].Load(float3(loc, 0)).xyz;
    float4 encodedN = t_GbufferTextures[Gbuffer_Normal].Load(float3(loc, 0));
    float3 GN, SN;
    UnpackOct16Normals(encodedN, GN, SN);
    float2 roughnessMetalness = t_GbufferTextures[Gbuffer_RoughnessMetalness].Load(float3(loc, 0)).xy;
    float roughness = roughnessMetalness.x;
    float metalness = roughnessMetalness.y;

    float3 V = normalize(g_Global.cameraWorldPos - worldPos);

    const uint samplesPerPixel = NrcIsUpdateMode() ? 1 : g_NrcConstants.samplesPerPixel;
    for (int sampleIndex = 0; sampleIndex < samplesPerPixel; ++sampleIndex)
    {
        // Initialize NRC data for path and sample index traced in this thread
        NrcSetSampleIndex(ctx, sampleIndex);
        NrcPathState nrcPathState = NrcCreatePathState(g_NrcConstants, Rand(rng));

        // offset pixel a bit to have some jitter during learning
        pixel = NrcIsUpdateMode() ? (pixel + Rand2(rng)) * g_Global.nrcTrainingDownscale : pixel;

        // early bail if gbuffer miss
        if (IsGbufferMiss(loc))
        {
            NrcUpdateOnMiss(nrcPathState);
            break;
        }

        // The current Monte-Carlo weight carried by the path when it arrives at this vertex
        // starts at (1, 1, 1). After each bounce multiplied by the BRDF-factor for that bounce.
        float3 throughput = float3(1.0, 1.0, 1.0);

        // The energy already accumulated by the path before reaching the N vertex. 
        // starts at (0, 0, 0). Represents whats been added to the image so far (e.g. emissive hit, environment miss, direct-light NEE from earlier bounces).
        float3 radiance = float3(0.0, 0.0, 0.0);

        // Flip normals towards the incident ray direction (needed for backfacing triangles)
        NrcSurfaceAttributes surfaceAttributes;
        surfaceAttributes.encodedPosition = NrcEncodePosition(worldPos, g_NrcConstants); // Use NrcEncodePosition
        surfaceAttributes.roughness = roughness;
        surfaceAttributes.specularF0 = Brdf_GetSpecularF0(albedo, metalness);
        surfaceAttributes.diffuseReflectance = Brdf_GetDiffuseReflectance(albedo, metalness); // pd term of diffuse BRDF term (albedo factor)
        surfaceAttributes.shadingNormal = SN;
        surfaceAttributes.viewVector = V;
        surfaceAttributes.isDeltaLobe = (metalness == 1.0f && roughness == 0.0f);
        NrcProgressState nrcProgressState = NrcUpdateOnHit(ctx, nrcPathState, surfaceAttributes, 0.0, 0, throughput, radiance); // Update NRC state on hit
        if (nrcProgressState == NrcProgressState::TerminateImmediately)
        {
            NrcWriteFinalPathInfo(ctx, nrcPathState, throughput, 0);
            break;
        }

        // Define a ray, where the origin is currently reconstructed Gbuffer world pos and the direction is a randomly selected hemisphere direction wi
        float pdf;
        RayDesc ray = QueryReconstructedHemisphereRay(worldPos, SN, rng, pdf);
        NrcSetBrdfPdf(nrcPathState, pdf);

        throughput *= surfaceAttributes.diffuseReflectance * PI; // as pdf = cos / INV_PI -> INV_PI = pdf / cos -> PI = cos / pdf

        // Prepare Payload and other data...
        RayPayload payload;
        payload.hitDistance = -1.0f;
        payload.instanceID = ~0U;
        payload.primitiveIndex = ~0U;
        payload.geometryIndex = ~0U;
        payload.barycentrics = 0;

        for (int bounce = 1; bounce < g_Global.nrcMaxPathVertices; ++bounce)
        {
            TraceRay(SceneBVH, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xff,
                0, // RayContributionToHitGroupIndex
                0, // MultiplierForGeometryContributionToHitGroupIndex
                0, // MissShaderIndex);
                ray,
                payload);

            if (!payload.IsHit())
                NrcUpdateOnMiss(nrcPathState); // Handle miss

            float hitT = payload.hitDistance;
            float3 hitP = ray.Origin + ray.Direction * payload.hitDistance;
            float3 rayV = normalize(-ray.Direction);

            // Decode material properties...  
            SurfaceSample surfaceSample;
            if (!ReconstructSurfaceData(payload.instanceID, payload.geometryIndex, payload.primitiveIndex, payload.barycentrics, t_InstanceData, t_GeometryData, t_MaterialData, surfaceSample))
            {
                NrcSetDebugPathTerminationReason(nrcPathState, NrcDebugPathTerminationReason::Unset);
                break;
            }

            NrcSurfaceAttributes sampledSurfaceAttributes; // Passed to NrcUpdateOnHit
            sampledSurfaceAttributes.encodedPosition = NrcEncodePosition(hitP, g_NrcConstants); // Use NrcEncodePosition
            sampledSurfaceAttributes.roughness = surfaceSample.roughness;
            sampledSurfaceAttributes.specularF0 = Brdf_GetSpecularF0(surfaceSample.albedo, surfaceSample.metalness);
            sampledSurfaceAttributes.diffuseReflectance = Brdf_GetDiffuseReflectance(surfaceSample.albedo, surfaceSample.metalness); // pd term of diffuse BRDF term (albedo factor)
            sampledSurfaceAttributes.shadingNormal = surfaceSample.SN;
            sampledSurfaceAttributes.viewVector = rayV;
            sampledSurfaceAttributes.isDeltaLobe = (surfaceSample.metalness == 1.0f && surfaceSample.roughness == 0.0f);
         
            NrcProgressState nrcProgressState = NrcUpdateOnHit(ctx, nrcPathState, sampledSurfaceAttributes, hitT, bounce, throughput, radiance); // Update NRC state on hit
            if (nrcProgressState == NrcProgressState::TerminateImmediately)
                break;

            // Account for emissives and evaluate NEE with RIS...

            // Terminate loop early on last bounce (don't sample BRDF)
            if (bounce == g_Global.nrcMaxPathVertices - 1)
            {
                NrcSetDebugPathTerminationReason(nrcPathState, NrcDebugPathTerminationReason::MaxPathVertices);
                break;
            }

            // Terminate loop after emissives and direct light if CreateQuery requests delayed termination. 
            // If direct lighting isn't cached (radianceCacheDirect is false)
            // add direct lighting on hit where we query NRC before terminating the loop.
            if (nrcProgressState == NrcProgressState::TerminateAfterDirectLighting)
                break;
            
            // Here NRC russian roulette if available
            // if (g_Global.enableRussianRoulette && NrcCanUseRussianRoulette(nrcPathState) && (bounce > 3 /*BOUNCE_MIN*/)) {}

            // Sample BRDF to generate the next ray and run MIS
            float3 rayDirection;
            float3 sampleWeight;
            float pdf;
            float specularProbability;
            if (!EvaluateBRDF(rng, surfaceSample, rayV, rayDirection, sampleWeight, pdf, specularProbability))
                NrcSetDebugPathTerminationReason(nrcPathState, NrcDebugPathTerminationReason::BRDFAbsorption);

            // always sample DIFFUSE BRDF! Here we only do diffuse!
            throughput /= specularProbability;
            NrcSetBrdfPdf(nrcPathState, pdf);
        } // End of path

        NrcWriteFinalPathInfo(ctx, nrcPathState, throughput, radiance);
    }
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.hitDistance = -1.0f;
}

struct Attributes
{
    float2 uv;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.hitDistance = RayTCurrent();
    payload.instanceID = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.geometryIndex = GeometryIndex();
    payload.barycentrics = attrib.uv;

    // ????
    uint packedDistance = asuint(payload.hitDistance) & (~0x1u);
    packedDistance |= HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE ? 0x1 : 0x0;

    payload.hitDistance = asfloat(packedDistance);
}