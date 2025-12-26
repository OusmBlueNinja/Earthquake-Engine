#version 430 core

layout(location = 0) out vec4 o_Color;

in VS_OUT
{
    vec3 worldPos;
    vec3 worldN;
    vec2 uv;
    vec4 tangent;
} v;

uniform vec3 u_CameraPos;

uniform int u_HasMaterial;

uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;

uniform float u_NormalStrength;
uniform float u_HeightScale;
uniform int u_HeightSteps;

uniform int u_MaterialTexMask;

uniform sampler2D u_AlbedoTex;
uniform sampler2D u_NormalTex;
uniform sampler2D u_MetallicTex;
uniform sampler2D u_RoughnessTex;
uniform sampler2D u_EmissiveTex;
uniform sampler2D u_OcclusionTex;
uniform sampler2D u_HeightTex;
uniform sampler2D u_ArmTex;

uniform int u_HeightInvert;
uniform int u_AlphaTest;
uniform float u_AlphaCutoff;
uniform int u_ManualSRGB;

uniform int u_HasIBL;
uniform float u_IBLIntensity;
uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D u_BRDFLUT;

uniform int u_TileSize;
uniform int u_TileCountX;
uniform int u_TileCountY;

uniform int u_DebugLod;

struct GPU_Light
{
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
    ivec4 meta;
};

layout(std430, binding = 0) readonly buffer B_Lights
{
    GPU_Light g_Lights[];
};

layout(std430, binding = 1) readonly buffer B_TileIndex
{
    uvec2 g_TileIndex[];
};

layout(std430, binding = 2) readonly buffer B_TileList
{
    uint g_TileList[];
};

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 sample_albedo(vec2 uv)
{
    vec4 t = texture(u_AlbedoTex, uv);
    vec3 c = t.rgb;
    if (u_ManualSRGB != 0)
        c = srgb_to_linear(c);
    return c;
}

vec3 sample_emissive(vec2 uv)
{
    vec3 c = texture(u_EmissiveTex, uv).rgb;
    if (u_ManualSRGB != 0)
        c = srgb_to_linear(c);
    return c;
}

vec3 tangent_space_normal(vec2 uv)
{
    vec3 n = texture(u_NormalTex, uv).xyz * 2.0 - 1.0;
    n.xy *= u_NormalStrength;
    return normalize(n);
}

mat3 build_tbn(vec3 N, vec4 tangent)
{
    vec3 T = normalize(tangent.xyz);
    vec3 B = normalize(cross(N, T)) * tangent.w;
    T = normalize(T - N * dot(N, T));
    B = normalize(cross(N, T)) * tangent.w;
    return mat3(T, B, N);
}

float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-8);
}

float G_SchlickGGX(float NoV, float k)
{
    return NoV / max(NoV * (1.0 - k) + k, 1e-8);
}

float G_Smith(float NoV, float NoL, float a)
{
    float r = a + 1.0;
    float k = (r * r) / 8.0;
    return G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
}

