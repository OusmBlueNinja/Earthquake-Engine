#version 460 core

#ifndef MAX_LIGHTS
#define MAX_LIGHTS 16
#endif

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

#define MAT_TEX_ALBEDO    (1 << 0)
#define MAT_TEX_NORMAL    (1 << 1)
#define MAT_TEX_METALLIC  (1 << 2)
#define MAT_TEX_ROUGHNESS (1 << 3)
#define MAT_TEX_EMISSIVE  (1 << 4)
#define MAT_TEX_OCCLUSION (1 << 5)
#define MAT_TEX_HEIGHT    (1 << 6)
#define MAT_TEX_ARM       (1 << 7)

struct GpuLight
{
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
    ivec4 meta;
};

layout(std140, binding = 0) uniform LightsBlock
{
    ivec4 header;
    GpuLight lights[MAX_LIGHTS];
} u_LB;

uniform vec3 u_CameraPos;

uniform int u_HasMaterial;
uniform int u_MaterialTexMask;

uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;

uniform sampler2D u_AlbedoTex;
uniform sampler2D u_NormalTex;
uniform sampler2D u_MetallicTex;
uniform sampler2D u_RoughnessTex;
uniform sampler2D u_EmissiveTex;
uniform sampler2D u_OcclusionTex;
uniform sampler2D u_HeightTex;
uniform sampler2D u_ArmTex;

uniform float u_HeightScale;
uniform int u_HeightSteps;
uniform float u_NormalStrength;

uniform int u_HeightInvert;
uniform int u_AlphaTest;
uniform float u_AlphaCutoff;

uniform int u_ManualSRGB;

uniform float u_HeightBias;
uniform float u_HeightContrast;

uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D u_BRDFLUT;
uniform int u_HasIBL;
uniform float u_IBLIntensity;

uniform int u_IsTransparent;

uniform sampler2D u_SceneColor;
uniform vec2 u_SceneInvSize;
uniform float u_Transmission;

in vec3 v_FragPos;
in vec3 v_Normal;
in vec2 v_TexCoord;

out vec4 FragColor;

bool HAS(int bit)
{
    return (u_MaterialTexMask & bit) != 0;
}

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

float range_fade(float dist, float r)
{
    float x = clamp(1.0 - dist / max(r, 1e-6), 0.0, 1.0);
    return x * x;
}

float spot_factor(vec3 fragPos, vec3 lightPos, vec3 lightDirRaw, float radius, float range)
{
    vec3 sd = lightDirRaw;
    float sd2 = dot(sd, sd);
    if (sd2 < 1e-8) sd = vec3(0.0, -1.0, 0.0);
    sd = normalize(sd);

    vec3 lightToFrag = normalize(fragPos - lightPos);
    float theta = dot(lightToFrag, sd);

    float r = max(radius, 1e-4);
    float d = max(range, 1e-4);

    float outerAng = atan(r, d);
    float innerAng = outerAng * 0.75;

    float outer = cos(outerAng);
    float inner = cos(innerAng);

    return clamp((theta - outer) / max(inner - outer, 1e-6), 0.0, 1.0);
}

mat3 tbn_from_derivatives(vec3 N, vec3 P, vec2 UV)
{
    vec3 dp1 = dFdx(P);
    vec3 dp2 = dFdy(P);
    vec2 duv1 = dFdx(UV);
    vec2 duv2 = dFdy(UV);

    vec3 T = dp1 * duv2.y - dp2 * duv1.y;
    vec3 B = dp2 * duv1.x - dp1 * duv2.x;

    float invMax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-20);
    T *= invMax;
    B *= invMax;

    vec3 n = normalize(N);
    T = normalize(T - n * dot(n, T));

    vec3 c = cross(n, T);
    float s = (dot(c, B) < 0.0) ? -1.0 : 1.0;
    B = normalize(c) * s;

    return mat3(T, B, n);
}

float height_sample_grad(vec2 uv, vec2 dUVdx, vec2 dUVdy)
{
    float h = textureGrad(u_HeightTex, uv, dUVdx, dUVdy).r;
    if (u_HeightInvert != 0) h = 1.0 - h;

    float b = clamp(u_HeightBias, 0.0, 1.0);
    float c = max(u_HeightContrast, 0.0);
    h = clamp((h - b) * c + b, 0.0, 1.0);

    return h;
}

