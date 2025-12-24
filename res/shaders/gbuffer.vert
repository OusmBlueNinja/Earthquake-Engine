#version 460 core
layout(location=0) in vec3 a_Position;
layout(location=1) in vec3 a_Normal;
layout(location=2) in vec2 a_UV;
layout(location=3) in vec4 a_Tangent;

layout(location=4) in vec4 i_M0;
layout(location=5) in vec4 i_M1;
layout(location=6) in vec4 i_M2;
layout(location=7) in vec4 i_M3;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Proj;
uniform int u_UseInstancing;

out VS_OUT
{
    vec3 ws_pos;
    vec3 ws_nrm;
    vec2 uv;
    mat3 tbn;
} v;

mat3 make_tbn(vec3 n, vec4 t)
{
    vec3 T = normalize(t.xyz);
    vec3 N = normalize(n);
    vec3 B = normalize(cross(N, T)) * (t.w < 0.0 ? -1.0 : 1.0);
    return mat3(T, B, N);
}

mat4 get_model()
{
    if (u_UseInstancing != 0)
        return mat4(i_M0, i_M1, i_M2, i_M3);
    return u_Model;
}

void main()
{
    mat4 M = get_model();

    vec4 wp = M * vec4(a_Position, 1.0);
    v.ws_pos = wp.xyz;

    vec3 wn = mat3(M) * a_Normal;
    v.ws_nrm = normalize(wn);

    v.uv = a_UV;
    v.tbn = make_tbn(v.ws_nrm, a_Tangent);

    gl_Position = u_Proj * u_View * wp;
}
