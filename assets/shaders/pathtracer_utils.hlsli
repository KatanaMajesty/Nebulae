#include "octahedron_encoding.hlsli"

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

void UnpackOct16Normals(in float4 encodedN, out float3 GN, out float3 SN)
{
    // .xy - geometry normal packed, .zw - surface normal packed
    GN = Oct16_FastUnpack(encodedN.xy);
    SN = Oct16_FastUnpack(encodedN.zw);
}

SurfaceSample ReconstructSurfaceData()
{
    SurfaceSample result;
    return result;
}