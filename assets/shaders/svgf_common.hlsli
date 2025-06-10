// from NVIDIAs SVGF paper (Section 4.4 'Edge-stopping functions')
// "We adopt a cosine term for our edge-stopping function on world-space normals"
// @ref https://research.nvidia.com/publication/2017-07_spatiotemporal-variance-guided-filtering-real-time-reconstruction-path-traced
float SVGF_NWeight(float3 N0, float3 N1)
{
    return saturate(dot(N0, N1));
}

// from NVIDIAs SVGF paper (Section 4.4 'Edge-stopping functions')
// @ref https://research.nvidia.com/publication/2017-07_spatiotemporal-variance-guided-filtering-real-time-reconstruction-path-traced
float SVGF_DWeight(float Z0, float Z1, float sigma)
{
    float dz = abs(Z0 - Z1);
    return exp(-dz * dz / (2.0 * sigma * sigma));
}

float SVGF_LuminanceWeightVarDenom(float phi, float var)
{
    return max(phi * sqrt(var), 1e-3);
}

float SVGF_LuminanceWeight(float3 L0, float3 L1, float varDenom)
{
    float3 dL = L0 - L1;
    return exp(-dot(dL, dL) / (4.0 * varDenom * varDenom));
}