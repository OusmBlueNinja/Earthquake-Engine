#version 460 core

in vec2 v_UV;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform sampler2D u_Depth;
uniform int u_EnableBloom;
uniform float u_BloomIntensity;
uniform int u_DebugMode;

out vec4 FragColor;

vec3 aces(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec2 uv = v_UV;

    vec3 scene = texture(u_Scene, uv).rgb;
    vec3 bloom = texture(u_Bloom, uv).rgb;

    if (u_DebugMode == 1) { FragColor = vec4(bloom, 1.0); return; }
    if (u_DebugMode == 2) { FragColor = vec4(scene, 1.0); return; }
    if (u_DebugMode == 3)
    {
        float d = texture(u_Depth, uv).r;
        FragColor = vec4(vec3(d), 1.0);
        return;
    }

    vec3 hdr = scene + ((u_EnableBloom != 0) ? bloom * u_BloomIntensity : vec3(0.0));
    vec3 ldr = aces(hdr);

    FragColor = vec4(ldr, 1.0);
}
