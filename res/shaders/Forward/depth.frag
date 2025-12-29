#version 430 core

in vec2 vUV;
uniform sampler2D u_AlbedoTex;
uniform float u_AlphaCutoff;

void main() {
  float a = texture(u_AlbedoTex, vUV).a;
  if (a < u_AlphaCutoff)
    discard;
}
