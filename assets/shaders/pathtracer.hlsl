#define ENABLE_NRC 1
#include "rtxgi/Nrc.hlsli"
#include "rtxgi/NrcStructures.h"
#include "octahedron_encoding.hlsli"
#include "rand.hlsli"
#include "brdf.hlsli"
#include "sun_disk_sampling.hlsli"

#define TRACING_MAX_DISTANCE 10000.0f

struct GlobalConstants
{
    // https://maraneshi.github.io/HLSL-ConstantBufferLayoutVisualizer/
    uint frameIndex;
    uint samplesPerPixel;
    float2 nrcTrainingDownscale;

    uint nrcMaxPathVertices;
    float3 cameraWorldPos;

    float3 skyColor;
    uint _pad0;

    float3 sunLightDirection;
    uint _pad1;

    float3 sunLightRadiance;
    float sunTanHalfAngle;
    float throughputThreshold;
};

ConstantBuffer<NrcConstants> g_NrcConstants : register(b0);
ConstantBuffer<GlobalConstants> g_Global : register(b1);

RWStructuredBuffer<NrcPackedQueryPathInfo> u_QueryPathInfo : register(u0, space0); // Misc path info (vertexCount, queryIndex)
RWStructuredBuffer<NrcPackedTrainingPathInfo> u_TrainingPathInfo : register(u1, space0); // Misc path info (vertexCount, queryIndex)
RWStructuredBuffer<NrcPackedPathVertex> u_TrainingPathVertices : register(u2, space0); // Path vertex data used to train the neural radiance cache
RWStructuredBuffer<NrcRadianceParams> u_QueryRadianceParams : register(u3, space0);
RWStructuredBuffer<uint> u_CountersData : register(u4, space0);

RWTexture2D<float4> u_NebDebugQueryThroughputMap : register(u0, space1);
RWTexture2D<uint>   u_NebDebugQueryHitMap : register(u1, space1);

enum GbufferSlot
{
    Gbuffer_Albedo = 0,
    Gbuffer_RoughnessMetalness = 1,
    Gbuffer_WorldPos = 2,
    Gbuffer_NumSlots,
};

Texture2D t_GbufferTextures[Gbuffer_NumSlots] : register(t0, space1);
Texture2D t_GbufferNormals : register(t3, space1);
Texture2D t_SceneDepth : register(t0, space0);
Texture2D<uint> t_SceneStencil : register(t1, space0);
RaytracingAccelerationStructure SceneBVH : register(t2, space0);

// space 3 for bindless heap, essentially an entire SRV heap
Texture2D t_BindlessTextures[] : register(t0, space3);
ByteAddressBuffer t_BindlessBuffers[] : register(t0, space4);

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
    float4x4 surfaceToWorld;
    int materialIndex;
    
    int indexBufferIndex;
    uint indexBufferOffset; // in bytes
    uint indexBufferStride; // either 2 of 4 for uint32 or uint16
    uint indexBufferSizeInbytes;

    int attributeBufferIndices[GeometryAttribute_NumTypes];
    uint attributeBufferOffsets[GeometryAttribute_NumTypes]; // in bytes
    uint attributeBufferStrides[GeometryAttribute_NumTypes]; // in bytes
    uint numVertices; // all attributes match this number of elements
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

    bool IsHit() { return hitDistance > 0.0f; }
};

struct Attributes
{
    float2 uv;
};

const bool IsGbufferMiss(in uint2 loc)
{
    return t_SceneStencil.Load(uint3(loc.x, loc.y, 0)) == 0;
}

RayDesc QueryReconstructedHemisphereRay(float3 worldPos, float3 SN, inout uint rng, out float pdf)
{
    // Set up the ray
    float3 wi = CosineSampleHemisphereSurfaceAligned(Rand2(rng), SN, pdf);

    RayDesc ray;
    ray.Origin = worldPos + SN * 1e-2;
    ray.Direction = wi;
    ray.TMin = 0.01;
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
    data.H = normalize(V + L);
    //data.H = Ndf_HalfvectorSampleGGXVndf(V, float2(data.alpha, data.alpha), u);
    
    float3 H = data.H;
    data.LdotN = dot(L, N);
    data.VdotH = saturate(dot(V, H));
    data.VdotN = dot(V, N);
    data.NdotH = dot(N, H);

    // Unpack material properties
    data.specularF0 = Brdf_GetSpecularF0(surfaceSample.albedo, surfaceSample.metalness);
    data.diffuseReflectance = Brdf_GetDiffuseReflectance(surfaceSample.albedo, surfaceSample.metalness);
    return data;
}

