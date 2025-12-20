#version 330 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Src;
uniform float u_TexelX;
uniform float u_TexelY;

void main()
{
    vec2 t = vec2(u_TexelX, u_TexelY);
    vec3 s = vec3(0.0);
    s += texture(u_Src, v_UV + t * vec2(-1.0, -1.0)).rgb;
    s += texture(u_Src, v_UV + t * vec2( 1.0, -1.0)).rgb;
    s += texture(u_Src, v_UV + t * vec2(-1.0,  1.0)).rgb;
    s += texture(u_Src, v_UV + t * vec2( 1.0,  1.0)).rgb;
    s *= 0.25;
    FragColor = vec4(s, 1.0);
}
