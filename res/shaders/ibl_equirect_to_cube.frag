#version 460 core

in vec2 v_UV;
layout(location=0) out vec4 o_Color;

uniform sampler2D u_Equirect;
uniform mat4 u_View;

vec2 dir_to_equirect(vec3 d)
{
    float phi = atan(d.z, d.x);
    float theta = asin(clamp(d.y, -1.0, 1.0));
    return vec2(phi / (2.0 * 3.14159265) + 0.5, theta / 3.14159265 + 0.5);
}

void main()
{
    vec2 p = v_UV * 2.0 - 1.0;

    vec3 dirV = normalize(vec3(p.x, p.y, -1.0));

    mat3 invViewRot = transpose(mat3(u_View));
    vec3 dirW = normalize(invViewRot * dirV);

    vec2 euv = dir_to_equirect(dirW);

    vec3 col = texture(u_Equirect, euv).rgb;
    o_Color = vec4(col, 1.0);
}
