#version 450

struct RectData {
    vec4 color;
    vec4 rect;   // (cx, cy, halfWidth, halfHeight)
    vec4 border; // border.x = frame thickness in pixels
};

layout(set = 0, binding = 0) readonly buffer RectBuffer {
    RectData instances[];
};

layout(push_constant) uniform PC {
    vec2 invScreen;  // (2/width, 2/height) — NDC units per pixel
};

layout(location = 0) out vec2  localPos;
layout(location = 1) out vec4  fragColor;
layout(location = 2) out vec2  fragHalfExtent;
layout(location = 3) out float fragBorder;

void main() {
    const vec2 quad[6] = vec2[6](
        vec2(-1,-1), vec2(1,-1), vec2(1,1),
        vec2(-1,-1), vec2(1,1), vec2(-1,1)
    );
    RectData d          = instances[gl_InstanceIndex];
    vec2 q              = quad[gl_VertexIndex];
    vec2 center         = d.rect.xy;
    vec2 halfExtent     = d.rect.zw;
    vec2 ndcCenter      = vec2(center.x * invScreen.x, -center.y * invScreen.y);
    vec2 ndcHalfExtent  = halfExtent * invScreen;
    localPos       = q * halfExtent;
    fragColor      = d.color;
    fragHalfExtent = halfExtent;
    fragBorder     = d.border.x;
    gl_Position    = vec4(ndcCenter + q * ndcHalfExtent, 0.0, 1.0);
}
