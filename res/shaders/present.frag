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

layout(std430, binding = 1) readonly buffer TileIndexBuf
{
    uvec2 tileIndex[];
};

uniform int u_TileSize;
uniform int u_TileCountX;
uniform int u_TileCountY;
uniform int u_TileMax;

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

float sat(float x) { return clamp(x, 0.0, 1.0); }

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 heatmap(float t)
{
    t = sat(t);
    float h = (1.0 - t) * 0.66;
    return hsv2rgb(vec3(h, 1.0, 1.0));
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

    if (u_DebugMode == 4)
    {
        float d = texture(u_Depth, uv).r;
        float bg = step(0.999999, d);
        FragColor = vec4(bg, 0.0, 1.0 - bg, 1.0);
        return;
    }

    if (u_DebugMode == 5)
    {
        int ts = max(u_TileSize, 1);
        int tcx = max(u_TileCountX, 1);
        int tcy = max(u_TileCountY, 1);

        int tx = int(floor(gl_FragCoord.x / float(ts)));
        int ty = int(floor(gl_FragCoord.y / float(ts)));
        tx = clamp(tx, 0, tcx - 1);
        ty = clamp(ty, 0, tcy - 1);

        uint tileId = uint(ty) * uint(tcx) + uint(tx);
        uint count = tileIndex[tileId].y;

        float denom = float(max(u_TileMax, 1));
        float t = float(count) / denom;

        vec3 col = heatmap(t);

        float fx = mod(gl_FragCoord.x, float(ts));
        float fy = mod(gl_FragCoord.y, float(ts));
        float grid = (fx < 1.0 || fy < 1.0) ? 0.65 : 1.0;

        FragColor = vec4(col * grid, 1.0);
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