vec3 F_Schlick(vec3 F0, float VoH)
{
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

vec2 parallax_uv(vec2 uv, vec3 V_ts)
{
    int steps = u_HeightSteps;
    float scale = u_HeightScale;
    if (steps < 1 || scale <= 0.0)
        return uv;

    float layerCount = float(steps);
    float layerDepth = 1.0 / layerCount;
    float curDepth = 0.0;

    vec2 P = (V_ts.xy / max(V_ts.z, 1e-4)) * scale;
    vec2 dUV = P / layerCount;

    vec2 curUV = uv;
    float h = texture(u_HeightTex, curUV).r;
    if (u_HeightInvert != 0)
        h = 1.0 - h;

    while (curDepth < h)
    {
        curUV -= dUV;
        curDepth += layerDepth;
        h = texture(u_HeightTex, curUV).r;
        if (u_HeightInvert != 0)
            h = 1.0 - h;
    }

    return curUV;
}

vec3 debug_lod_tint(vec3 c, int d)
{
    if (d <= 0)
        return c;
    int m = (d - 1) % 6;
    vec3 t = vec3(1.0);
    if (m == 0) t = vec3(1.0, 0.3, 0.3);
    if (m == 1) t = vec3(0.3, 1.0, 0.3);
    if (m == 2) t = vec3(0.3, 0.3, 1.0);
    if (m == 3) t = vec3(1.0, 1.0, 0.3);
    if (m == 4) t = vec3(1.0, 0.3, 1.0);
    if (m == 5) t = vec3(0.3, 1.0, 1.0);
    return mix(c, c * t, 0.35);
}

void main()
{
    vec3 Nw = normalize(v.worldN);
    vec3 Vw = normalize(u_CameraPos - v.worldPos);

    mat3 TBN = build_tbn(Nw, v.tangent);

    vec2 uv = v.uv;

    if ((u_MaterialTexMask & (1 << 6)) != 0 && u_HeightScale > 0.0 && u_HeightSteps > 0)
    {
        vec3 Vts = normalize(transpose(TBN) * Vw);
        uv = parallax_uv(uv, Vts);
    }

    vec3 albedo = u_Albedo;
    float alpha_tex = 1.0;

    if ((u_MaterialTexMask & (1 << 0)) != 0)
    {
        vec4 t = texture(u_AlbedoTex, uv);
        albedo *= (u_ManualSRGB != 0) ? srgb_to_linear(t.rgb) : t.rgb;
        alpha_tex = t.a;
    }

    float alpha = alpha_tex * u_Opacity;

    if (u_AlphaTest != 0)
    {
        if (alpha < u_AlphaCutoff)
            discard;
        alpha = 1.0;
    }

    float roughness = clamp(u_Roughness, 0.04, 1.0);
    float metallic = clamp(u_Metallic, 0.0, 1.0);
    float ao = 1.0;

    if ((u_MaterialTexMask & (1 << 7)) != 0)
    {
        vec3 arm = texture(u_ArmTex, uv).rgb;
        ao *= arm.r;
        roughness *= arm.g;
        metallic *= arm.b;
        roughness = clamp(roughness, 0.04, 1.0);
        metallic = clamp(metallic, 0.0, 1.0);
    }
    else
    {
        if ((u_MaterialTexMask & (1 << 3)) != 0)
        {
            roughness *= texture(u_RoughnessTex, uv).r;
            roughness = clamp(roughness, 0.04, 1.0);
        }
        if ((u_MaterialTexMask & (1 << 2)) != 0)
        {
            metallic *= texture(u_MetallicTex, uv).r;
            metallic = clamp(metallic, 0.0, 1.0);
        }
    }

    if ((u_MaterialTexMask & (1 << 5)) != 0)
        ao *= texture(u_OcclusionTex, uv).r;

    vec3 emissive = u_Emissive;
    if ((u_MaterialTexMask & (1 << 4)) != 0)
        emissive *= ((u_ManualSRGB != 0) ? srgb_to_linear(texture(u_EmissiveTex, uv).rgb) : texture(u_EmissiveTex, uv).rgb);

    vec3 N = Nw;
    if ((u_MaterialTexMask & (1 << 1)) != 0)
    {
        vec3 Nts = tangent_space_normal(uv);
        N = normalize(TBN * Nts);
    }

    float NoV = saturate(dot(N, Vw));

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    ivec2 tileXY = ivec2(int(gl_FragCoord.x) / max(u_TileSize, 1), int(gl_FragCoord.y) / max(u_TileSize, 1));
    tileXY.x = clamp(tileXY.x, 0, max(u_TileCountX - 1, 0));
    tileXY.y = clamp(tileXY.y, 0, max(u_TileCountY - 1, 0));
    int tileIndex = tileXY.x + tileXY.y * u_TileCountX;

    uvec2 sc = g_TileIndex[uint(tileIndex)];
    uint start = sc.x;
    uint count = sc.y;
    if (count > 2048u)
        count = 2048u;

    for (uint ii = 0u; ii < count; ++ii)
    {
        uint li = g_TileList[start + ii];
        GPU_Light Ld = g_Lights[li];

        vec3 L;
        float atten = 1.0;

        if (Ld.meta.x == 1)
        {
            L = normalize(-Ld.direction.xyz);
            atten = 1.0;
        }
        else
        {
            vec3 toL = Ld.position.xyz - v.worldPos;
            float d2 = dot(toL, toL);
            float d = sqrt(max(d2, 1e-8));
            L = toL / d;

            float range = Ld.params.z;
            if (range > 1e-4)
            {
                float x = saturate(1.0 - d / range);
                atten = x * x;
            }
            else
            {
                atten = 1.0 / max(d2, 1e-4);
            }
        }

        float NoL = saturate(dot(N, L));
        if (NoL <= 1e-5)
            continue;

        vec3 H = normalize(Vw + L);
        float NoH = saturate(dot(N, H));
        float VoH = saturate(dot(Vw, H));

        float a = roughness * roughness;

        float D = D_GGX(NoH, a);
        float G = G_Smith(NoV, NoL, a);
        vec3  F = F_Schlick(F0, VoH);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 radiance = Ld.color.rgb * Ld.params.x * atten;

        vec3 spec = (D * G * F) / max(4.0 * NoV * NoL, 1e-6);
        vec3 diff = kD * (albedo / 3.14159265);

        Lo += (diff + spec) * radiance * NoL;
    }

    vec3 ambient = vec3(0.0);

    if (u_HasIBL != 0)
    {
        vec3 R = reflect(-Vw, N);

        vec3 F = F_Schlick(F0, NoV);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuse = irradiance * albedo;

        float maxLod = 8.0;
        vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * maxLod).rgb;
        vec2 brdf = texture(u_BRDFLUT, vec2(NoV, roughness)).rg;
        vec3 specular = prefiltered * (F * brdf.x + brdf.y);

        ambient = (kD * diffuse + specular) * ao * u_IBLIntensity;
    }

    vec3 color = ambient + Lo + emissive;

    color = debug_lod_tint(color, u_DebugLod);

    float exposure = 1.0;
    vec3 mapped = vec3(1.0) - exp(-color * exposure);

    o_Color = vec4(mapped, alpha);
}
