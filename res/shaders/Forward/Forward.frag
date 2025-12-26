#version 430 core

layout(location = 0) out vec4 o_Color;

in VS_OUT
{
    vec3 wPos;
    vec2 uv;
    vec3 wN;
    vec3 wT;
    vec3 wB;
} f;

struct GpuLight
{
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
    ivec4 meta;
};

layout(std430, binding = 0) readonly buffer LightsBuf
{
    GpuLight lights[];
};

layout(std430, binding = 1) readonly buffer TileIndexBuf
{
    uvec2 tileIndex[];
};

layout(std430, binding = 2) readonly buffer TileListBuf
{
    uint tileList[];
};

uniform uvec2 u_TileCount;
uniform uint u_TileSize;

uniform vec3 u_CameraPos;

uniform int u_HasMaterial;
uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;
uniform float u_NormalStrength;
uniform int u_MaterialTexMask;

uniform int u_AlphaTest;
uniform float u_AlphaCutoff;
uniform int u_ManualSRGB;

uniform sampler2D u_AlbedoTex;
uniform sampler2D u_NormalTex;
uniform sampler2D u_MetallicTex;
uniform sampler2D u_RoughnessTex;
uniform sampler2D u_EmissiveTex;
uniform sampler2D u_OcclusionTex;
uniform sampler2D u_HeightTex;
uniform sampler2D u_ArmTex;

uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D u_BRDFLUT;
uniform int u_HasIBL;
uniform float u_IBLIntensity;

uniform int u_DebugLod;

const float PI = 3.14159265358979323846;

vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 normal_from_map(vec3 nGeom)
{
    if ((u_MaterialTexMask & (1 << 1)) == 0)
        return normalize(nGeom);

    vec3 t = normalize(f.wT);
    vec3 b = normalize(f.wB);
    vec3 n = normalize(nGeom);
    mat3 TBN = mat3(t, b, n);

    vec3 nm = texture(u_NormalTex, f.uv).xyz * 2.0 - 1.0;
    nm.xy *= u_NormalStrength;
    nm = normalize(nm);

    return normalize(TBN * nm);
}

float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotV, float k)
{
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float k)
{
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    vec3 Fr = max(vec3(1.0 - roughness), F0);
    return F0 + (Fr - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 sample_prefilter(vec3 R, float roughness)
{
    float maxMip = 5.0;
    float mip = roughness * maxMip;
    return textureLod(u_PrefilterMap, R, mip).rgb;
}

void main()
{
    vec4 albedoTex = vec4(1.0);
    if ((u_MaterialTexMask & (1 << 0)) != 0)
        albedoTex = texture(u_AlbedoTex, f.uv);

    vec3 baseColor = u_Albedo * albedoTex.rgb;
    if (u_ManualSRGB != 0 && (u_MaterialTexMask & (1 << 0)) != 0)
        baseColor = srgb_to_linear(baseColor);

    float alpha = u_Opacity * albedoTex.a;

    if (u_AlphaTest != 0)
    {
        if (alpha < u_AlphaCutoff)
            discard;
    }

    float roughness = u_Roughness;
    float metallic = u_Metallic;
    float ao = 1.0;

    if ((u_MaterialTexMask & (1 << 7)) != 0)
    {
        vec3 arm = texture(u_ArmTex, f.uv).rgb;
        ao = arm.r;
        roughness *= arm.g;
        metallic *= arm.b;
    }
    else
    {
        if ((u_MaterialTexMask & (1 << 5)) != 0)
            ao = texture(u_OcclusionTex, f.uv).r;

        if ((u_MaterialTexMask & (1 << 3)) != 0)
            roughness *= texture(u_RoughnessTex, f.uv).r;

        if ((u_MaterialTexMask & (1 << 2)) != 0)
            metallic *= texture(u_MetallicTex, f.uv).r;
    }

    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    ao = clamp(ao, 0.0, 1.0);

    vec3 emissive = u_Emissive;
    if ((u_MaterialTexMask & (1 << 4)) != 0)
    {
        vec3 e = texture(u_EmissiveTex, f.uv).rgb;
        if (u_ManualSRGB != 0)
            e = srgb_to_linear(e);
        emissive *= e;
    }

    vec3 N = normal_from_map(f.wN);
    vec3 V = normalize(u_CameraPos - f.wPos);

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    uvec2 tile = uvec2(gl_FragCoord.xy) / u_TileSize;
    tile.x = min(tile.x, u_TileCount.x - 1u);
    tile.y = min(tile.y, u_TileCount.y - 1u);

    uint tileId = tile.y * u_TileCount.x + tile.x;
    uvec2 oc = tileIndex[tileId];
    uint off = oc.x;
    uint cnt = oc.y;

    vec3 Lo = vec3(0.0);

    float NdotV = max(dot(N, V), 0.0);
    float a = roughness * roughness;
    float k = (roughness + 1.0);
    k = (k * k) * 0.125;

    for (uint i = 0u; i < cnt; ++i)
    {
        uint li = tileList[off + i];
        GpuLight Lg = lights[li];

        int type = Lg.meta.x;

        vec3 Ldir = vec3(0.0);
        vec3 radiance = vec3(0.0);

        if (type == 1)
        {
            Ldir = normalize(-Lg.direction.xyz);
            radiance = Lg.color.rgb * Lg.params.x;
        }
        else
        {
            vec3 toL = Lg.position.xyz - f.wPos;
            float dist = length(toL);
            float range = max(Lg.params.z, 0.0001);
            if (dist > range)
                continue;

            Ldir = toL / max(dist, 1e-6);

            float att = 1.0 - saturate(dist / range);
            att = att * att;
            att = att / (1.0 + dist * dist);

            radiance = Lg.color.rgb * Lg.params.x * att;
        }

        float NdotL = max(dot(N, Ldir), 0.0);
        if (NdotL <= 0.0)
            continue;

        vec3 H = normalize(V + Ldir);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        float D = D_GGX(NdotH, a);
        float G = G_Smith(NdotV, NdotL, k);
        vec3 F = F_Schlick(VdotH, F0);

        vec3 num = D * G * F;
        float denom = max(4.0 * NdotV * NdotL, 1e-6);
        vec3 spec = num / denom;

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 diff = kD * baseColor / PI;

        Lo += (diff + spec) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.0);

    if (u_HasIBL != 0)
    {
        vec3 F = F_SchlickRoughness(NdotV, F0, roughness);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuse = irradiance * baseColor;

        vec3 R = reflect(-V, N);
        vec3 prefiltered = sample_prefilter(R, roughness);
        vec2 brdf = texture(u_BRDFLUT, vec2(NdotV, roughness)).rg;
        vec3 specIBL = prefiltered * (F * brdf.x + brdf.y);

        ambient = (kD * diffuse + specIBL) * ao * u_IBLIntensity;
    }
    else
    {
        ambient = baseColor * 0.03 * ao;
    }

    vec3 color = ambient + Lo + emissive;

    if (u_DebugLod != 0)
    {
        float t = clamp(float(u_DebugLod) / 6.0, 0.0, 1.0);
        vec3 dbg = vec3(t, 1.0 - t, 0.2);
        color = mix(color, dbg, 0.35);
    }

    o_Color = vec4(color, alpha);
}
