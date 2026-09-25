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
// the rendered radius past that natural spacing so neighboring circles' edges overlap; combined
// with the density-buffer accumulation in circle.frag, that overlap is what lets nearby particles
// blend into each other rather than staying separate dots — bigger kOverlapFactor means particles
// need to be closer together ("higher pressure") before their footprints start merging.
// kMinPixelRadius is a floor so sub-pixel particles at extreme counts still always cover at least
// one sample. Unlike the pre-density-buffer version of this shader, alpha is NOT scaled down by
// the resulting area ratio — the composite pass normalizes accumulated density back to true
// per-particle brightness, so a single isolated (enlarged) particle still reads at full opacity.
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
    vec2 ndcCenter        = vec2(d.center.x * invScreen.x, -d.center.y * invScreen.y);
    vec2 ndcRadius        = renderRadius * invScreen;
    localPos    = q * renderRadius;
    fragColor   = d.color;
    fragRadius  = renderRadius;
    gl_Position = vec4(ndcCenter + q * ndcRadius, 0.0, 1.0);
}
