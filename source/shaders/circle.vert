#version 450

struct CircleData {
    vec4  color;
    vec2  center;
    float radius;
    float _pad;
};

layout(set = 0, binding = 0) readonly buffer CircleBuffer {
    CircleData instances[];
};

layout(push_constant) uniform PC {
    vec2 invScreen;  // (2/width, 2/height) — NDC units per pixel
};

layout(location = 0) out vec2  localPos;
layout(location = 1) out vec4  fragColor;
layout(location = 2) out float fragRadius;

// kMinPixelRadius is a floor so sub-pixel particles at extreme counts still cover at least one
// sample. Circles are otherwise drawn at their true radius with their own alpha.
const float kMinPixelRadius = 1.0;

void main() {
    const vec2 quad[6] = vec2[6](
        vec2(-1,-1), vec2(1,-1), vec2(1,1),
        vec2(-1,-1), vec2(1,1), vec2(-1,1)
    );
    CircleData d          = instances[gl_InstanceIndex];
    vec2 q                = quad[gl_VertexIndex];
    float renderRadius    = max(d.radius, kMinPixelRadius);
    vec2 ndcCenter        = vec2(d.center.x * invScreen.x, -d.center.y * invScreen.y);
    vec2 ndcRadius        = renderRadius * invScreen;
    localPos    = q * renderRadius;
    fragColor   = d.color;
    fragRadius  = renderRadius;
    gl_Position = vec4(ndcCenter + q * ndcRadius, 0.0, 1.0);
}
