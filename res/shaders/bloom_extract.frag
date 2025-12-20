#version 330 core
in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Src;
uniform float u_Threshold;
uniform float u_Knee;

float lum(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    vec3 c = texture(u_Src, v_UV).rgb;

    float l = lum(c);
    float t = u_Threshold;
    float k = max(u_Knee, 1e-6);

    float x = l - t;
    float soft = clamp(x / (2.0 * k) + 0.5, 0.0, 1.0);
    float w = max(x, 0.0) + (soft * soft) * k;

    vec3 outc = c * (w / max(l, 1e-6));
    FragColor = vec4(outc, 1.0);
}
