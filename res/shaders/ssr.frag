#version 460 core

in vec2 v_UV;
layout(location=0) out vec4 o_Color;

uniform sampler2D u_Scene;
uniform sampler2D u_GDepth;
uniform sampler2D u_GNormal;
uniform sampler2D u_GMaterial;

uniform mat4 u_View;
uniform mat4 u_Proj;
uniform mat4 u_InvView;
uniform mat4 u_InvProj;

uniform vec2 u_InvResolution;
uniform float u_Intensity;
uniform int u_Steps;
uniform float u_Stride;
uniform float u_Thickness;
uniform float u_MaxDist;

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

vec2 project_to_uv(vec3 viewPos)
{
    vec4 clip = u_Proj * vec4(viewPos, 1.0);
    vec2 ndc = clip.xy / max(clip.w, 1e-6);
    return ndc * 0.5 + 0.5;
}

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

void main()
{
    vec3 base = texture(u_Scene, v_UV).rgb;

    float depth = texture(u_GDepth, v_UV).r;
    if (depth >= 1.0)
    {
        o_Color = vec4(base, 1.0);
        return;
    }

    vec3 N = decode_normal(texture(u_GNormal, v_UV));
    vec4 mm = texture(u_GMaterial, v_UV);
    float rough = clamp(mm.r, 0.02, 1.0);

    vec3 Pvs = reconstruct_view_pos(v_UV, depth);
    vec3 V = normalize(-Pvs);

    vec3 R = reflect(-V, N);

    float edgeFade = saturate(min(min(v_UV.x, 1.0 - v_UV.x), min(v_UV.y, 1.0 - v_UV.y)) * 20.0);
    float roughFade = saturate(1.0 - rough);
    float w0 = u_Intensity * edgeFade * roughFade;

    if (w0 <= 1e-4)
    {
        o_Color = vec4(base, 1.0);
        return;
    }

    float stepLen = max(u_Stride, 1e-4);
    vec3 dir = normalize(R);

    vec3 hitCol = vec3(0.0);
    float hit = 0.0;

    float t = stepLen;

    for (int i = 0; i < u_Steps; ++i)
    {
        if (t > u_MaxDist)
            break;

        vec3 Q = Pvs + dir * t;
        vec2 uv = project_to_uv(Q);

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;

        float d2 = texture(u_GDepth, uv).r;
        if (d2 >= 1.0)
        {
            t += stepLen;
            continue;
        }

        vec3 QvsScene = reconstruct_view_pos(uv, d2);

        float dz = QvsScene.z - Q.z;

        if (abs(dz) < u_Thickness)
        {
            hitCol = texture(u_Scene, uv).rgb;
            hit = 1.0;
            break;
        }

        t += stepLen;
    }

    float w = w0 * hit;
    vec3 outc = mix(base, hitCol, saturate(w));
    o_Color = vec4(outc, 1.0);
}
