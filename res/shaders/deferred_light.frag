#version 460 core

#ifndef MAX_LIGHTS
#define MAX_LIGHTS 16
#endif

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

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

in vec2 v_UV;
layout(location=0) out vec4 o_Color;

uniform sampler2D u_GAlbedo;
uniform sampler2D u_GNormal;
uniform sampler2D u_GMaterial;
uniform sampler2D u_GEmissive;
uniform sampler2D u_GDepth;

uniform mat4 u_InvView;
uniform mat4 u_InvProj;
uniform vec3 u_CameraPos;

uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D u_BRDFLUT;
uniform int u_HasIBL;
uniform float u_IBLIntensity;

uniform float u_ReflectionSharpness;

vec3 reconstruct_view_pos(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 vpos = u_InvProj * ndc;
    return vpos.xyz / max(vpos.w, 1e-6);
}

vec3 view_to_world(vec3 vp)
{
    vec4 wp = u_InvView * vec4(vp, 1.0);
    return wp.xyz;
}

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
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
    float gv = G_SchlickGGX(NdotV, k);
    float gl = G_SchlickGGX(NdotL, k);
    return gv * gl;
}

vec3 oct_decode(vec2 e)
{
    e = e * 2.0 - 1.0;

    vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    float t = clamp(-n.z, 0.0, 1.0);

    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;

    return normalize(n);
}


void main()
{
    vec4 a = texture(u_GAlbedo, v_UV);
    vec3 albedo = a.rgb;

    vec2 ne = texture(u_GNormal, v_UV).rg;
    vec2 enc = texture(u_GNormal, v_UV).xy;
    vec3 N = oct_decode(enc);

    vec4 mm = texture(u_GMaterial, v_UV);
    float rough = clamp(mm.r, 0.02, 1.0);
    float metal = clamp(mm.g, 0.0, 1.0);
    float ao = clamp(mm.b, 0.0, 1.0);
    float emissI = max(mm.a, 0.0);

    vec3 emissive = texture(u_GEmissive, v_UV).rgb * emissI;

    float depth = texture(u_GDepth, v_UV).r;
    if (depth >= 1.0)
    {
        o_Color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 view_pos = reconstruct_view_pos(v_UV, depth);
    vec3 P = view_to_world(view_pos);
    vec3 V = normalize(u_CameraPos - P);

    vec3 F0 = mix(vec3(0.04), albedo, metal);

    vec3 Lo = vec3(0.0);

    int light_count = clamp(u_LB.header.x, 0, MAX_LIGHTS);

    for (int i = 0; i < light_count; ++i)
    {
        GpuLight Lg = u_LB.lights[i];
        int type = Lg.meta.x;

        vec3 Ldir;
        float atten = 1.0;

        if (type == LIGHT_DIRECTIONAL)
        {
            vec3 d = Lg.direction.xyz;
            float dl2 = dot(d, d);
            if (dl2 < 1e-8) d = vec3(0.0, -1.0, 0.0);
            Ldir = normalize(-d);
        }
        else
        {
            vec3 toL = Lg.position.xyz - P;
            float dist = length(toL);
            if (dist < 1e-4)
                continue;
            Ldir = toL / dist;

            float range = max(Lg.params.z, 0.0);
            if (range > 0.0)
                atten *= saturate(1.0 - dist / range);

            float radius = max(Lg.params.y, 0.0);
            if (radius > 0.0)
                atten *= 1.0 / (1.0 + (dist * dist) / (radius * radius));
        }

        float NdotL = saturate(dot(N, Ldir));
        if (NdotL <= 0.0)
            continue;

        vec3 radiance = Lg.color.rgb * Lg.params.x * atten;

        vec3 H = normalize(V + Ldir);
        float NdotV = saturate(dot(N, V));
        float NdotH = saturate(dot(N, H));
        float VdotH = saturate(dot(V, H));

        float aR = max(rough * rough, 0.02);
        float D = D_GGX(NdotH, aR);
        float G = G_Smith(NdotV, NdotL, rough);
        vec3 F = fresnel_schlick(VdotH, F0);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metal);

        vec3 diffuse = kD * albedo / 3.14159265;
        vec3 specular = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-6);

        Lo += (diffuse + specular) * radiance * NdotL;
    }

    vec3 ambient;

    if (u_HasIBL != 0)
    {
        float NdotV = saturate(dot(N, V));
        vec3 R = reflect(-V, N);
        R.y = -R.y;

        vec3 F = fresnel_schlick_roughness(NdotV, F0, rough);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metal);

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;

        int mips = textureQueryLevels(u_PrefilterMap);
        float maxMip = float(max(mips - 1, 0));
        float lod = rough * maxMip;

        float sharp = max(u_ReflectionSharpness, 0.0);
        lod = lod / (1.0 + sharp);

        vec3 prefiltered = textureLod(u_PrefilterMap, R, lod).rgb;
        vec2 brdf = texture(u_BRDFLUT, vec2(NdotV, rough)).rg;
        vec3 specIBL = prefiltered * (F * brdf.x + brdf.y);

        ambient = (kD * diffuseIBL + specIBL) * ao * u_IBLIntensity;
    }
    else
    {
        ambient = albedo * 0.02 * ao;
    }

    vec3 color = emissive + ambient + Lo;

    o_Color = vec4(color, 1.0);
}
