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
    float4x4 InstanceToWorld;
    float4x4 ViewProj;
};

ConstantBuffer<InstanceInfo> cbInstanceInfo : register(b0);

VSOutput VSMain(VSInput input)
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

float3 SRGBtoLinear(float3 c)
{
    return pow(c, 2.2);
} // If your textures are sRGB

// Fresnel Schlick Approximation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Normal Distribution Function (GGX)
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265 * denom * denom;

    return num / denom;
}

// Geometry Schlick-GGX
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float4 PSMain(VSOutput input) : SV_Target
{
    // Camera position, for demo use (0,0,0). In practice pass as a uniform!
    float3 camPos = float3(0, 0, 0);

    // Sample material textures
    float3 albedo = SRGBtoLinear(MaterialTextures[kMaterialTextures_AlbedoMapIndex].Sample(StaticSampler, input.TexUv).rgb);
    float3 normalSample = MaterialTextures[kMaterialTextures_NormalMapIndex].Sample(StaticSampler, input.TexUv).xyz * 2.0 - 1.0;
    float2 rm = MaterialTextures[kMaterialTextures_RoughnessMetalnessMapIndex].Sample(StaticSampler, input.TexUv).rg;
    float roughness = saturate(rm.r);
    float metallic = saturate(rm.g);

    // Transform tangent space normal to world space
    float3x3 TBN = float3x3(normalize(input.Tangent), normalize(input.Bitangent), normalize(input.WorldNormal));
    float3 N = normalize(mul(normalSample, TBN));
    float3 V = normalize(camPos - input.WorldPos); // view direction

    // Light setup (single directional for demo)
    float3 L = normalize(float3(-0.5, -1.0, -1.0));
    float3 H = normalize(V + L);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float NdotL = max(dot(N, L), 0.0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
    float3 specular = numerator / denominator;

    // kS is energy conserved specular, kD is diffuse
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 diffuse = kD * albedo / 3.14159265;

    float3 radiance = float3(1.0, 1.0, 1.0); // white light

    float3 color = (diffuse + specular) * radiance * NdotL;

    // Ambient (simplified)
    float3 ambient = float3(0.03, 0.03, 0.03) * albedo;
    color += ambient;

    // Output
    color = color / (color + 1.0); // Reinhard tonemapping
    color = pow(color, 1.0 / 2.2); // Gamma correction

    return float4(color, 1.0);
}