// This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
float3 EvaluateDirectBRDF(in uint2 loc,
                    in uint rng,
                    in SurfaceSample surfaceSample,
                    in float3 V,
                    in float3 L)
{
    float3 SN = surfaceSample.SN;
    BRDFData data = PrepareBRDFData(SN, L, V, surfaceSample, Rand2(rng));

    // Eval specular and diffuse BRDFs
    float3 F0 = Brdf_GetSpecularF0(surfaceSample.albedo, surfaceSample.metalness);
    float3 F = Brdf_FresnelSchlick(F0, saturate(data.VdotH));
    float3 Kd = 1.0 - F; // We assume F to represent Ks -> Kd = 1.0 - Ks
    float3 O = Kd * Brdf_Diffuse_Lambertian(surfaceSample.albedo) + Brdf_Specular_CookTorrance(F, surfaceSample.roughness,
                                                                                                        saturate(data.VdotN),
                                                                                                        saturate(data.LdotN),
                                                                                                        saturate(data.VdotH),
                                                                                                        saturate(data.NdotH));
    return O;
}

bool EvaluateIndirectBRDF(in uint2 loc,
                  in uint rng,
                  in SurfaceSample surfaceSample,
                  in float3 V,
                  out float3 rayDirection,
                  out float3 sampleWeight,
                  out float pdf,
                  out float diffuseProbability)
{
    // Ignore incident ray coming from "below" the hemisphere
    rayDirection = 0.0;
    sampleWeight = 0.0;
    pdf = 0.0;
    diffuseProbability = 0.0;

    float3 SN = normalize(surfaceSample.SN);

    // (local cosine hemisphere space is oriented so that its positive Z axis points along the shading normal)
    float3 L = CosineSampleHemisphereSurfaceAligned(Rand2(rng), SN, pdf); // randomly sample ray direction from the surface hemisphere
    BRDFData data = PrepareBRDFData(SN, L, V, surfaceSample, Rand2(rng));

    // Function 'diffuseTerm' is predivided by PDF of sampling the cosine weighted hemisphere
    // as we use lambertian BRDF, diffuse sample weight == diffuse component == diffuse reflectance factor
    diffuseProbability = (1.0 - Brdf_GetSpecularProbability(saturate(data.VdotN), data.specularF0, surfaceSample.albedo));
    //diffuseProbability = (1.0 - Brdf_FresnelSchlick(data.specularF0, data.VdotN));
    sampleWeight = data.diffuseReflectance;
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

// Returns three consecutive 16-bit indices that start at offsetInBytes
// offsetInBytes must be 2-byte aligned or 4-byte aligned
//
// https://github.com/microsoft/DirectXShaderCompiler/wiki/ByteAddressBuffer-Load-Store-Additions
// For instance, if the largest type is uint64_t, byteOffset must be aligned by 8. If the largest type is float16_t, 
// then the minimum alignment required is 2.
uint3 ReadUint16Indices(in ByteAddressBuffer buf, in uint offsetInBytes)
{
    uint3 indices;
    indices.x = (uint)buf.Load<uint16_t>(offsetInBytes);
    indices.y = (uint)buf.Load<uint16_t>(offsetInBytes + 2);
    indices.z = (uint)buf.Load<uint16_t>(offsetInBytes + 4);
    return indices;
}

bool ReconstructSurfaceData(
    in uint triangleIndex,
    in uint geometryIndex,
    in float2 rayBarycentrics,
    in StructuredBuffer<GeometryData> geometryBuffer,
    in StructuredBuffer<MaterialData> materialBuffer,
    out SurfaceSample surfaceSample)
{
    GeometryData geometry = geometryBuffer[geometryIndex];

    float3 barycentrics;
    barycentrics.yz = rayBarycentrics;
    barycentrics.x = 1.0 - (barycentrics.y + barycentrics.z);
    
    if (geometry.indexBufferIndex == InvalidIndex ||
        geometry.attributeBufferIndices[GeometryAttribute_Position] == InvalidIndex ||
        geometry.attributeBufferIndices[GeometryAttribute_Normal] == InvalidIndex ||
        geometry.attributeBufferIndices[GeometryAttribute_TexCoords] == InvalidIndex || 
        geometry.attributeBufferIndices[GeometryAttribute_Tangents] == InvalidIndex)
        return false;

    ByteAddressBuffer ib = t_BindlessBuffers[NonUniformResourceIndex(geometry.indexBufferIndex)];
    uint3 indices = (geometry.indexBufferStride == 2) ? 
        ReadUint16Indices(ib, geometry.indexBufferOffset + triangleIndex * (geometry.indexBufferStride * 3)) :
        ReadUint32Indices(ib, geometry.indexBufferOffset + triangleIndex * (geometry.indexBufferStride * 3));

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
    surfaceSample.GN = normalize(InterpolateBary(rawGeometryNormals, barycentrics));
    surfaceSample.GN = normalize(mul(float4(surfaceSample.GN, 0.0), geometry.surfaceToWorld).xyz);

    ByteAddressBuffer uvBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.attributeBufferIndices[GeometryAttribute_TexCoords])];
    float2 rawUvs[3];
    rawUvs[0] = asfloat(uvBuffer.Load2(geometry.attributeBufferOffsets[GeometryAttribute_TexCoords] + indices[0] * geometry.attributeBufferStrides[GeometryAttribute_TexCoords]));
    rawUvs[1] = asfloat(uvBuffer.Load2(geometry.attributeBufferOffsets[GeometryAttribute_TexCoords] + indices[1] * geometry.attributeBufferStrides[GeometryAttribute_TexCoords]));
    rawUvs[2] = asfloat(uvBuffer.Load2(geometry.attributeBufferOffsets[GeometryAttribute_TexCoords] + indices[2] * geometry.attributeBufferStrides[GeometryAttribute_TexCoords]));
    float2 uv = InterpolateBary(rawUvs, barycentrics);

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
        ByteAddressBuffer tangentBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.attributeBufferIndices[GeometryAttribute_Tangents])];
        float4 rawTangents[3]; // float4 as last element is a length of bitangent
        rawTangents[0] = asfloat(tangentBuffer.Load4(geometry.attributeBufferOffsets[GeometryAttribute_Tangents] + indices[0] * geometry.attributeBufferStrides[GeometryAttribute_Tangents]));
        rawTangents[1] = asfloat(tangentBuffer.Load4(geometry.attributeBufferOffsets[GeometryAttribute_Tangents] + indices[1] * geometry.attributeBufferStrides[GeometryAttribute_Tangents]));
        rawTangents[2] = asfloat(tangentBuffer.Load4(geometry.attributeBufferOffsets[GeometryAttribute_Tangents] + indices[2] * geometry.attributeBufferStrides[GeometryAttribute_Tangents]));
        float4 tangent = normalize(InterpolateBary(rawTangents, barycentrics));
        float3 bitangent = normalize(cross(surfaceSample.GN, tangent.xyz) * tangent.w);

        surfaceSample.SN = surfaceSample.GN; // if no normal map then surface normal == geometry normal
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
    uint2 loc = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;
    uint rng = InitRNG(loc, dim, g_Global.frameIndex);

    // Context + per-frame buffers
    NrcBuffers nrcBufs;
    nrcBufs.queryPathInfo = u_QueryPathInfo;
    nrcBufs.trainingPathInfo = u_TrainingPathInfo;
    nrcBufs.trainingPathVertices = u_TrainingPathVertices;
    nrcBufs.queryRadianceParams = u_QueryRadianceParams;
    nrcBufs.countersData = u_CountersData;
    NrcContext ctx = NrcCreateContext(g_NrcConstants, nrcBufs, loc);
    
    // offset pixel a bit to have some jitter during learning
    // we will only have 1 sample in Update mode anyways, so do it here, before reading gbuffers 
    if (NrcIsUpdateMode())
    {
        float2 resolution = dim * g_Global.nrcTrainingDownscale;
        float2 jittered = loc + (Rand2(rng)) * g_Global.nrcTrainingDownscale;
        loc = uint2(clamp(jittered * g_Global.nrcTrainingDownscale, 0.0, resolution));
    }

    float3 albedo = t_GbufferTextures[Gbuffer_Albedo].Load(float3(loc, 0)).xyz;
    float3 worldPos = t_GbufferTextures[Gbuffer_WorldPos].Load(float3(loc, 0)).xyz;
    float4 encodedN = t_GbufferNormals.Load(float3(loc, 0));
    float3 GN, SN;
    UnpackOct16Normals(encodedN, GN, SN);
    float2 roughnessMetalness = t_GbufferTextures[Gbuffer_RoughnessMetalness].Load(float3(loc, 0)).xy;
    float roughness = roughnessMetalness.x;
    float metalness = roughnessMetalness.y;

    float3 V = g_Global.cameraWorldPos - worldPos;

    const uint samplesPerPixel = NrcIsUpdateMode() ? 1 : g_NrcConstants.samplesPerPixel;
    for (int sampleIndex = 0; sampleIndex < samplesPerPixel; ++sampleIndex)
    {
        // Initialize NRC data for path and sample index traced in this thread
        NrcSetSampleIndex(ctx, sampleIndex);
        NrcPathState nrcPathState = NrcCreatePathState(g_NrcConstants, Rand(rng));

        // The current Monte-Carlo weight carried by the path when it arrives at this vertex
        // starts at (1, 1, 1). After each bounce multiplied by the BRDF-factor for that bounce.
        float3 throughput = float3(1.0, 1.0, 1.0);

        // The energy already accumulated by the path before reaching the N vertex. 
        // starts at (0, 0, 0). Represents whats been added to the image so far (e.g. emissive hit, environment miss, direct-light NEE from earlier bounces).
        float3 radiance = float3(0.0, 0.0, 0.0);

        // early bail if gbuffer miss
        //if (IsGbufferMiss(loc))
        //{
        //    //NrcUpdateOnMiss(nrcPathState);
        //    //radiance += g_Global.skyColor;
        //    //break;
        //}

        
        // Flip normals towards the incident ray direction (needed for backfacing triangles)
        NrcSurfaceAttributes surfaceAttributes;
        surfaceAttributes.encodedPosition = NrcEncodePosition(worldPos, g_NrcConstants); // Use NrcEncodePosition
        surfaceAttributes.roughness = roughness;
        surfaceAttributes.specularF0 = Brdf_GetSpecularF0(albedo, metalness);
        surfaceAttributes.diffuseReflectance = Brdf_GetDiffuseReflectance(albedo, metalness); // pd term of diffuse BRDF term (albedo factor)
        surfaceAttributes.shadingNormal = SN;
        surfaceAttributes.viewVector = normalize(V);
        surfaceAttributes.isDeltaLobe = (metalness == 1.0f && roughness == 0.0f);

        NrcProgressState nrcProgressState = NrcUpdateOnHit(ctx, nrcPathState, surfaceAttributes, length(V), 0, throughput, radiance); // Update NRC state on hit
        if (nrcProgressState == NrcProgressState::TerminateImmediately)
        {
            NrcWriteFinalPathInfo(ctx, nrcPathState, throughput, radiance);
            break;
        }

        throughput *= surfaceAttributes.diffuseReflectance; // as pdf = cos / INV_PI -> INV_PI = pdf / cos -> PI = cos / pdf
        float diffuseProbability = (1.0 - Brdf_GetSpecularProbability(saturate(dot(normalize(V), SN)), surfaceAttributes.specularF0, albedo));
        if (Rand(rng) < diffuseProbability)
        {
            throughput /= diffuseProbability;
        }
        
        // Define a ray, where the origin is currently reconstructed Gbuffer world pos and the direction is a randomly selected hemisphere direction wi
        float pdf;
        RayDesc ray = QueryReconstructedHemisphereRay(worldPos, SN, rng, pdf);
        NrcSetBrdfPdf(nrcPathState, pdf);

        // Prepare Payload and other data...
        RayPayload payload;
        payload.hitDistance = -1.0f;
        payload.instanceID = ~0U;
        payload.primitiveIndex = ~0U;
        payload.geometryIndex = ~0U;
        payload.barycentrics = 0;

        int bounce = 1;
        for (; bounce < g_Global.nrcMaxPathVertices; ++bounce)
        {
            TraceRay(SceneBVH, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff,
                0, // RayContributionToHitGroupIndex
                0, // MultiplierForGeometryContributionToHitGroupIndex
                0, // MissShaderIndex
                ray,
                payload);

            if (!payload.IsHit())
            {
                //u_NebDebugQueryHitMap[loc] = uint(bounce);
                NrcUpdateOnMiss(nrcPathState); // Handle miss
                radiance += g_Global.skyColor * throughput;
                break;
            }

            // Decode material properties...  
            SurfaceSample surfaceSample;
            if (!ReconstructSurfaceData(payload.primitiveIndex, payload.geometryIndex, payload.barycentrics, t_GeometryData, t_MaterialData, surfaceSample))
            {
                NrcSetDebugPathTerminationReason(nrcPathState, NrcDebugPathTerminationReason::Unset);
                break;
            }

            float hitT = payload.hitDistance;
            float3 hitP = ray.Origin + ray.Direction * payload.hitDistance;
            V = normalize(-ray.Direction);

            // Flip normals towards the incident ray direction (needed for backfacing triangles)
            //if (dot(surfaceSample.GN, V) < 0.0)
            //    surfaceSample.GN = -surfaceSample.GN;
            //
            //if (dot(surfaceSample.SN, V) < 0.0)
            //    surfaceSample.SN = -surfaceSample.SN;

            NrcSurfaceAttributes sampledSurfaceAttributes; // Passed to NrcUpdateOnHit
            sampledSurfaceAttributes.encodedPosition = NrcEncodePosition(hitP, g_NrcConstants); // Use NrcEncodePosition
            sampledSurfaceAttributes.roughness = surfaceSample.roughness;
            sampledSurfaceAttributes.specularF0 = Brdf_GetSpecularF0(surfaceSample.albedo, surfaceSample.metalness);
            sampledSurfaceAttributes.diffuseReflectance = Brdf_GetDiffuseReflectance(surfaceSample.albedo, surfaceSample.metalness); // pd term of diffuse BRDF term (albedo factor)
            sampledSurfaceAttributes.shadingNormal = surfaceSample.SN;
            sampledSurfaceAttributes.viewVector = V;
            sampledSurfaceAttributes.isDeltaLobe = (surfaceSample.metalness == 1.0f && surfaceSample.roughness == 0.0f);

            NrcProgressState nrcProgressState = NrcUpdateOnHit(ctx, nrcPathState, sampledSurfaceAttributes, hitT, bounce, throughput, radiance); // Update NRC state on hit
            if (nrcProgressState == NrcProgressState::TerminateImmediately)
            {
                break;
            }

            // Account for emissives and evaluate NEE with RIS...
            {
    
                float2 u = Rand2(rng);
                float angle = u.x * 2.0f * PI;
                float distance = sqrt(u.y);

                float3 L = normalize(-g_Global.sunLightDirection);
                float3 B = normalize(GetPerpendicularVector(L));
                float3 T = cross(B, L);
                float3 incidentVector;
                incidentVector = L + (B * sin(angle) + T * cos(angle)) * g_Global.sunTanHalfAngle * distance;
                incidentVector = normalize(incidentVector);

                bool transitionSunRay = dot(surfaceSample.GN, incidentVector) <= 0.0f;
                RayDesc sunRay;
                sunRay.Origin = hitP + (transitionSunRay ? -surfaceSample.GN : surfaceSample.GN) * 1e-2;
                sunRay.Direction = incidentVector;
                sunRay.TMin = 0.001;
                sunRay.TMax = TRACING_MAX_DISTANCE;

                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> rq;
                rq.TraceRayInline(SceneBVH, 0, 0xFF, sunRay);
                rq.Proceed(); // the DXR spec says you must loop until it returns false.

                if (rq.CommittedStatus() == COMMITTED_NOTHING)
                {
                    float3 O = EvaluateDirectBRDF(loc, rng, surfaceSample, V, L) * g_Global.sunLightRadiance;
                    radiance += O * throughput;
                }
            }
            
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
            {
                break;
            }
            
            // Here NRC russian roulette if available
            // if (g_Global.enableRussianRoulette && NrcCanUseRussianRoulette(nrcPathState) && (bounce > 3 /*BOUNCE_MIN*/)) {}

            // Sample BRDF to generate the next ray and run MIS
            float3 rayDirection;
            float3 sampleWeight;
            if (!EvaluateIndirectBRDF(loc, rng, surfaceSample, V, rayDirection, sampleWeight, pdf, diffuseProbability))
            {
                //u_NebDebugQueryHitMap[loc] = uint(bounce);
                NrcSetDebugPathTerminationReason(nrcPathState, NrcDebugPathTerminationReason::BRDFAbsorption);
                break; // Ray was eaten by the surface :(
            }

            // no transitions with this brdf
            ray.Origin = hitP + surfaceSample.GN * 1e-2;
            ray.Direction = rayDirection;
            ray.TMin = 0.001;
            ray.TMax = TRACING_MAX_DISTANCE;

            // Account for surface properties using the BRDF "weight"
            throughput *= sampleWeight;
            if (Rand(rng) < diffuseProbability)
            {
                // always sample DIFFUSE BRDF! Here we only do diffuse!
                throughput /= diffuseProbability;
            }

            NrcSetBrdfPdf(nrcPathState, pdf);
        }

        NrcWriteFinalPathInfo(ctx, nrcPathState, throughput, radiance);
    }
}

[shader("miss")]
void PathtracerMS(inout RayPayload payload : SV_RayPayload)
{
    payload.hitDistance = -1.0f;
}

[shader("closesthit")]
void PathtracerCH(
    inout RayPayload payload : SV_RayPayload, 
    in Attributes attrib : SV_IntersectionAttributes)
{
    payload.hitDistance = RayTCurrent();
    payload.instanceID = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.geometryIndex = GeometryIndex();
    payload.barycentrics = attrib.uv;
}