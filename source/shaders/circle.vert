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

// Particles are spawned with spacing = radius*3 (see main.cpp's Init()), so even with every
// circle rasterizing correctly there's a real gap of ~radius between each particle and its
// neighbors — at high counts that gap-vs-dot spacing beats against the pixel grid as a
// halftone/moire "screen door" pattern, not just an aliasing artifact. kOverlapFactor inflates
// the rendered radius past that natural spacing so neighboring circles' edges overlap and blend
// into a continuous field instead of staying separate dots; kMinPixelRadius is a floor so
// sub-pixel particles at extreme counts still always cover at least one sample. Scaling alpha
// down by the resulting area ratio keeps particles from reading as brighter than their true
// (now visually enlarged) size would justify, so overlapping circles blend rather than blow out.
const float kOverlapFactor  = 1.8;
const float kMinPixelRadius = 1.0;

void main() {
    const vec2 quad[6] = vec2[6](
        vec2(-1,-1), vec2(1,-1), vec2(1,1),
        vec2(-1,-1), vec2(1,1), vec2(-1,1)
    );
    CircleData d          = instances[gl_InstanceIndex];
    vec2 q                = quad[gl_VertexIndex];
    float renderRadius    = max(d.radius * kOverlapFactor, kMinPixelRadius);
    float areaRatio       = (d.radius / renderRadius);
    vec2 ndcCenter        = vec2(d.center.x * invScreen.x, -d.center.y * invScreen.y);
    vec2 ndcRadius        = renderRadius * invScreen;
    localPos    = q * renderRadius;
    fragColor   = vec4(d.color.rgb, d.color.a * areaRatio * areaRatio);
    fragRadius  = renderRadius;
    gl_Position = vec4(ndcCenter + q * ndcRadius, 0.0, 1.0);
}
