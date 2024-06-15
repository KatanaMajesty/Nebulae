
struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoords : TEX_COORDS;
};

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 TexUv : TEXCOORD;
};

struct InstanceInfo
{
    // Requires 256 alignment
    float4x4 InstanceToWorld;
    float4x4 ViewProj; // Currently view-proj is in instance info, just for the time being
};

ConstantBuffer<InstanceInfo> cbInstanceInfo : register(b0);

VSOutput VSMain(VSInput input)
{
    float4 worldPos = mul(float4(input.Position, 1.0), cbInstanceInfo.InstanceToWorld);

    VSOutput output;
    output.Pos = mul(worldPos, cbInstanceInfo.ViewProj);
    output.TexUv = input.TexCoords;

    return output;
}

#define kMaterialTextures_AlbedoMapIndex 0
#define kMaterialTextures_NormalMapIndex 1
#define kMaterialTextures_RoughnessMetalnessMapIndex 2
#define kMaterialTextures_NumTextureTypes 3

Texture2D MaterialTextures[kMaterialTextures_NumTextureTypes] : register(t0, space0);
SamplerState StaticSampler : register(s0);

float4 PSMain(VSOutput input) : SV_Target0
{
    float3 albedo = MaterialTextures[kMaterialTextures_AlbedoMapIndex].Sample(StaticSampler, input.TexUv).rgb;
    return float4(albedo, 1.0);
}