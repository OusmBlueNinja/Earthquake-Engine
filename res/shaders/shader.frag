#version 330 core

#ifndef MAX_LIGHTS
#define MAX_LIGHTS 16
#endif

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct Light {
  int type;
  vec3 position;
  vec3 direction;
  vec3 color;
  float intensity;
  float radius;
  float range;
};

uniform int u_LightCount;
uniform Light u_Lights[MAX_LIGHTS];

uniform vec3 u_CameraPos;

uniform int u_HasMaterial;
uniform vec3 u_Albedo;
uniform vec3 u_Emissive;
uniform float u_Roughness;
uniform float u_Metallic;
uniform float u_Opacity;

in vec3 v_FragPos;
in vec3 v_Normal;

out vec4 FragColor;

float checker2(vec2 uv) {
  vec2 c = floor(uv);
  return mod(c.x + c.y, 2.0);
}

vec3 no_material_pattern(vec3 p, vec3 n) {
  vec3 an = abs(n);
  an *= 1.0 / (an.x + an.y + an.z + 1e-6);

  float scale = 6.0;

  float cx = checker2(p.zy * scale);
  float cy = checker2(p.xz * scale);
  float cz = checker2(p.xy * scale);

  float c = cx * an.x + cy * an.y + cz * an.z;

  return mix(vec3(0.03), vec3(1.0, 0.0, 0.7), c);
}

float range_atten(float dist, float r) {
  float x = clamp(1.0 - dist / max(r, 1e-6), 0.0, 1.0);
  return x * x;
}

float spot_factor(vec3 fragPos, Light light) {
  vec3 sd = light.direction;
  float sd2 = dot(sd, sd);
  if (sd2 < 1e-8)
    sd = vec3(0.0, -1.0, 0.0);
  sd = normalize(sd);

  vec3 lightToFrag = normalize(fragPos - light.position);
  float theta = dot(lightToFrag, sd);

  float r = max(light.radius, 1e-4);
  float d = max(light.range, 1e-4);

  float outerAng = atan(r, d);
  float innerAng = outerAng * 0.75;

  float outer = cos(outerAng);
  float inner = cos(innerAng);

  return clamp((theta - outer) / max(inner - outer, 1e-6), 0.0, 1.0);
}

vec3 calculate_light(Light light, vec3 normal, vec3 viewDir, vec3 fragPos) {
  vec3 lightDir;
  float atten = 1.0;
  float spot = 1.0;

  if (light.type == LIGHT_DIRECTIONAL) {
    vec3 d = light.direction;
    float dl2 = dot(d, d);
    if (dl2 < 1e-8)
      d = vec3(0.0, -1.0, 0.0);
    lightDir = normalize(-d);
    atten = 1.0;
  } else {
    vec3 L = light.position - fragPos;
    float dist2 = dot(L, L);
    float invDist = inversesqrt(max(dist2, 1e-8));
    float dist = 1.0 / invDist;
    lightDir = L * invDist;

    float reach = (light.type == LIGHT_SPOT) ? light.range : light.radius;
    atten = range_atten(dist, reach);
    if (atten <= 0.0)
      return vec3(0.0);

    if (light.type == LIGHT_SPOT) {
      spot = spot_factor(fragPos, light);
      if (spot <= 0.0)
        return vec3(0.0);
    }
  }

  float ndl = max(dot(normal, lightDir), 0.0);
  vec3 diffuse = ndl * light.color * light.intensity * atten * spot;

  vec3 h = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, h), 0.0), 32.0);
  vec3 specular = spec * light.color * light.intensity * atten * spot;

  return diffuse + specular;
}

void main() {
  vec3 n = normalize(v_Normal);

  if (u_HasMaterial == 0) {
    FragColor = vec4(no_material_pattern(v_FragPos, n), 1.0);
    return;
  }

  vec3 v = normalize(u_CameraPos - v_FragPos);

  vec3 base = u_Albedo;
  vec3 color = u_Emissive + base * 0.12;

  int lc = clamp(u_LightCount, 0, MAX_LIGHTS);
  for (int i = 0; i < lc; ++i)
    color += calculate_light(u_Lights[i], n, v, v_FragPos) * base;

  FragColor = vec4(color, u_Opacity);
}
