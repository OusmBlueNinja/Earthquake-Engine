#version 330 core

layout(location = 0) in vec3 a_Pos;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Proj;

out vec3 v_FragPos;
out vec3 v_Normal;
out vec2 v_TexCoord;

void main() {
  vec4 world = u_Model * vec4(a_Pos, 1.0);
  v_FragPos = world.xyz;

  mat3 nrmMat = mat3(transpose(inverse(u_Model)));
  v_Normal = normalize(nrmMat * a_Normal);

  v_TexCoord = a_UV;

  gl_Position = u_Proj * u_View * world;
}