vec2 parallax_uv(vec3 Nw, vec3 P, vec2 uv0, vec3 viewDirW)
{
    if (!HAS(MAT_TEX_HEIGHT)) return uv0;

    float hs = max(u_HeightScale, 0.0);
    if (hs <= 0.0) return uv0;

    mat3 TBN = tbn_from_derivatives(Nw, P, uv0);
    vec3 Vt = normalize(transpose(TBN) * viewDirW);

    if (Vt.z <= 0.0001) return uv0;

    float NdotV = clamp(Vt.z, 0.0, 1.0);
    float fade = smoothstep(0.35, 0.65, NdotV);
    if (fade <= 0.0) return uv0;

    vec2 dUVdx = dFdx(uv0);
    vec2 dUVdy = dFdy(uv0);

    int maxSteps = clamp(u_HeightSteps, 8, 96);
    int steps = int(mix(float(maxSteps), 8.0, NdotV));

    float vz = max(Vt.z, 0.20);
    vec2 dir = (Vt.xy / vz) * (hs * fade);

    dir = clamp(dir, vec2(-0.08), vec2(0.08));

    float layer = 1.0 / float(steps);
    vec2 delta = dir * layer;

    vec2 uv = uv0;
    vec2 prevUV = uv;

    float depth = 0.0;
    float prevDepth = 0.0;

    float h = height_sample_grad(uv, dUVdx, dUVdy);
    float prevH = h;

    for (int i = 0; i < steps; ++i)
    {
        if (depth >= h) break;

        prevUV = uv;
        prevDepth = depth;
        prevH = h;

        uv += delta;
        depth += layer;

        h = height_sample_grad(uv, dUVdx, dUVdy);
    }

    float after = h - depth;
    float before = prevH - prevDepth;
    float w = before / (before - after + 1e-6);

    vec2 uvHit = mix(uv, prevUV, clamp(w, 0.0, 1.0));

    if (any(lessThan(uvHit, vec2(0.0))) || any(greaterThan(uvHit, vec2(1.0))))
        return uv0;

    return uvHit;
}

vec3 sample_normal_world(vec3 Nw, vec3 P, vec2 uv)
{
    if (!HAS(MAT_TEX_NORMAL)) return normalize(Nw);

    mat3 TBN = tbn_from_derivatives(Nw, P, uv);
    vec3 nt = texture(u_NormalTex, uv).xyz * 2.0 - 1.0;

    float ns = max(u_NormalStrength, 0.0);
    nt.xy *= ns;

    nt = normalize(nt);
    return normalize(TBN * nt);
}

vec4 sample_albedo_rgba(vec2 uv)
{
    vec4 tex = HAS(MAT_TEX_ALBEDO) ? texture(u_AlbedoTex, uv) : vec4(1.0);
    if (u_ManualSRGB != 0) tex.rgb = srgb_to_linear(tex.rgb);
    return vec4(tex.rgb * u_Albedo, tex.a);
}

vec3 sample_emissive(vec2 uv)
{
    vec3 e = u_Emissive;
    if (HAS(MAT_TEX_EMISSIVE))
    {
        vec3 t = texture(u_EmissiveTex, uv).rgb;
        if (u_ManualSRGB != 0) t = srgb_to_linear(t);
        e *= t;
    }
    return e;
}

float sample_ao(vec2 uv)
{
    float ao = 1.0;
    if (HAS(MAT_TEX_OCCLUSION)) ao *= texture(u_OcclusionTex, uv).r;
    if (HAS(MAT_TEX_ARM)) ao *= texture(u_ArmTex, uv).r;
    return clamp(ao, 0.0, 1.0);
}

float sample_roughness(vec2 uv)
{
    float r = clamp(u_Roughness, 0.02, 1.0);
    if (HAS(MAT_TEX_ROUGHNESS)) r *= texture(u_RoughnessTex, uv).r;
    if (HAS(MAT_TEX_ARM)) r *= texture(u_ArmTex, uv).g;
    return clamp(r, 0.02, 1.0);
}

float sample_metallic(vec2 uv)
{
    float m = clamp(u_Metallic, 0.0, 1.0);
    if (HAS(MAT_TEX_METALLIC)) m *= texture(u_MetallicTex, uv).r;
    if (HAS(MAT_TEX_ARM)) m *= texture(u_ArmTex, uv).b;
    return clamp(m, 0.0, 1.0);
}

float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-7);
}

float G_SchlickGGX(float NdotX, float k)
{
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-7);
}

