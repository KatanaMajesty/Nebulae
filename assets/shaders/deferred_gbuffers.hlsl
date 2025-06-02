#include "octahedron_encoding.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texUvs : TEXCOORD;
    float4 tangent : TANGENT;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 texUv : TEXCOORD;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float3 worldNormal : WORLDNORMAL;
    float3 worldPos : WORLDPOS;
};

// from Material.h
#define kMaterialFlag_None 0
#define kMaterialFlag_HasAlbedoMap 1
#define kMaterialFlag_HasNormalMap 2
#define kMaterialFlag_HasRoughnessMetalnessMap 4

struct InstanceInfo
{
    // Requires 256 alignment
    float4x4 InstanceToWorld;
    float4x4 ViewProj; // Currently view-proj is in instance info, just for the time being
    uint MaterialFlags; // refer to kMaterialFlag_*
};
ConstantBuffer<InstanceInfo> cbInstanceInfo : register(b0);

VSOutput VSMain(VSInput input)
{
    float4 worldPos = mul(float4(input.position, 1.0), cbInstanceInfo.InstanceToWorld);

    float3 N = normalize(input.normal);
    float3 tangent = input.tangent.xyz;
    float3 bitangent = normalize(cross(N, tangent) * input.tangent.w);

    VSOutput output;
    output.pos = mul(worldPos, cbInstanceInfo.ViewProj);
    output.texUv = input.texUvs;
    output.tangent = tangent;
    output.bitangent = bitangent;

    // TODO: we can now multiply normal with world matrix as we do not use scaling
    output.worldNormal = normalize(mul(float4(input.normal, 0.0), cbInstanceInfo.InstanceToWorld).xyz);
    output.worldPos = worldPos.xyz;
    return output;
}

#define kMaterialTextures_AlbedoMapIndex 0
#define kMaterialTextures_NormalMapIndex 1
#define kMaterialTextures_RoughnessMetalnessMapIndex 2
#define kMaterialTextures_NumTextureTypes 3
Texture2D MaterialTextures[kMaterialTextures_NumTextureTypes] : register(t0, space0);
SamplerState StaticSampler : register(s0);

struct PSOutput
{
    float3 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float2 roughnessMetalness : SV_Target2;
    float4 worldPos : SV_Target3;
};

PSOutput PSMain(VSOutput input)
{
    float4 albedo = 0.0;
    if (cbInstanceInfo.MaterialFlags & kMaterialFlag_HasAlbedoMap)
    {
        albedo = MaterialTextures[kMaterialTextures_AlbedoMapIndex].Sample(StaticSampler, input.texUv);
    }

    float3 GN = normalize(input.worldNormal);
    float3 SN = GN;
    if (cbInstanceInfo.MaterialFlags & kMaterialFlag_HasNormalMap)
    {
        float3 tangent = normalize(input.tangent);
        float3 bitangent = normalize(input.bitangent);
        float3x3 TBN = float3x3(tangent, bitangent, SN);
        float3 NSample = MaterialTextures[kMaterialTextures_NormalMapIndex].Sample(StaticSampler, input.texUv).xyz * 2.0 - 1.0;
        SN = normalize(mul(NSample, TBN));
    }
    
    // TODO: Make it roughness factor + metalness factor from Cbuffer
    float2 roughnessMetalness = float2(1.0, 0.0);
    if (cbInstanceInfo.MaterialFlags & kMaterialFlag_HasRoughnessMetalnessMap)
    {
        // Gltf stores roughness in green channel and metalness in blue channel
        roughnessMetalness = MaterialTextures[kMaterialTextures_RoughnessMetalnessMapIndex].Sample(StaticSampler, input.texUv).gb;
    }
    
    PSOutput output;
    output.albedo = albedo.rgb;
    output.normal = float4(Oct16_FastPack(GN), Oct16_FastPack(SN));
    output.roughnessMetalness = roughnessMetalness;
    output.worldPos = float4(input.worldPos, 0.0);
    return output;
}