#version 430 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in vec4 a_Tangent;

layout(location = 4) in vec4 a_I0;
layout(location = 5) in vec4 a_I1;
layout(location = 6) in vec4 a_I2;
layout(location = 7) in vec4 a_I3;

uniform mat4 u_View;
uniform mat4 u_Proj;
uniform mat4 u_Model;
uniform int u_UseInstancing;

out VS_OUT
{
    vec3 worldPos;
    vec3 worldN;
    vec2 uv;
    vec4 tangent;
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

    mat3 Nrm = mat3(transpose(inverse(M)));
    v.worldN = normalize(Nrm * a_Normal);

    v.uv = a_UV;
    v.tangent = a_Tangent;

    gl_Position = u_Proj * u_View * wpos;
}
