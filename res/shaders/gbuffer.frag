#version 460 core

#define MAT_TEX_ALBEDO    (1 << 0)
#define MAT_TEX_NORMAL    (1 << 1)
#define MAT_TEX_METALLIC  (1 << 2)
#define MAT_TEX_ROUGHNESS (1 << 3)
#define MAT_TEX_EMISSIVE  (1 << 4)
#define MAT_TEX_OCCLUSION (1 << 5)
#define MAT_TEX_HEIGHT    (1 << 6)
#define MAT_TEX_ARM       (1 << 7)

in VS_OUT
{
    vec3 ws_pos;
    vec3 ws_nrm;
    vec2 uv;
    mat3 tbn;
} v;

layout(location=0) out vec4 oAlbedo;
layout(location=1) out vec2 oNormal;
layout(location=2) out vec4 oMaterial;
layout(location=3) out vec4 oEmissive;

uniform int u_HasMaterial;
uniform int u_MaterialTexMask;

uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;

uniform float u_NormalStrength;
uniform float u_HeightScale;
uniform int u_HeightSteps;

uniform sampler2D u_AlbedoTex;
uniform sampler2D u_NormalTex;
uniform sampler2D u_MetallicTex;
uniform sampler2D u_RoughnessTex;
uniform sampler2D u_EmissiveTex;
uniform sampler2D u_OcclusionTex;
uniform sampler2D u_HeightTex;
uniform sampler2D u_ArmTex;

uniform int u_AlphaTest;
uniform float u_AlphaCutoff;
uniform int u_HeightInvert;
uniform int u_ManualSRGB;

uniform vec3 u_CameraPos;

uniform int u_DebugLod;

vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

float sample_height(vec2 uv)
{
    float h = texture(u_HeightTex, uv).r;
    if (u_HeightInvert != 0) h = 1.0 - h;
    return h;
}

vec2 parallax_uv(vec2 uv, vec3 v_ts)
{
    if ((u_MaterialTexMask & MAT_TEX_HEIGHT) == 0) return uv;
    if (u_HeightScale <= 0.0) return uv;

    int steps = max(u_HeightSteps, 1);
    float layer = 1.0 / float(steps);

    vec2 p = (v_ts.xy / max(v_ts.z, 1e-3)) * u_HeightScale;
    vec2 duv = p * layer;

    float cur = 0.0;
    vec2 u = uv;

    for (int i = 0; i < steps; ++i)
    {
        float h = sample_height(u);
        if (cur >= h) break;
        u -= duv;
        cur += layer;
    }
    return u;
}

float sign_not_zero(float k)
{
    return (k >= 0.0) ? 1.0 : -1.0;
}

vec2 oct_encode(vec3 n)
{
    n = normalize(n);
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);

    vec2 e = n.xy;

    if (n.z < 0.0)
        e = (1.0 - abs(e.yx)) * vec2(sign_not_zero(e.x), sign_not_zero(e.y));

    return e;
}

vec3 lod_color(int l)
{
    if (l <= 0) return vec3(0.2, 1.0, 0.2);
    if (l == 1) return vec3(0.2, 0.6, 1.0);
    if (l == 2) return vec3(1.0, 0.8, 0.2);
    if (l == 3) return vec3(1.0, 0.2, 0.2);
    if (l == 4) return vec3(0.9, 0.2, 1.0);
    if (l == 5) return vec3(0.2, 1.0, 1.0);
    return vec3(0.8, 0.8, 0.8);
}

void main()
{
    if (u_DebugLod != 0)
    {
        vec3 c = lod_color(u_DebugLod - 1);

        oAlbedo   = vec4(c, 1.0);
        oNormal   = vec2(0.5, 0.5);
        oMaterial = vec4(1.0, 0.0, 1.0, 0.0);
        oEmissive = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 V = normalize(u_CameraPos - v.ws_pos);
    vec3 v_ts = normalize(transpose(v.tbn) * V);

    vec2 uv = v.uv;
    if (u_HasMaterial != 0)
        uv = parallax_uv(uv, v_ts);

    vec4 base = vec4(u_Albedo, 1.0);
    if ((u_MaterialTexMask & MAT_TEX_ALBEDO) != 0)
        base = texture(u_AlbedoTex, uv);

    if (u_ManualSRGB != 0)
        base.rgb = srgb_to_linear(base.rgb);

    float opacity = u_Opacity;
    if (u_HasMaterial != 0)
        opacity *= base.a;

    if (u_AlphaTest != 0 && opacity < u_AlphaCutoff)
        discard;

    vec3 N = normalize(v.ws_nrm);

    if ((u_MaterialTexMask & MAT_TEX_NORMAL) != 0)
    {
        vec3 tn = texture(u_NormalTex, uv).xyz * 2.0 - 1.0;
        tn.xy *= max(u_NormalStrength, 0.0);
        tn = normalize(tn);
        N = normalize(v.tbn * tn);
    }

    float rough = clamp(u_Roughness, 0.02, 1.0);
    float metal = clamp(u_Metallic, 0.0, 1.0);
    float ao = 1.0;

    if ((u_MaterialTexMask & MAT_TEX_ROUGHNESS) != 0)
        rough = clamp(texture(u_RoughnessTex, uv).r, 0.02, 1.0);

    if ((u_MaterialTexMask & MAT_TEX_METALLIC) != 0)
        metal = clamp(texture(u_MetallicTex, uv).r, 0.0, 1.0);

    if ((u_MaterialTexMask & MAT_TEX_OCCLUSION) != 0)
        ao = clamp(texture(u_OcclusionTex, uv).r, 0.0, 1.0);

    vec3 emiss = u_Emissive;
    if ((u_MaterialTexMask & MAT_TEX_EMISSIVE) != 0)
        emiss *= texture(u_EmissiveTex, uv).rgb;

    float emissI = max(max(emiss.r, emiss.g), emiss.b);

    oAlbedo   = vec4(base.rgb, opacity);

    vec2 oct  = oct_encode(N);
    oNormal   = oct * 0.5 + 0.5;

    oMaterial = vec4(rough, metal, ao, emissI);
    oEmissive = vec4(emiss, 1.0);
}