float G_Smith(float NdotV, float NdotL, float rough)
{
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

vec3 fresnel_schlick(float cosTheta, vec3 F0)
{
    float ct = clamp(cosTheta, 0.0, 1.0);
    return F0 + (1.0 - F0) * pow(1.0 - ct, 5.0);
}

vec3 fresnel_schlick_roughness(float cosTheta, vec3 F0, float rough)
{
    float ct = clamp(cosTheta, 0.0, 1.0);
    vec3 Fr = max(vec3(1.0 - rough), F0);
    return F0 + (Fr - F0) * pow(1.0 - ct, 5.0);
}

vec3 sample_scene_color(vec2 fragCoord)
{
    vec2 uv = fragCoord * u_SceneInvSize;
    return texture(u_SceneColor, uv).rgb;
}

void main()
{
    vec3 Nw_geom = normalize(v_Normal);

    if (u_HasMaterial == 0)
    {
        vec3 an = abs(Nw_geom);
        an *= 1.0 / (an.x + an.y + an.z + 1e-6);

        vec3 p = v_FragPos;
        float scale = 6.0;
        vec2 c0 = floor(p.zy * scale);
        vec2 c1 = floor(p.xz * scale);
        vec2 c2 = floor(p.xy * scale);

        float cx = mod(c0.x + c0.y, 2.0);
        float cy = mod(c1.x + c1.y, 2.0);
        float cz = mod(c2.x + c2.y, 2.0);

        float c = cx * an.x + cy * an.y + cz * an.z;
        vec3 col = mix(vec3(0.03), vec3(1.0, 0.0, 0.7), c);

        FragColor = vec4(col, 1.0);
        return;
    }

    vec3 Vw = normalize(u_CameraPos - v_FragPos);

    vec2 uv = v_TexCoord;
    uv = parallax_uv(Nw_geom, v_FragPos, uv, Vw);

    vec4 albedoRGBA = sample_albedo_rgba(uv);
    vec3 base = clamp(albedoRGBA.rgb, 0.0, 100.0);

    float alpha = clamp(u_Opacity * albedoRGBA.a, 0.0, 1.0);

    bool doAlphaTest = (u_AlphaTest != 0) && (u_IsTransparent == 0);
    if (doAlphaTest && alpha < u_AlphaCutoff)
        discard;

    float metallic = sample_metallic(uv);
    float rough = sample_roughness(uv);
    float ao = sample_ao(uv);
    vec3 emiss = sample_emissive(uv);

    vec3 Nw = sample_normal_world(Nw_geom, v_FragPos, uv);

    vec3 F0 = mix(vec3(0.04), base, metallic);
    vec3 albedoDiffuse = base * (1.0 - metallic);

    vec3 Lo = vec3(0.0);

    int lc = clamp(u_LB.header.x, 0, MAX_LIGHTS);
    for (int i = 0; i < lc; ++i)
    {
        GpuLight L = u_LB.lights[i];
        int type = L.meta.x;

        vec3 lightPos = L.position.xyz;
        vec3 lightDirRaw = L.direction.xyz;

        vec3 lightColor = L.color.xyz;
        float intensity = L.params.x;
        float radius = L.params.y;
        float range = L.params.z;

        vec3 Ldir = vec3(0.0);
        float atten = 1.0;
        float spot = 1.0;

        if (type == LIGHT_DIRECTIONAL)
        {
            vec3 d = lightDirRaw;
            float dl2 = dot(d, d);
            if (dl2 < 1e-8) d = vec3(0.0, -1.0, 0.0);
            Ldir = normalize(-d);
        }
        else
        {
            vec3 V = lightPos - v_FragPos;
            float dist2 = dot(V, V);
            if (dist2 < 1e-8) continue;

            float invDist = inversesqrt(dist2);
            float dist = dist2 * invDist;

            Ldir = V * invDist;

            float reach = (type == LIGHT_SPOT) ? max(range, 1e-4) : max(radius, 1e-4);
            atten = range_fade(dist, reach);
            if (atten <= 0.0) continue;

            if (type == LIGHT_SPOT)
            {
                spot = spot_factor(v_FragPos, lightPos, lightDirRaw, max(radius, 1e-4), max(range, 1e-4));
                if (spot <= 0.0) continue;
            }
        }

        float NdotL = max(dot(Nw, Ldir), 0.0);
        if (NdotL <= 0.0) continue;

        vec3 radiance = lightColor * (intensity * atten * spot);

        vec3 H = normalize(Vw + Ldir);

        float NdotVw = max(dot(Nw, Vw), 0.0);
        float NdotH = max(dot(Nw, H), 0.0);
        float VdotH = max(dot(Vw, H), 0.0);

        float a = max(rough * rough, 0.02);

        float D = D_GGX(NdotH, a);
        float G = G_Smith(NdotVw, NdotL, rough);
        vec3 F = fresnel_schlick(VdotH, F0);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 diffuse = (kD * albedoDiffuse) / 3.14159265;
        vec3 specular = (D * G) * F / max(4.0 * NdotVw * NdotL, 1e-6);

        Lo += (diffuse + specular) * radiance * NdotL;
    }

    vec3 ambient;

    if (u_HasIBL != 0)
    {
        float NdotV = saturate(dot(Nw, Vw));
        vec3 R = reflect(-Vw, Nw);

        R.y = -R.y;

        vec3 F = fresnel_schlick_roughness(NdotV, F0, rough);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 irradiance = texture(u_IrradianceMap, Nw).rgb;
        vec3 diffuseIBL = irradiance * albedoDiffuse;

        int mips = textureQueryLevels(u_PrefilterMap);
        float maxMip = float(max(mips - 1, 0));
        float lod = rough * maxMip;

        vec3 prefiltered = textureLod(u_PrefilterMap, R, lod).rgb;
        vec2 brdf = texture(u_BRDFLUT, vec2(NdotV, rough)).rg;
        vec3 specIBL = prefiltered * (F * brdf.x + brdf.y);

        ambient = (kD * diffuseIBL + specIBL) * ao * u_IBLIntensity;
    }
    else
    {
        ambient = albedoDiffuse * 0.03 * ao;
    }

    vec3 shaded = emiss + ambient + Lo;

    if (u_IsTransparent != 0)
    {
        vec3 scene = sample_scene_color(gl_FragCoord.xy);
        vec3 outRgb = mix(scene, shaded, alpha);
        FragColor = vec4(outRgb, alpha);
        return;
    }

    FragColor = vec4(shaded, alpha);
}
