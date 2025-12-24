#version 460 core

in vec2 v_UV;
layout(location=0) out vec4 o_Color;

uniform samplerCube u_EnvMap;
uniform mat4 u_View;
uniform mat4 u_Proj;

vec3 texcoord_to_dir(vec2 uv)
{
    vec2 p = uv * 2.0 - 1.0;
    vec4 clip = vec4(p, 1.0, 1.0);
    vec4 view = inverse(u_Proj) * clip;
    vec3 dirV = normalize(view.xyz / max(view.w, 1e-6));
    vec3 dirW = normalize((inverse(u_View) * vec4(dirV, 0.0)).xyz);
    return dirW;
}

void main()
{
    vec3 N = normalize(texcoord_to_dir(v_UV));
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    float sampleDelta = 0.05;

    vec3 irradiance = vec3(0.0);
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * 3.14159265; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * 3.14159265; theta += sampleDelta)
        {
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += texture(u_EnvMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }

    irradiance = 3.14159265 * irradiance * (1.0 / max(nrSamples, 1e-6));
    o_Color = vec4(irradiance, 1.0);
}
