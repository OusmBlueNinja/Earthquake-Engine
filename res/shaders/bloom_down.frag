#version 330 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Src;
uniform float u_TexelX;
uniform float u_TexelY;

void main()
{
    vec2 t = vec2(u_TexelX, u_TexelY);

    vec3 c = vec3(0.0);

    c += texture(u_Src, v_UV).rgb * 4.0;

    c += texture(u_Src, v_UV + vec2( t.x, 0.0)).rgb * 2.0;
    c += texture(u_Src, v_UV + vec2(-t.x, 0.0)).rgb * 2.0;
    c += texture(u_Src, v_UV + vec2(0.0,  t.y)).rgb * 2.0;
    c += texture(u_Src, v_UV + vec2(0.0, -t.y)).rgb * 2.0;

    c += texture(u_Src, v_UV + vec2( t.x,  t.y)).rgb;
    c += texture(u_Src, v_UV + vec2(-t.x,  t.y)).rgb;
    c += texture(u_Src, v_UV + vec2( t.x, -t.y)).rgb;
    c += texture(u_Src, v_UV + vec2(-t.x, -t.y)).rgb;

    c *= 1.0 / 16.0;
    FragColor = vec4(c, 1.0);
}
