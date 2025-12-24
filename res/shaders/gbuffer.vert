#version 460 core
layout(location=0) in vec3 a_Position;
layout(location=1) in vec3 a_Normal;
layout(location=2) in vec2 a_UV;
layout(location=3) in vec4 a_Tangent;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Proj;

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

void main()
{
    vec4 wp = u_Model * vec4(a_Position, 1.0);
    v.ws_pos = wp.xyz;

    vec3 wn = mat3(u_Model) * a_Normal;
    v.ws_nrm = normalize(wn);

    v.uv = a_UV;
    v.tbn = make_tbn(v.ws_nrm, a_Tangent);

    gl_Position = u_Proj * u_View * wp;
}
