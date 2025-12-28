#version 460 core
layout(location=0) out vec4 o_Color;
uniform sampler2D u_Tex;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 c = texelFetch(u_Tex, p, 0).rgb;
    o_Color = vec4(c, 1.0);
}
