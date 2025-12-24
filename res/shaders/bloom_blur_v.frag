#version 330 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Src;
uniform float u_TexelY;

void main()
{
    float y = u_TexelY;
    vec3 c = vec3(0.0);

    c += texture(u_Src, v_UV + vec2(0.0, -4.0*y)).rgb * 0.05;
    c += texture(u_Src, v_UV + vec2(0.0, -3.0*y)).rgb * 0.09;
    c += texture(u_Src, v_UV + vec2(0.0, -2.0*y)).rgb * 0.12;
    c += texture(u_Src, v_UV + vec2(0.0, -1.0*y)).rgb * 0.15;
    c += texture(u_Src, v_UV).rgb                    * 0.18;
    c += texture(u_Src, v_UV + vec2(0.0,  1.0*y)).rgb * 0.15;
    c += texture(u_Src, v_UV + vec2(0.0,  2.0*y)).rgb * 0.12;
    c += texture(u_Src, v_UV + vec2(0.0,  3.0*y)).rgb * 0.09;
    c += texture(u_Src, v_UV + vec2(0.0,  4.0*y)).rgb * 0.05;

    FragColor = vec4(c, 1.0);
}
