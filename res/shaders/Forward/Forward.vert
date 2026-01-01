#version 430 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in vec4 a_Tangent;

layout(location = 4) in vec4 a_I0;
layout(location = 5) in vec4 a_I1;
layout(location = 6) in vec4 a_I2;
layout(location = 7) in vec4 a_I3;
layout(location = 8) in float a_Fade01;

uniform mat4 u_Model;
uniform int u_UseInstancing;

layout(std140, binding = 0) uniform PerFrame
{
    mat4 u_View;
    mat4 u_Proj;
    vec4 u_CameraPos;
    int u_HeightInvert;
    int u_ManualSRGB;
    int u_ShadowEnabled;
    int u_ShadowCascadeCount;
    int u_ShadowMapSize;
    int u_ShadowLightIndex;
    int u_ShadowPCF;
    int _pad0;
    vec4 u_ShadowSplits;
    float u_ShadowBias;
    float u_ShadowNormalBias;
    float u_IBLIntensity;
    float _pad1;
    mat4 u_ShadowVP[4];
};

out VS_OUT
{
    vec3 worldPos;
    vec3 worldN;
    vec2 uv;
    vec4 tangent;
    float lodFade01;
} v;

mat4 getModel()
{
    if (u_UseInstancing != 0)
        return mat4(a_I0, a_I1, a_I2, a_I3);
    return u_Model;
}

void main()
{
    mat4 M = getModel();

    vec4 wpos = M * vec4(a_Position, 1.0);
    v.worldPos = wpos.xyz;

    mat3 M3 = mat3(M);
    mat3 normalMat = transpose(inverse(M3));
    v.worldN = normalize(normalMat * a_Normal);

    v.uv = a_UV;

    vec3 T = normalize(M3 * a_Tangent.xyz);
    T = normalize(T - v.worldN * dot(v.worldN, T));
    v.tangent = vec4(T, a_Tangent.w);

    v.lodFade01 = (u_UseInstancing != 0) ? a_Fade01 : 0.0;

    gl_Position = u_Proj * u_View * wpos;
}
