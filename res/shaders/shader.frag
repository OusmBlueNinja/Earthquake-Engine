#version 330 core

#define MAX_LIGHTS 16

struct Light {
  int type;
  vec3 position;
  vec3 direction;
  vec3 color;
  float intensity;
};

uniform int u_LightCount;
uniform Light u_Lights[MAX_LIGHTS];

uniform vec3 u_CameraPos;

uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;

in vec3 v_FragPos;
in vec3 v_Normal;
in vec2 v_TexCoord;

out vec4 FragColor;

vec3 calculate_light(Light light, vec3 normal, vec3 viewDir, vec3 fragPos) {
  vec3 result = vec3(0.0);
  vec3 lightDir;

  if (light.type == 0)
    lightDir = normalize(-light.direction);
  else
    lightDir = normalize(light.position - fragPos);

  float diff = max(dot(normal, lightDir), 0.0);
  vec3 diffuse = diff * light.color * light.intensity;

  vec3 halfwayDir = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
  vec3 specular = spec * light.color * light.intensity;

  result = diffuse + specular;
  return result;
}

void main() {
  vec3 norm = normalize(v_Normal);
  vec3 viewDir = normalize(u_CameraPos - v_FragPos);

  vec3 color = u_Emissive;

  for (int i = 0; i < u_LightCount; i++) {
    color += calculate_light(u_Lights[i], norm, viewDir, v_FragPos) * u_Albedo;
  }

  FragColor = vec4(color, u_Opacity);
}
