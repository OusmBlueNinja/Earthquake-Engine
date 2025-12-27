#version 430 core

layout(location = 0) out vec4 o_Color;

in VS_OUT
{
    vec3 worldPos;
    vec3 worldN;
    vec2 uv;
    vec4 tangent;
    float lodFade01;
} v;

uniform vec3 u_CameraPos;

uniform int u_HasMaterial;

uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;

uniform float u_NormalStrength;
uniform float u_HeightScale;
uniform int u_HeightSteps;

uniform int u_MaterialTexMask;

uniform sampler2D u_AlbedoTex;
uniform sampler2D u_NormalTex;
uniform sampler2D u_MetallicTex;
uniform sampler2D u_RoughnessTex;
uniform sampler2D u_EmissiveTex;
uniform sampler2D u_OcclusionTex;
uniform sampler2D u_HeightTex;
uniform sampler2D u_ArmTex;

uniform int u_HeightInvert;
uniform int u_AlphaTest;
uniform float u_AlphaCutoff;
uniform int u_ManualSRGB;

uniform int u_HasIBL;
uniform float u_IBLIntensity;
uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D u_BRDFLUT;

uniform int u_TileSize;
uniform int u_TileCountX;
uniform int u_TileCountY;
uniform int u_TileMax;

uniform int u_DebugMode;

uniform int u_LodXFadeEnabled;
uniform int u_LodXFadeMode;

struct GPU_Light
{
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
    ivec4 meta;
};

layout(std430, binding = 0) readonly buffer B_Lights
{
    GPU_Light g_Lights[];
};

layout(std430, binding = 1) readonly buffer B_TileIndex
{
    uvec2 g_TileIndex[];
};

layout(std430, binding = 2) readonly buffer B_TileList
{
    uint g_TileList[];
};

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 tangent_space_normal(vec2 uv)
{
    vec3 n = texture(u_NormalTex, uv).xyz * 2.0 - 1.0;
    n.xy *= u_NormalStrength;
    return normalize(n);
}

mat3 build_tbn(vec3 N, vec4 tangent)
{
    vec3 n = normalize(N);
    vec3 t = tangent.xyz;
    float t2 = dot(t, t);
    if (t2 < 1e-12)
    {
        vec3 up = (abs(n.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        vec3 T = normalize(cross(up, n));
        vec3 B = normalize(cross(n, T));
        return mat3(T, B, n);
    }

    vec3 T = normalize(t);
    T = normalize(T - n * dot(n, T));
    vec3 B = normalize(cross(n, T)) * tangent.w;
    return mat3(T, B, n);
}

float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-8);
}

float G_SchlickGGX(float NoV, float k)
{
    return NoV / max(NoV * (1.0 - k) + k, 1e-8);
}

float G_Smith(float NoV, float NoL, float a)
{
    float r = a + 1.0;
    float k = (r * r) / 8.0;
    return G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
}

