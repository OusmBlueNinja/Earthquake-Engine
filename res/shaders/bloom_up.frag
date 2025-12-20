#version 330 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Low;
uniform sampler2D u_High;
uniform float u_TexelX;
uniform float u_TexelY;
uniform float u_Intensity;

void main()
{
    vec3 low = texture(u_Low, v_UV).rgb;

    vec2 t = vec2(u_TexelX, u_TexelY);
    vec3 h = vec3(0.0);
    h += texture(u_High, v_UV + t * vec2(-1.0, -1.0)).rgb;
    h += texture(u_High, v_UV + t * vec2( 1.0, -1.0)).rgb;
    h += texture(u_High, v_UV + t * vec2(-1.0,  1.0)).rgb;
    h += texture(u_High, v_UV + t * vec2( 1.0,  1.0)).rgb;
    h *= 0.25;

    vec3 outc = low + h * u_Intensity;
    FragColor = vec4(outc, 1.0);
}
