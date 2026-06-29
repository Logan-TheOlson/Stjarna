#version 450

layout(push_constant) uniform PC {
    vec4 color;
    vec2 ndcCenter;
    vec2 ndcHalfSize;
    vec2 pixHalfSize;
} pc;

layout(location = 0) out vec2 localPos;

void main() {
    const vec2 quad[6] = vec2[6](
        vec2(-1,-1), vec2(1,-1), vec2(1,1),
        vec2(-1,-1), vec2(1,1), vec2(-1,1)
    );
    vec2 q = quad[gl_VertexIndex];
    localPos = q * pc.pixHalfSize;
    gl_Position = vec4(pc.ndcCenter + q * pc.ndcHalfSize, 0.0, 1.0);
}
