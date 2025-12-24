#version 460 core

layout(location = 0) out vec4 o_Color;

uniform samplerCube u_Env;
uniform sampler2D u_Depth;

uniform mat4 u_InvProj;
uniform mat4 u_InvView;

uniform int u_HasEnv;

vec3 reconstruct_view_dir(vec2 uv)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 view = u_InvProj * clip;
    view /= max(view.w, 1e-6);
    return normalize(view.xyz);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(u_Depth, 0));

    float d = texture(u_Depth, uv).r;
    if (d < 1.0)
        discard;

    vec3 vdir = reconstruct_view_dir(uv);

    vec3 wdir = normalize((u_InvView * vec4(vdir, 0.0)).xyz);

    wdir.y = -wdir.y;

    vec3 col = u_HasEnv != 0 ? texture(u_Env, wdir).rgb : vec3(0.0);

    o_Color = vec4(col, 1.0);
}
