#version 330 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Low;
uniform sampler2D u_High;
uniform float u_TexelX;
uniform float u_TexelY;
uniform float u_Intensity;

vec3 tent9(sampler2D t, vec2 uv, vec2 px)
{
    vec3 s = vec3(0.0);
    s += texture(t, uv).rgb * 4.0;

    s += texture(t, uv + vec2( px.x, 0.0)).rgb * 2.0;
    s += texture(t, uv + vec2(-px.x, 0.0)).rgb * 2.0;
    s += texture(t, uv + vec2(0.0,  px.y)).rgb * 2.0;
    s += texture(t, uv + vec2(0.0, -px.y)).rgb * 2.0;

    s += texture(t, uv + vec2( px.x,  px.y)).rgb;
    s += texture(t, uv + vec2(-px.x,  px.y)).rgb;
    s += texture(t, uv + vec2( px.x, -px.y)).rgb;
    s += texture(t, uv + vec2(-px.x, -px.y)).rgb;

    return s * (1.0 / 16.0);
}

void main()
{
    vec2 px = vec2(u_TexelX, u_TexelY);

    vec3 low = tent9(u_Low, v_UV, px);
    vec3 high = texture(u_High, v_UV).rgb;

    vec3 outc = high + low * u_Intensity;
    FragColor = vec4(outc, 1.0);
}