vec3 F_Schlick(vec3 F0, float VoH)
{
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

vec2 parallax_uv(vec2 uv, vec3 V_ts)
{
    int steps = u_HeightSteps;
    float scale = u_HeightScale;
    if (steps < 1 || scale <= 0.0)
        return uv;

    float layerCount = float(steps);
    float layerDepth = 1.0 / layerCount;
    float curDepth = 0.0;

    vec2 P = (V_ts.xy / max(V_ts.z, 1e-4)) * scale;
    vec2 dUV = P / layerCount;

    vec2 curUV = uv;

    float h = texture(u_HeightTex, curUV).r;
    if (u_HeightInvert != 0)
        h = 1.0 - h;

    while (curDepth < h)
    {
        curUV -= dUV;
        curDepth += layerDepth;

        h = texture(u_HeightTex, curUV).r;
        if (u_HeightInvert != 0)
            h = 1.0 - h;
    }

    return curUV;
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

vec3 debug_lod_tint(vec3 c, int d)
{
    if (d <= 0)
        return mix(c, c * vec3(0.2, 1.0, 0.2), 0.95);

    vec3 t = vec3(1.0);

    if (d == 1) t = vec3(0.05, 0.35, 0.05);
    else if (d == 2) t = vec3(0.05, 0.8, 0.7);
    else if (d == 3) t = vec3(0.2, 0.35, 1.0);
    else if (d == 4) t = vec3(0.65, 0.25, 1.0);
    else t = vec3(1.0, 0.2, 1.0);

    return mix(c, c * t, 0.95);
}

int dbg_mode()
{
    return (u_DebugMode & 255);
}

int dbg_lod_p1()
{
    return ((u_DebugMode >> 8) & 255);
}

float dither_noise(vec2 p)
{
    float x = dot(p, vec2(0.06711056, 0.00583715));
    return fract(52.9829189 * fract(x));
}

void main()
{
    vec3 Nw = normalize(v.worldN);
    vec3 Vw = normalize(u_CameraPos - v.worldPos);

    mat3 TBN = build_tbn(Nw, v.tangent);

    vec2 uv = v.uv;

    if ((u_MaterialTexMask & (1 << 6)) != 0 && u_HeightScale > 0.0 && u_HeightSteps > 0)
    {
        vec3 Vts = normalize(transpose(TBN) * Vw);
        uv = parallax_uv(uv, Vts);
    }

    float alpha_tex = 1.0;
    if ((u_MaterialTexMask & (1 << 0)) != 0)
        alpha_tex = texture(u_AlbedoTex, uv).a;
    
    float alpha = alpha_tex * u_Opacity;
    
    if (u_AlphaTest != 0)
    {
        float w = max(fwidth(alpha), 1e-4);
        float a = smoothstep(u_AlphaCutoff - w, u_AlphaCutoff + w, alpha);
        if (a <= 0.0)
            discard;
        alpha = 1.0;
    }


    if (u_LodXFadeEnabled != 0)
    {
        float f = clamp(v.lodFade01, 0.0, 1.0);
        float n = dither_noise(gl_FragCoord.xy);

        if (u_LodXFadeMode == 0)
        {
            if (n < f)
                discard;
        }
        else
        {
            if (n >= f)
                discard;
        }
    }

    vec3 N = Nw;
    if ((u_MaterialTexMask & (1 << 1)) != 0)
    {
        vec3 Nts = tangent_space_normal(uv);
        N = normalize(TBN * Nts);
    }

    int mode = dbg_mode();

    ivec2 tileXY = ivec2(int(gl_FragCoord.x) / max(u_TileSize, 1), int(gl_FragCoord.y) / max(u_TileSize, 1));
    tileXY.x = clamp(tileXY.x, 0, max(u_TileCountX - 1, 0));
    tileXY.y = clamp(tileXY.y, 0, max(u_TileCountY - 1, 0));
    int tileIndex = tileXY.x + tileXY.y * u_TileCountX;

    uvec2 sc = g_TileIndex[uint(tileIndex)];
    uint start = sc.x;
    uint count = sc.y;

    vec3 overlay2 = vec3(0.0);
    float overlay2_w = 0.0;

    if (mode == 2)
    {
        float denom = float(max(u_TileMax, 1));
        float tt = log2(1.0 + float(count)) / log2(1.0 + denom);
        vec3 col = heatmap(tt);

        vec2 f = fract(gl_FragCoord.xy / float(max(u_TileSize, 1)));
        float edge = 0.0;
        edge = max(edge, 1.0 - step(0.03, f.x));
        edge = max(edge, 1.0 - step(0.03, f.y));
        edge = max(edge, 1.0 - step(0.03, 1.0 - f.x));
        edge = max(edge, 1.0 - step(0.03, 1.0 - f.y));
        col = mix(col, vec3(0.0), edge);

        overlay2 = col;
        overlay2_w = 0.5;
    }

    if (mode == 3)
    {
        vec3 nn = normalize(N) * 0.5 + 0.5;
        o_Color = vec4(nn, 1.0);
        return;
    }

    if (mode == 4)
    {
        o_Color = vec4(1.0, 1.0, 1.0, alpha);
        return;
    }

    vec3 albedo = (u_HasMaterial != 0) ? u_Albedo : vec3(1.0);
    float roughness = (u_HasMaterial != 0) ? u_Roughness : 1.0;
    float metallic = (u_HasMaterial != 0) ? u_Metallic : 0.0;
    vec3 emissive = (u_HasMaterial != 0) ? u_Emissive : vec3(0.0);

    if ((u_MaterialTexMask & (1 << 0)) != 0)
    {
        vec3 a = texture(u_AlbedoTex, uv).rgb;
        if (u_ManualSRGB != 0)
            a = srgb_to_linear(a);
        albedo *= a;
    }

    if ((u_MaterialTexMask & (1 << 7)) != 0)
    {
        vec3 arm = texture(u_ArmTex, uv).rgb;
        roughness *= arm.g;
        metallic *= arm.b;
    }
    else
    {
        if ((u_MaterialTexMask & (1 << 3)) != 0)
            roughness *= texture(u_RoughnessTex, uv).r;
        if ((u_MaterialTexMask & (1 << 2)) != 0)
            metallic *= texture(u_MetallicTex, uv).r;
    }

    float ao = 1.0;
    if ((u_MaterialTexMask & (1 << 5)) != 0)
        ao = texture(u_OcclusionTex, uv).r;

    if ((u_MaterialTexMask & (1 << 4)) != 0)
    {
        vec3 e = texture(u_EmissiveTex, uv).rgb;
        if (u_ManualSRGB != 0)
            e = srgb_to_linear(e);
        emissive *= e;
    }

    roughness = clamp(roughness, 0.02, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    albedo = max(albedo, vec3(0.0));

    float NoV = saturate(dot(N, Vw));
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    for (uint ii = 0u; ii < count; ++ii)
    {
        uint li = g_TileList[start + ii];
        GPU_Light Ld = g_Lights[li];

        vec3 L;
        float atten = 1.0;

        if (Ld.meta.x == 1)
        {
            L = normalize(-Ld.direction.xyz);
            atten = 1.0;
        }
        else
        {
            vec3 toL = Ld.position.xyz - v.worldPos;
            float d2 = dot(toL, toL);
            float d = sqrt(max(d2, 1e-8));
            L = toL / d;

            float range = Ld.params.z;
            if (range > 1e-4)
            {
                float x = saturate(1.0 - d / range);
                atten = x * x;
            }
            else
            {
                atten = 1.0 / max(d2, 1e-4);
            }
        }

        float NoL = saturate(dot(N, L));
        if (NoL <= 1e-5)
            continue;

        vec3 H = normalize(Vw + L);
        float NoH = saturate(dot(N, H));
        float VoH = saturate(dot(Vw, H));

        float a = roughness * roughness;

        float D = D_GGX(NoH, a);
        float G = G_Smith(NoV, NoL, a);
        vec3 F = F_Schlick(F0, VoH);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 radiance = Ld.color.rgb * Ld.params.x * atten;

        vec3 spec = (D * G * F) / max(4.0 * NoV * NoL, 1e-6);
        vec3 diff = kD * (albedo / 3.14159265);

        Lo += (diff + spec) * radiance * NoL;
    }

    vec3 color = Lo * ao;

    if (u_HasIBL != 0)
    {
        vec3 kS = F_Schlick(F0, NoV);
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;

        vec3 R = reflect(-Vw, N);
        float maxLod = 5.0;
        vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * maxLod).rgb;
        vec2 brdf = texture(u_BRDFLUT, vec2(NoV, roughness)).rg;
        vec3 specIBL = prefiltered * (kS * brdf.x + brdf.y);

        color += (kD * diffuseIBL * ao + specIBL) * u_IBLIntensity;
    }

    color += emissive;

    if (mode == 1)
        color = debug_lod_tint(color, dbg_lod_p1());

    vec3 mapped = vec3(1.0) - exp(-color);

    if (mode == 2)
        mapped = mix(mapped, overlay2, overlay2_w);

    o_Color = vec4(mapped, alpha);
}
