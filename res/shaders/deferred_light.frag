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
uniform sampler2D u_GDepth;

uniform vec2 u_InvResolution;
uniform mat4 u_InvView;
uniform mat4 u_InvProj;
uniform vec3 u_CameraPos;

 vec3 decode_normal(vec4 n)
{
    vec3 nn = n.xyz * 2.0 - 1.0;
    return normalize(nn);
}

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

void main()
{
    vec3 albedo = texture(u_GAlbedo, v_UV).rgb;
    vec3 N = decode_normal(texture(u_GNormal, v_UV));
    vec4 m = texture(u_GMaterial, v_UV);
    float rough = clamp(m.r, 0.02, 1.0);
    float metal = clamp(m.g, 0.0, 1.0);
    float ao = clamp(m.b, 0.0, 1.0);

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

    int light_count = u_LB.header.x;

    for (int i = 0; i < light_count; ++i)
    {
        GpuLight Lg = u_LB.lights[i];
        int type = Lg.meta.x;

        vec3 Ldir;
        float atten = 1.0;

        if (type == LIGHT_DIRECTIONAL)
        {
            Ldir = normalize(-Lg.direction.xyz);
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

        float a = rough * rough;
        float a2 = a * a;

        float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
        float D = a2 / max(3.14159265 * denom * denom, 1e-6);

        float k = (rough + 1.0);
        k = (k * k) / 8.0;
        float Gv = NdotV / (NdotV * (1.0 - k) + k);
        float Gl = NdotL / (NdotL * (1.0 - k) + k);
        float G = Gv * Gl;

        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

        vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-6);

        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metal);

        vec3 diff = kD * albedo / 3.14159265;

        Lo += (diff + spec) * radiance * NdotL;
    }

    vec3 ambient = albedo * 0.02 * ao;

    vec3 color = ambient + Lo;

    o_Color = vec4(color, 1.0);
}
