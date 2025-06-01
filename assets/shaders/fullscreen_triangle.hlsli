struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

// .xy - pos, .zw - uv
static const float4 FullscreenTriangle[3] =
{
    float4(-1.0, 1.0, 0.0, 0.0),
    float4(3.0, 1.0, 2.0, 0.0),
    float4(-1.0, -3.0, 0.0, 2.0),
};