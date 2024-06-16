
struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoords : TEXCOORD;
    float4 Tangent : TANGENT;
};

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 TexUv : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;

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

    float3 N = normalize(input.Normal);
    float3 tangent = input.Tangent.xyz;
    float3 bitangent = normalize(cross(N, tangent) * input.Tangent.w);

    VSOutput output;
    output.Pos = mul(worldPos, cbInstanceInfo.ViewProj);
    output.TexUv = input.TexCoords;
    output.Tangent = tangent;
    output.Bitangent = bitangent;

    // TODO: we can now multiply normal with world matrix as we do not use scaling
    output.WorldNormal = normalize(mul(float4(input.Normal, 0.0), cbInstanceInfo.InstanceToWorld).xyz);
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
    float3x3 TBN = float3x3(normalize(input.Tangent), normalize(input.Bitangent), N);

    // Surface normal
    float3 NSampled = MaterialTextures[kMaterialTextures_NormalMapIndex].Sample(StaticSampler, input.TexUv).xyz * 2.0 - 1.0;
    float3 SN = normalize(mul(NSampled, TBN));

    float incidenceFactor = clamp(dot(-L, SN), 0.01, 1.0);

    float3 albedo = MaterialTextures[kMaterialTextures_AlbedoMapIndex].Sample(StaticSampler, input.TexUv).rgb;
    return float4((Ambient + incidenceFactor) * albedo, 1.0);
}