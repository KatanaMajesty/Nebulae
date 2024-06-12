
//struct VSInput
//{
//    float3 Position : POSITION;
//    float3 Normal : NORMAL;
//    float2 TexCoords : TEX_COORDS;
//};

struct VSOutput
{
    float4 Pos : SV_Position;
};

static const float2 Positions[] =
{
    float2(-0.5, -0.5),
    float2(0.0, 0.5),
    float2(0.5, -0.5)
};

VSOutput VSMain(uint vid : SV_VertexID)
{
    VSOutput output;
    output.Pos = float4(Positions[vid], 0.0, 1.0);

    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 1.0);
}