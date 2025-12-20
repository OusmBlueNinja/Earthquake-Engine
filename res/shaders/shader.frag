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

in vec3 v_FragPos;
in vec3 v_Normal;
in vec2 v_TexCoord;

out vec4 FragColor;

bool HAS(int bit)
{
    return (u_MaterialTexMask & bit) != 0;
}

float checker2(vec2 uv)
{
    vec2 c = floor(uv);
    return mod(c.x + c.y, 2.0);
}

vec3 no_material_pattern(vec3 p, vec3 n)
{
    vec3 an = abs(n);
    an *= 1.0 / (an.x + an.y + an.z + 1e-6);

    float scale = 6.0;

    float cx = checker2(p.zy * scale);
    float cy = checker2(p.xz * scale);
    float cz = checker2(p.xy * scale);

    float c = cx * an.x + cy * an.y + cz * an.z;

    return mix(vec3(0.03), vec3(1.0, 0.0, 0.7), c);
}

float range_atten(float dist, float r)
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

float spec_power_from_roughness(float rough)
{
    float r = clamp(rough, 0.02, 1.0);
    float p = 2.0 / (r * r) - 2.0;
    return clamp(p, 2.0, 2048.0);
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
    B = normalize(cross(n, T)) * (dot(cross(n, T), B) < 0.0 ? -1.0 : 1.0);

    return mat3(T, B, n);
}

vec2 parallax_uv(vec3 Nw, vec3 P, vec2 uv, vec3 viewDirW)
{
    if (!HAS(MAT_TEX_HEIGHT)) return uv;

    float hs = max(u_HeightScale, 0.0);
    if (hs <= 0.0) return uv;

    mat3 TBN = tbn_from_derivatives(Nw, P, uv);
    vec3 Vt = normalize(transpose(TBN) * viewDirW);

    float vz = max(abs(Vt.z), 0.08);
    int steps = clamp(u_HeightSteps, 4, 64);

    float layer = 1.0 / float(steps);
    float depth = 0.0;

    vec2 dir = (Vt.xy / vz) * hs;
    vec2 delta = dir * layer;

    vec2 u = uv;
    float h = texture(u_HeightTex, u).r;
    float prevDepth = 0.0;
    float prevH = h;

    for (int i = 0; i < steps; ++i)
    {
        depth += layer;
        u -= delta;
        h = texture(u_HeightTex, u).r;
        if (depth >= h)
            break;
        prevDepth = depth;
        prevH = h;
    }

    float after = h - depth;
    float before = prevH - prevDepth;
    float w = before / (before - after + 1e-6);

    return mix(u, u + delta, clamp(w, 0.0, 1.0));
}

vec3 sample_normal_world(vec3 Nw, vec3 P, vec2 uv)
{
    if (!HAS(MAT_TEX_NORMAL)) return normalize(Nw);

    mat3 TBN = tbn_from_derivatives(Nw, P, uv);
    vec3 nt = texture(u_NormalTex, uv).xyz * 2.0 - 1.0;

    nt.xy *= max(u_NormalStrength, 0.0);
    nt = normalize(nt);

    return normalize(TBN * nt);
}

vec4 sample_albedo_rgba(vec2 uv)
{
    vec4 a = vec4(u_Albedo, 1.0);
    if (HAS(MAT_TEX_ALBEDO)) a *= texture(u_AlbedoTex, uv);
    return a;
}

vec3 sample_emissive(vec2 uv)
{
    vec3 e = u_Emissive;
    if (HAS(MAT_TEX_EMISSIVE)) e += texture(u_EmissiveTex, uv).rgb;
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

vec3 calculate_light(GpuLight L, vec3 normal, vec3 viewDir, vec3 fragPos, float rough)
{
    int type = L.meta.x;

    vec3 pos = L.position.xyz;
    vec3 dirRaw = L.direction.xyz;
    vec3 col = L.color.xyz;

    float intensity = L.params.x;
    float radius = L.params.y;
    float range = L.params.z;

    vec3 lightDir;
    float atten = 1.0;
    float spot = 1.0;

    if (type == LIGHT_DIRECTIONAL)
    {
        vec3 d = dirRaw;
        float dl2 = dot(d, d);
        if (dl2 < 1e-8) d = vec3(0.0, -1.0, 0.0);
        lightDir = normalize(-d);
    }
    else
    {
        vec3 V = pos - fragPos;
        float dist2 = dot(V, V);
        if (dist2 < 1e-8) return vec3(0.0);

        float invDist = inversesqrt(dist2);
        float dist = dist2 * invDist;

        lightDir = V * invDist;

        float reach = (type == LIGHT_SPOT) ? range : radius;
        atten = range_atten(dist, reach);
        if (atten <= 0.0) return vec3(0.0);

        if (type == LIGHT_SPOT)
        {
            spot = spot_factor(fragPos, pos, dirRaw, radius, range);
            if (spot <= 0.0) return vec3(0.0);
        }
    }

    float ndl = max(dot(normal, lightDir), 0.0);
    if (ndl <= 0.0) return vec3(0.0);

    vec3 radiance = col * (intensity * atten * spot);

    vec3 h = normalize(lightDir + viewDir);
    float specPow = spec_power_from_roughness(rough);
    float spec = pow(max(dot(normal, h), 0.0), specPow);

    return radiance * (ndl + spec * 0.25);
}

void main()
{
    vec3 Nw_geom = normalize(v_Normal);

    if (u_HasMaterial == 0)
    {
        FragColor = vec4(no_material_pattern(v_FragPos, Nw_geom), 1.0);
        return;
    }

    vec3 Vw = normalize(u_CameraPos - v_FragPos);

    vec2 uv = parallax_uv(Nw_geom, v_FragPos, v_TexCoord, Vw);

    vec4 albedoRGBA = sample_albedo_rgba(uv);
    vec3 base = albedoRGBA.rgb;

    float metallic = sample_metallic(uv);
    float rough = sample_roughness(uv);
    vec3 emiss = sample_emissive(uv);
    float ao = sample_ao(uv);

    vec3 Nw = sample_normal_world(Nw_geom, v_FragPos, uv);

    vec3 F0 = mix(vec3(0.04), base, metallic);
    vec3 ambient = base * 0.06 * ao;

    vec3 color = emiss + ambient;

    int lc = clamp(u_LB.header.x, 0, MAX_LIGHTS);
    for (int i = 0; i < lc; ++i)
    {
        vec3 lit = calculate_light(u_LB.lights[i], Nw, Vw, v_FragPos, rough);
        vec3 diff = (1.0 - metallic) * base;
        vec3 specTint = F0;
        color += lit * (diff + specTint * 0.35);
    }

    float alpha = clamp(u_Opacity * albedoRGBA.a, 0.0, 1.0);
    FragColor = vec4(color, alpha);
}
