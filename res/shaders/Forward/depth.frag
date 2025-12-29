#version 430 core

in vec2 vUV;
in float vFade01;

layout(location = 0) out vec4 o_Color;

uniform sampler2D u_AlbedoTex;
uniform float u_AlphaCutoff;
uniform int u_LodXFadeEnabled;
uniform int u_LodXFadeMode;

void main()
{
    float a = texture(u_AlbedoTex, vUV).a;
    if (a < u_AlphaCutoff)
        discard;

    if (u_LodXFadeEnabled != 0)
    {
        float f = clamp(vFade01, 0.0, 1.0);
        float w = (u_LodXFadeMode == 0) ? (1.0 - f) : f;
        w = clamp(w, 0.0, 1.0);
        o_Color = vec4(0.0, 0.0, 0.0, w);
        if (w <= 0.0)
            discard;
        return;
    }

    o_Color = vec4(0.0, 0.0, 0.0, 1.0);
}
