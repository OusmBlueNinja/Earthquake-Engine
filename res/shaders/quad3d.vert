#version 430 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_UV;
layout(location = 2) in vec4 a_Color;

uniform mat4 u_View;
uniform mat4 u_Proj;

out vec2 v_UV;
out vec4 v_Color;

void main()
{
    v_UV = a_UV;
    v_Color = a_Color;
    gl_Position = u_Proj * u_View * vec4(a_Position, 1.0);
}

