#version 460 core

in vec2 v_UV;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform sampler2D u_Depth;
uniform int u_EnableBloom;
uniform float u_BloomIntensity;
uniform int u_DebugMode;

uniform float u_Exposure;
uniform float u_OutputGamma;

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

vec3 linear_to_srgb(vec3 c, float gammaVal)
{
    return pow(max(c, vec3(0.0)), vec3(1.0 / max(gammaVal, 1e-6)));
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

    float ex = max(u_Exposure, 0.0);
    if (ex > 0.0)
        hdr *= ex;

    vec3 ldr = aces(hdr);

    if (u_OutputGamma > 0.0)
        ldr = linear_to_srgb(ldr, u_OutputGamma);

    FragColor = vec4(ldr, 1.0);
}
