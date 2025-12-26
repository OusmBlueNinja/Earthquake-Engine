#version 460 core

in vec2 v_UV;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;

uniform int u_EnableBloom;
uniform float u_BloomIntensity;

uniform float u_Exposure;
uniform float u_OutputGamma;

uniform int u_EnableAutoExposure;
uniform float u_AutoExposureSpeed;
uniform float u_DeltaTime;

uniform float u_AutoExposureMin;
uniform float u_AutoExposureMax;

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

float luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float scene_avg_luminance()
{
    int levels = textureQueryLevels(u_Scene);
    int lod = max(levels - 1, 0);
    vec3 c = textureLod(u_Scene, vec2(0.5, 0.5), float(lod)).rgb;
    return max(luminance(c), 1e-6);
}

float auto_exposure_value(float curExposure)
{
    float avgLum = scene_avg_luminance();
    float targetExposure = 0.18 / avgLum;

    targetExposure = clamp(targetExposure, u_AutoExposureMin, u_AutoExposureMax);

    float dt = max(u_DeltaTime, 0.0);
    float speed = max(u_AutoExposureSpeed, 0.0);
    float k = 1.0 - exp(-speed * dt);

    return mix(curExposure, targetExposure, k);
}

void main()
{
    vec2 uv = v_UV;

    vec3 scene = texture(u_Scene, uv).rgb;
    vec3 bloom = texture(u_Bloom, uv).rgb;

    vec3 hdr = scene + ((u_EnableBloom != 0) ? bloom * u_BloomIntensity : vec3(0.0));

    float ex = max(u_Exposure, 0.0);
    if (u_EnableAutoExposure != 0)
        ex = auto_exposure_value(ex);

    if (ex > 0.0)
        hdr *= ex;

    vec3 ldr = aces(hdr);

    if (u_OutputGamma > 0.0)
        ldr = linear_to_srgb(ldr, u_OutputGamma);

    FragColor = vec4(ldr, 1.0);
}
