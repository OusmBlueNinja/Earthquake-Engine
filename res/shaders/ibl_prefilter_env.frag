#version 460 core

in vec2 v_UV;
layout(location=0) out vec4 o_Color;

uniform samplerCube u_EnvMap;
uniform mat4 u_View;
uniform mat4 u_Proj;
uniform float u_Roughness;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float rough)
{
    float a = rough * rough;

    float phi = 2.0 * 3.14159265 * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

vec3 texcoord_to_dir(vec2 uv)
{
    vec2 p = uv * 2.0 - 1.0;
    vec4 clip = vec4(p, 1.0, 1.0);
    vec4 view = inverse(u_Proj) * clip;
    vec3 dirV = normalize(view.xyz / max(view.w, 1e-6));
    vec3 dirW = normalize((inverse(u_View) * vec4(dirV, 0.0)).xyz);
    return dirW;
}

void main()
{
    vec3 R = normalize(texcoord_to_dir(v_UV));
    vec3 N = R;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;

    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, u_Roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefiltered += texture(u_EnvMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefiltered = prefiltered / max(totalWeight, 1e-6);
    o_Color = vec4(prefiltered, 1.0);
}
