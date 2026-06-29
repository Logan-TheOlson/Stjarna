#version 450

layout(push_constant) uniform PC {
    vec4 color;
    vec2 ndcCenter;
    vec2 ndcHalfSize;
    vec2 pixHalfSize;
} pc;

layout(location = 0) in  vec2 localPos;
layout(location = 0) out vec4 outColor;

void main() {
    vec2  d    = abs(localPos) - pc.pixHalfSize;
    float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    float edge = fwidth(dist);
    float alpha = 1.0 - smoothstep(0.0, edge, dist);
    outColor = vec4(pc.color.rgb, pc.color.a * alpha);
}
