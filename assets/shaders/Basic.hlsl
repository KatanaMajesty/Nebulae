
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

    float3 WorldNormal : WORLDNORMAL;
    float3 WorldPos : WORLDPOS;
};

struct InstanceInfo
{
    // Requires 256 alignment
    float4x4 InstanceToWorld;
    float4x4 ViewProj; // Currently view-proj is in instance info, just for the time being
};

ConstantBuffer<InstanceInfo> cbInstanceInfo : register(b0);

VSOutput VSMain(in VSInput input)
{
    float4 worldPos = mul(float4(input.Position, 1.0), cbInstanceInfo.InstanceToWorld);

    VSOutput output;
    output.Pos = mul(worldPos, cbInstanceInfo.ViewProj);
    output.TexUv = input.TexCoords;

    // TODO: we can now multiply normal with world matrix as we do not use scaling
    output.WorldNormal = normalize(mul(float4(input.Normal, 0.0), cbInstanceInfo.InstanceToWorld).xyz);
    //output.WorldNormal = input.Normal;
    output.WorldPos = worldPos.xyz;

    return output;
}

#define kMaterialTextures_AlbedoMapIndex 0
#define kMaterialTextures_NormalMapIndex 1
#define kMaterialTextures_RoughnessMetalnessMapIndex 2
#define kMaterialTextures_NumTextureTypes 3

Texture2D MaterialTextures[kMaterialTextures_NumTextureTypes] : register(t0, space0);
SamplerState StaticSampler : register(s0);

float4 PSMain(in VSOutput input) : SV_Target0
{
    static const float3 L = normalize(float3(-0.5, -1.0, -1.0));
    static const float3 Ambient = float3(0.04, 0.01, 0.02);

    float3 N = normalize(input.WorldNormal);
    float incidenceFactor = saturate(dot(-L, N));

    float3 albedo = MaterialTextures[kMaterialTextures_AlbedoMapIndex].Sample(StaticSampler, input.TexUv).rgb;
    return float4(Ambient + incidenceFactor * albedo, 1.0);
}