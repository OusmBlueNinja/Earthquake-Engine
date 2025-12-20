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

uniform vec3 u_CameraPos;

uniform int u_HasMaterial;
uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;

uniform int u_HasAlbedoTex;
uniform sampler2D u_AlbedoTex;

uniform int u_HasNormalTex;
uniform sampler2D u_NormalTex;

uniform int u_HasMetallicTex;
uniform sampler2D u_MetallicTex;

uniform int u_HasRoughnessTex;
uniform sampler2D u_RoughnessTex;

uniform int u_HasEmissiveTex;
uniform sampler2D u_EmissiveTex;

uniform int u_HasOcclusionTex;
uniform sampler2D u_OcclusionTex;

uniform int u_HasHeightTex;
uniform sampler2D u_HeightTex;

uniform int u_HasCustomTex;
uniform sampler2D u_CustomTex;

in vec3 v_FragPos;
in vec3 v_Normal;
in vec2 v_TexCoord;

out vec4 FragColor;

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

vec3 calculate_light(GpuLight L, vec3 normal, vec3 viewDir, vec3 fragPos)
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
    float specPow = spec_power_from_roughness(u_Roughness);
    float spec = pow(max(dot(normal, h), 0.0), specPow);

    return radiance * (ndl + spec * 0.25);
}

void main()
{
    vec3 n = normalize(v_Normal);

    if (u_HasMaterial == 0)
    {
        FragColor = vec4(no_material_pattern(v_FragPos, n), 1.0);
        return;
    }

    vec3 vdir = normalize(u_CameraPos - v_FragPos);

    vec3 base = u_Albedo;
    if (u_HasAlbedoTex == 1)
        base *= texture(u_AlbedoTex, v_TexCoord).rgb;

    vec3 emiss = u_Emissive;
    if (u_HasEmissiveTex == 1)
        emiss += texture(u_EmissiveTex, v_TexCoord).rgb;

    float ao = 1.0;
    if (u_HasOcclusionTex == 1)
        ao = texture(u_OcclusionTex, v_TexCoord).r;

    vec3 color = emiss + base * 0.12;

    int lc = clamp(u_LB.header.x, 0, MAX_LIGHTS);
    for (int i = 0; i < lc; ++i)
        color += calculate_light(u_LB.lights[i], n, vdir, v_FragPos) * base;

    color *= ao;

    FragColor = vec4(color, clamp(u_Opacity, 0.0, 1.0));
}
