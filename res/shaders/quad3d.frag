#version 430 core

uniform sampler2D u_Tex;

in vec2 v_UV;
in vec4 v_Color;

out vec4 o_Color;

void main()
{
    vec4 texel = texture(u_Tex, v_UV);
    o_Color = texel * v_Color;
}

