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

    c += texture(u_Src, v_UV + t * vec2(-0.5, -0.5)).rgb;
    c += texture(u_Src, v_UV + t * vec2( 0.5, -0.5)).rgb;
    c += texture(u_Src, v_UV + t * vec2(-0.5,  0.5)).rgb;
    c += texture(u_Src, v_UV + t * vec2( 0.5,  0.5)).rgb;

    FragColor = vec4(c * 0.25, 1.0);
}
