#version 450

struct RectData {
    vec4 color;
    vec2 center;
    vec2 halfWH;
};

layout(set = 0, binding = 0) readonly buffer RectBuffer {
    RectData instances[];
};

layout(push_constant) uniform PC {
    vec2 invScreen;  // (2/width, 2/height) — NDC units per pixel
};

layout(location = 0) out vec2 localPos;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec2 fragPixHalfSize;

void main() {
    const vec2 quad[6] = vec2[6](
        vec2(-1,-1), vec2(1,-1), vec2(1,1),
        vec2(-1,-1), vec2(1,1), vec2(-1,1)
    );
    const float QUAD_EXPAND = 1.0;
    RectData d      = instances[gl_InstanceIndex];
    vec2 q          = quad[gl_VertexIndex];
    vec2 ndcCenter  = vec2(d.center.x * invScreen.x, -d.center.y * invScreen.y);
    vec2 ndcHalfWH  = d.halfWH * invScreen;
    localPos        = q * (d.halfWH + vec2(QUAD_EXPAND));
    fragColor       = d.color;
    fragPixHalfSize = d.halfWH;
    gl_Position     = vec4(ndcCenter + q * (ndcHalfWH + invScreen * QUAD_EXPAND), 0.0, 1.0);
}
