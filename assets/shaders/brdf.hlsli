// https://youtu.be/RRE-F57fbXw?t=560 timecode of the whole thing
// In Pbr we want energy conservation, thus assertion -> Kd + Ks = 1

float3 Brdf_GetSpecularF0(in float3 albedo, in float metalness)
{
    return lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
}

float3 Brdf_GetDiffuseReflectance(in float3 albedo, in float metalness)
{
    return albedo * (1.0 - metalness);
}

// Fresnel effect dictates the amount of specular reflection that takes place for
// the provided angle
// 
// Basically we can represent the Fresnel's factor as Ks specular coefficient
// in float F0 - material's base reflectivity (reflectivity when the viewing angle is perpendicular to the surface, aka colinear with surface's normal)
// in float3 V - normalized view vector
// in float3 H - normalized half-vector between view vector V and light vector L
// returns Schlick's approximation of Fresnel factor (https://en.wikipedia.org/wiki/Schlick%27s_approximation)
float3 Brdf_FresnelSchlick(in float3 F0, in float VdotH)
{
    return F0 + (1.0 - F0) * (1.0 - pow(VdotH, 5.0));
}

// TODO: Move math-related constants to separate Math.hlsli (?)
static const float PI = 3.14159265;
static const float PI_INV = 1.0 / PI;
static const float PI_TWO = 2.0 * PI;

// Lambertian Diffuse Model
// in float3 albedo - Albedo of a material
float3 Brdf_Diffuse_Lambertian(in float3 albedo)
{
    return albedo * PI_INV;
}

// Normal distribution function -> GGX/Trowbridge-Reitz model
// function determines how microfacets on the point are distributed according to roughness
// in float alpha - bascially is roughness^2
float Ndf_GGXTrowbridgeReitz(in float alpha, in float NdotH)
{
    float alphaSq = alpha * alpha;
    float distribution = (NdotH * NdotH) * (alphaSq - 1.0) + 1.0;
    return alphaSq / (PI * distribution * distribution);
}

// Samples a half-vector of specular BRDF using GGX distribution accounting for VNDF ('visible-normal distribution') method
// Source: "Sampling the GGX Distribution of Visible Normals" by Heitz
// See also https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/
//
// Notice: Alpha2D is not equal to AlphaSquared, its just a float2 of alphas
float3 Ndf_HalfvectorSampleGGXVndf(float3 Ve, float2 alpha2D, float2 u)
{
    // Section 3.2 -> transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha2D.x * Ve.x, alpha2D.y * Ve.y, Ve.z));

    // Section 4.1 -> orthonormal basis (with special case if cross product is zero)
    // rsqrt is a faster implementation of 1/sqrt
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0f ? float3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    float3 T2 = cross(Vh, T1);

    // Section 4.2 -> parameterization of the projected area
    float r = sqrt(u.x);
    float phi = PI_TWO * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = lerp(sqrt(1.0f - t1 * t1), t2, s);

    // Section 4.3 -> reprojection onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

    // Section 3.4 -> transforming the normal back to the ellipsoid configuration
    return normalize(float3(alpha2D.x * Nh.x, alpha2D.y * Nh.y, max(0.0f, Nh.z)));
}

// Schlick approximated the Smith equation for Beckmann. Naty warns that Schlick approximated the wrong version of Smith, 
// so be sure to compare to the Smith version before using
// in XdotN is a dot product between N and vector X (Primarily, vector X would be either view vector or light vector depending on the context).
float Gsf_SchlickBeckmann(in float k, in float XdotN)
{
    float denom = 1.0 / (XdotN * (1.0 - k) + k);
    return XdotN * denom;
}

// Geometry shadowing function -> Smith/Schlick-Beckmann model (Schlick-GGX model)
// Smith model takes into acount two types of geometry interaction between L, N and V, N vectors
float Gsf_GGXSchlick(in float alpha, in float VdotN, in float LdotN)
{
    // Schlick-GGX k coefficient remapping
    float k = alpha * 0.5;
    return Gsf_SchlickBeckmann(k, VdotN) * Gsf_SchlickBeckmann(k, LdotN);
}

// Cook-Torrance Specular Model
// in float F - fresnel factor. In most common case the result of Brdf_FresnelSchlick(F0, VdotH)
float3 Brdf_Specular_CookTorrance(in float3 F, in float roughness, in float VdotN, in float LdotN, in float VdotH, in float NdotH)
{
    float denom = 1.0 / (4.0 * VdotN * LdotN);
    
    // Normal distribution function
    float alpha = roughness * roughness;
    float Ndf = Ndf_GGXTrowbridgeReitz(alpha, NdotH);
    
    // Geometry shadowing function
    float Gsf = Gsf_GGXSchlick(alpha, VdotN, LdotN);
    return Ndf * Gsf * F * denom;
}

// probability density for routine to return for the outgoing direction
// For sampling of all our diffuse BRDFs use Lambertian, cosine-weighted hemisphere sampling, with PDF equal to (NdotL/PI)
// 
float DiffusePdf(float NdotL)
{
    return NdotL * PI_INV;
}

// converts an RGB triplet to a single 'brightness' number that approximates how the human eye weights red, green and blue light.
// Ref: ITU-R BT.709-6, Section 3: Signal Format, 'Derivation of luminance signal'
float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// Calculates probability of selecting BRDF (specular or diffuse) using the approximate Fresnel term
float Brdf_GetSpecularProbability(float VdotN, float3 specularF0, float3 albedo)
{
    // Evaluate Fresnel term using the shading normal
    // Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel term here. That's suboptimal for rough surfaces at grazing angles, but half-vector is yet unknown at this point
    float diffuseReflectance = Luminance(albedo);

    // Approximate relative contribution of BRDFs using the Fresnel term (specular == fresnel)
    float fresnel = saturate(Luminance(Brdf_FresnelSchlick(specularF0, saturate(VdotN))));
    float specular = fresnel;
    float diffuse = diffuseReflectance * (1.0f - fresnel); //< If diffuse term is weighted by Fresnel, apply it here as well

    // Return probability of selecting specular BRDF over diffuse BRDF
    float probability = (specular / max(0.0001f, (specular + diffuse)));
    return clamp(probability, 0.1f, 0.9f);
}

// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
float3 CosineSampleHemisphere(float2 u, out float pdf)
{
    float a = sqrt(u.x);
    float b = PI_TWO * u.y;
    float3 result = float3(a * cos(b), a * sin(b), sqrt(1.0f - u.x));
    pdf = result.z * PI_INV;
    return result;
}

// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
float3 CosineSampleHemisphere(float2 u)
{
    float a = sqrt(u.x);
    float b = PI_TWO * u.y;
    float3 result = float3(a * cos(b), a * sin(b), sqrt(1.0f - u.x));
    return result;
}