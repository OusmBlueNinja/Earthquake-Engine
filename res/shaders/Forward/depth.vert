#version 430 core

layout(location = 0) in vec3 a_Position;
layout(location = 2) in vec2 a_UV;
layout(location = 4) in vec4 a_I0;
layout(location = 5) in vec4 a_I1;
layout(location = 6) in vec4 a_I2;
layout(location = 7) in vec4 a_I3;


uniform mat4 u_View;
uniform mat4 u_Proj;
uniform mat4 u_Model;
uniform int u_UseInstancing;

out vec2 vUV;


mat4 getModel()
{
    if (u_UseInstancing != 0)
        return mat4(a_I0, a_I1, a_I2, a_I3);
    return u_Model;
};

void main()
{
    mat4 M = getModel();
    vUV = a_UV;
    gl_Position = u_Proj * u_View * (M * vec4(a_Position, 1.0));
};
