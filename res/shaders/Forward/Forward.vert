#version 430 core

layout(location = 0) in vec3 a_Pos;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in vec4 a_Tangent;

layout(location = 4) in vec4 i_M0;
layout(location = 5) in vec4 i_M1;
layout(location = 6) in vec4 i_M2;
layout(location = 7) in vec4 i_M3;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Proj;
uniform int u_UseInstancing;

out VS_OUT
{
    vec3 wPos;
    vec2 uv;
    vec3 wN;
    vec3 wT;
    vec3 wB;
} v;

mat4 getModelMat()
{
    if (u_UseInstancing != 0)
        return mat4(i_M0, i_M1, i_M2, i_M3);
    return u_Model;
}

void main()
{
    mat4 M = getModelMat();

    vec4 wp = M * vec4(a_Pos, 1.0);
    v.wPos = wp.xyz;
    v.uv = a_UV;

    mat3 N = transpose(inverse(mat3(M)));
    v.wN = normalize(N * a_Normal);

    vec3 t = normalize(N * a_Tangent.xyz);
    vec3 b = normalize(cross(v.wN, t)) * a_Tangent.w;

    v.wT = t;
    v.wB = b;

    gl_Position = u_Proj * u_View * vec4(v.wPos, 1.0);
}
