#version 450

layout(push_constant) uniform PC {
    vec4  color;
    vec2  center;
    vec2  screenSize;
    float radius;
} pc;

layout(location = 0) out vec2 localPos;

void main() {
    const vec2 quad[6] = vec2[6](
        vec2(-1,-1), vec2(1,-1), vec2(1,1),
        vec2(-1,-1), vec2(1,1), vec2(-1,1)
    );
    vec2 q = quad[gl_VertexIndex];
    localPos = q * pc.radius;
    vec2 pixelPos = pc.center + localPos;
    vec2 ndc = (pixelPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}